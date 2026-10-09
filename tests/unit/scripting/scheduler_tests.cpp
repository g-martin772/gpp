#include <catch2/catch_test_macros.hpp>

import GPP;
import std;

using namespace GPP;

namespace
{
    struct Harness
    {
        LuauVm Vm;
        std::shared_ptr<LuauTaskErrors> Errors = std::make_shared<LuauTaskErrors>();
        std::vector<double> Log;
        bool Flag{false};
        std::unique_ptr<LuauScheduler> Scheduler;

        Harness()
        {
            Vm.Register("test", "log", [this](LuauNativeCall& call)
            {
                Log.push_back(call.Number(1));
                return 0;
            });
            Vm.Register("test", "flag", [this](LuauNativeCall& call)
            {
                call.PushBoolean(Flag);
                return 1;
            });
            RegisterTaskBindings(Vm, Errors);
            Vm.Seal();
            Scheduler = std::make_unique<LuauScheduler>(Vm, Errors);
        }

        // The body runs with S bound to the scheduler.
        std::expected<LuauValue, LuauError> Run(const std::string& body)
        {
            auto chunk = Vm.Load("local M = {}\nfunction M.run(S)\n" + body + "\nend\nreturn M", "=task_test");
            REQUIRE(chunk.has_value());
            const std::array<int, 1> refs{Scheduler->Handle()};
            auto result = Vm.CallWith(*chunk, "run", refs);
            Vm.Release(*chunk);
            return result;
        }

        void Tick(const int count, const float dt)
        {
            for (int i = 0; i < count; ++i) Scheduler->Tick(dt);
        }
    };
}

TEST_CASE("Tasks run until their first wait and resume once the simulated clock passes it", "[luau][scheduler]")
{
    Harness h;
    REQUIRE(h.Run("S.spawn(function() test.log(1) task.wait(1.0) test.log(2) task.wait(0.5) test.log(3) end)").has_value());
    CHECK(h.Log == std::vector<double>{1});
    h.Tick(3, 0.25f);
    CHECK(h.Log == std::vector<double>{1});
    h.Tick(1, 0.25f);
    CHECK(h.Log == std::vector<double>{1, 2});
    h.Tick(1, 0.25f);
    CHECK(h.Log == std::vector<double>{1, 2});
    h.Tick(1, 0.25f);
    CHECK(h.Log == std::vector<double>{1, 2, 3});
    CHECK(h.Scheduler->Active() == 0);
    CHECK(h.Errors->Items.empty());
}

TEST_CASE("Wait returns the elapsed tick and frame resumes on the next tick", "[luau][scheduler]")
{
    Harness h;
    REQUIRE(h.Run("S.spawn(function() local dt = task.frame() test.log(dt) test.log(task.wait(0)) end)").has_value());
    h.Tick(1, 0.5f);
    CHECK(h.Log == std::vector<double>{0.5});
    h.Tick(1, 0.125f);
    CHECK(h.Log == std::vector<double>{0.5, 0.125});
}

TEST_CASE("Wait-until polls its predicate every tick", "[luau][scheduler]")
{
    Harness h;
    REQUIRE(h.Run("S.spawn(function() task.wait_until(test.flag) test.log(7) end)").has_value());
    h.Tick(3, 0.1f);
    CHECK(h.Log.empty());
    h.Flag = true;
    h.Tick(1, 0.1f);
    CHECK(h.Log == std::vector<double>{7});
}

TEST_CASE("Events wake every waiter with the signalled arguments", "[luau][scheduler]")
{
    Harness h;
    REQUIRE(h.Run(R"(
        S.spawn(function() local a, b = task.wait_event("go") test.log(a + b) end)
        S.spawn(function() task.wait_event("go") test.log(100) end)
        S.spawn(function() task.wait_event("other") test.log(-1) end))").has_value());
    h.Scheduler->Tick(0.1f);
    CHECK(h.Log.empty());
    REQUIRE(h.Run("S.signal(\"go\", 1, 2)").has_value());
    CHECK(h.Log == std::vector<double>{3, 100});
    CHECK(h.Scheduler->Active() == 1);
    h.Scheduler->Signal("go");
    CHECK(h.Log.size() == 2);
}

TEST_CASE("Cancelling drops tasks, individually, by owner or all at once", "[luau][scheduler]")
{
    Harness h;
    REQUIRE(h.Run(R"(
        local a = S.spawn(function() task.wait(1) test.log(1) end)
        S.spawn_owned("owner", function() task.wait(1) test.log(2) end)
        S.spawn(function() task.wait(1) test.log(3) end)
        S.cancel(a)
        S.cancel_owner("owner"))").has_value());
    CHECK(h.Scheduler->Active() == 1);
    h.Tick(4, 0.25f);
    CHECK(h.Log == std::vector<double>{3});
    REQUIRE(h.Run("S.spawn(function() task.wait(1) test.log(9) end) S.spawn(function() task.wait(1) test.log(9) end)").has_value());
    h.Scheduler->CancelAll();
    CHECK(h.Scheduler->Active() == 0);
    h.Tick(8, 0.25f);
    CHECK(h.Log == std::vector<double>{3});
}

TEST_CASE("A failing task is reported and removed without stopping the others", "[luau][scheduler]")
{
    Harness h;
    REQUIRE(h.Run(R"(
        S.spawn(function() task.wait(0.5) local x = nil; return x.y end)
        S.spawn(function() task.wait(1) test.log(1) end)
        S.spawn(function() error("immediate") end))").has_value());
    REQUIRE(h.Errors->Items.size() == 1);
    CHECK(h.Errors->Items[0].Message.find("immediate") != std::string::npos);
    h.Tick(2, 0.5f);
    REQUIRE(h.Errors->Items.size() == 2);
    CHECK(h.Errors->Items[1].Line == 4);
    CHECK(h.Log == std::vector<double>{1});
    CHECK(h.Scheduler->Active() == 0);
    CHECK(h.Errors->Take().size() == 2);
    CHECK(h.Errors->Items.empty());
}

TEST_CASE("Waiting outside a task and runaway tasks are errors, not crashes", "[luau][scheduler][limits]")
{
    Harness h;
    auto outside = h.Run("task.wait(1)");
    REQUIRE_FALSE(outside.has_value());
    CHECK(outside.error().Message.find("inside a task") != std::string::npos);
    auto runaway = h.Run("S.spawn(function() while true do end end)");
    INFO((runaway ? "ok" : runaway.error().Message));
    REQUIRE_FALSE(h.Errors->Items.empty());
    CHECK(h.Errors->Items[0].Message.find("limit") != std::string::npos);
    CHECK(h.Scheduler->Active() == 0);
}

TEST_CASE("The clock only advances on ticks so a paused host keeps tasks suspended", "[luau][scheduler]")
{
    Harness h;
    REQUIRE(h.Run("S.spawn(function() task.wait(1) test.log(1) end)").has_value());
    h.Tick(2, 0.25f);
    CHECK(h.Scheduler->Time() == 0.5);
    CHECK(h.Scheduler->Active() == 1);
    h.Tick(2, 0.25f);
    CHECK(h.Log == std::vector<double>{1});
}

TEST_CASE("Spawn inside a task uses the running scheduler", "[luau][scheduler]")
{
    Harness h;
    REQUIRE(h.Run(R"(S.spawn(function()
        task.spawn(function() task.wait(0.5) test.log(2) end)
        test.log(1)
        task.wait(1)
        test.log(3)
    end))").has_value());
    CHECK(h.Log == std::vector<double>{1});
    h.Tick(4, 0.25f);
    CHECK(h.Log == std::vector<double>{1, 2, 3});
}

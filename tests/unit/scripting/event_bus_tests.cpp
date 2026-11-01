#include <catch2/catch_test_macros.hpp>

import GPP;
import std;

using namespace GPP;

TEST_CASE("The event bus delivers in emit order to subscribers in subscription order", "[scripting][events]")
{
    ScriptEventBus bus;
    std::vector<std::string> seen;
    const auto all = bus.Subscribe({}, [&](const ScriptEvent& event) { seen.push_back("all:" + event.Name); });
    (void)bus.Subscribe("a", [&](const ScriptEvent& event) { seen.push_back("a:" + event.Name); });
    (void)all;

    bus.Emit("a");
    bus.Emit("b");
    bus.Emit("a");
    CHECK(bus.Pending() == 3);
    CHECK(seen.empty());
    CHECK(bus.Dispatch() == 3);
    CHECK(seen == std::vector<std::string>{"all:a", "a:a", "all:b", "all:a", "a:a"});
    CHECK(bus.Pending() == 0);
}

TEST_CASE("Events emitted by a handler queue behind the ones already waiting", "[scripting][events]")
{
    ScriptEventBus bus;
    std::vector<std::string> seen;
    (void)bus.Subscribe({}, [&](const ScriptEvent& event)
    {
        seen.push_back(event.Name);
        if (event.Name == "first") bus.Emit("reply");
    });
    bus.Emit("first");
    bus.Emit("second");
    bus.Dispatch();
    CHECK(seen == std::vector<std::string>{"first", "second", "reply"});
}

TEST_CASE("Event arguments arrive unchanged", "[scripting][events]")
{
    ScriptEventBus bus;
    std::vector<LuauValue> got;
    (void)bus.Subscribe("e", [&](const ScriptEvent& event) { got = event.Args; });
    bus.Emit("e", {1.5, std::string("text"), glm::vec3(1, 2, 3), std::uint64_t{42}, true});
    bus.Dispatch();
    REQUIRE(got.size() == 5);
    CHECK(std::get<double>(got[0]) == 1.5);
    CHECK(std::get<std::string>(got[1]) == "text");
    CHECK(std::get<glm::vec3>(got[2]) == glm::vec3(1, 2, 3));
    CHECK(std::get<std::uint64_t>(got[3]) == 42);
    CHECK(std::get<bool>(got[4]));
}

TEST_CASE("A handler may unsubscribe itself and others while events are delivered", "[scripting][events]")
{
    ScriptEventBus bus;
    int first = 0;
    int second = 0;
    ScriptEventBus::Token secondToken = 0;
    (void)bus.Subscribe("e", [&](const ScriptEvent&)
    {
        ++first;
        bus.Unsubscribe(secondToken);
    });
    secondToken = bus.Subscribe("e", [&](const ScriptEvent&) { ++second; });
    bus.Emit("e");
    bus.Emit("e");
    bus.Dispatch();
    CHECK(first == 2);
    CHECK(second == 0);
}

TEST_CASE("A runaway handler is cut off at the dispatch limit", "[scripting][events]")
{
    ScriptEventBus bus;
    int delivered = 0;
    (void)bus.Subscribe("loop", [&](const ScriptEvent&)
    {
        ++delivered;
        bus.Emit("loop");
    });
    bus.Emit("loop");
    CHECK(bus.Dispatch(50) == 50);
    CHECK(delivered == 50);
    CHECK(bus.Pending() == 0);
    CHECK(bus.TakeOverflow());
    CHECK_FALSE(bus.TakeOverflow());
}

TEST_CASE("A throwing handler leaves the bus usable", "[scripting][events]")
{
    ScriptEventBus bus;
    int calls = 0;
    (void)bus.Subscribe("e", [&](const ScriptEvent&)
    {
        if (++calls == 1) throw std::runtime_error("boom");
    });
    bus.Emit("e");
    CHECK_THROWS_AS(bus.Dispatch(), std::runtime_error);
    bus.Emit("e");
    CHECK(bus.Dispatch() == 1);
    CHECK(calls == 2);
}

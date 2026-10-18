#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

import GPP;
import std;

using namespace GPP;

namespace
{
    struct NoopModule final : ISimulationModule
    {
    };

    std::unique_ptr<SimulationRunner> MakeRunner(double timeScale, double hertz = 60.0)
    {
        Scene scene("Clock");
        scene.SetTimeScale(timeScale);
        SimulationOptions options;
        options.FixedTimestep = std::chrono::duration<float>(static_cast<float>(1.0 / hertz));
        return std::make_unique<SimulationRunner>(std::move(scene), std::make_shared<NoopModule>(), options);
    }
}

TEST_CASE("The clock advances by dt times TimeScale per tick", "[simclock]")
{
    auto runner = MakeRunner(2.5e-3);
    runner->StepAndWait(120);
    const auto clock = runner->AcquireSnapshot()->Clock();
    CHECK(clock.Elapsed == Catch::Approx(2.0).epsilon(1e-12));
    CHECK(clock.Time == Catch::Approx(2.0 * 2.5e-3).epsilon(1e-12));
}

TEST_CASE("TimeScale changes apply from the next tick on", "[simclock]")
{
    auto runner = MakeRunner(1.0);
    runner->StepAndWait(60);
    runner->EnqueueEdit([](Scene& scene) { scene.SetTimeScale(10.0); });
    runner->StepAndWait(60);
    const auto clock = runner->AcquireSnapshot()->Clock();
    CHECK(clock.Elapsed == Catch::Approx(2.0).epsilon(1e-12));
    CHECK(clock.Time == Catch::Approx(11.0).epsilon(1e-12));
}

TEST_CASE("The clock stands still while paused and in the published snapshot", "[simclock]")
{
    auto runner = MakeRunner(1.0);
    runner->Start();
    runner->SetPaused(true);
    runner->EnqueueEdit([](Scene&) {});
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    const auto first = runner->AcquireSnapshot()->Clock();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    const auto second = runner->AcquireSnapshot()->Clock();
    CHECK(first == second);

    runner->SetPaused(false);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    const auto running = runner->AcquireSnapshot()->Clock();
    CHECK(running.Time > second.Time);
    runner->SetPaused(true);
    runner->Stop();
}

TEST_CASE("Manual stepping advances the clock by exactly the requested ticks", "[simclock]")
{
    auto runner = MakeRunner(3.0, 120.0);
    runner->Start();
    runner->SetManualStepping(true);
    runner->StepAndWait(240);
    const auto clock = runner->AcquireSnapshot()->Clock();
    runner->Stop();
    CHECK(clock.Elapsed == Catch::Approx(2.0).epsilon(1e-12));
    CHECK(clock.Time == Catch::Approx(6.0).epsilon(1e-12));
}

TEST_CASE("SetSimTime is published before it returns and the clock continues from it", "[simclock][export]")
{
    auto runner = MakeRunner(1.0);
    runner->Start();
    runner->SetPaused(true);
    runner->SetSimTime(12345.678);
    CHECK(runner->AcquireSnapshot()->Clock().Time == 12345.678);
    runner->StepAndWait(60);
    CHECK(runner->AcquireSnapshot()->Clock().Time == Catch::Approx(12346.678).epsilon(1e-12));
    runner->Stop();

    auto inlineRunner = MakeRunner(1.0);
    inlineRunner->SetSimTime(7.0);
    CHECK(inlineRunner->AcquireSnapshot()->Clock().Time == 7.0);
}

TEST_CASE("Export frames place the clock explicitly and identically on every run", "[simclock][export]")
{
    constexpr double fps = 30.0;
    constexpr double timeScale = 196.0;
    constexpr double start = 3600.0;
    const auto render = []
    {
        auto runner = MakeRunner(timeScale);
        std::vector<double> frames;
        runner->SetSimTime(start);
        for (int frame = 0; frame < 90; ++frame)
        {
            frames.push_back(runner->AcquireSnapshot()->Clock().Time);
            runner->StepAndWait(2);
            runner->SetSimTime(start + (frame + 1) * timeScale / fps);
        }
        return frames;
    };
    const auto a = render();
    const auto b = render();
    CHECK(a == b);
    CHECK(a[0] == start);
    CHECK(a[89] == start + 89 * timeScale / fps);
}

TEST_CASE("Copies and syncs carry the clock, so Stop restores the play-start time", "[simclock][playsession]")
{
    auto runner = MakeRunner(1.0);
    runner->SetSimTime(100.0);
    Scene before = runner->AcquireSnapshot()->Clone();
    CHECK(before.Clock().Time == 100.0);

    runner->StepAndWait(60);
    Scene live = runner->AcquireSnapshot()->Clone();
    CHECK(live.Clock().Time == Catch::Approx(101.0).epsilon(1e-12));
    Scene::SyncInto(before, live);
    CHECK(live.Clock().Time == 100.0);
    CHECK(live.Clock().Elapsed == before.Clock().Elapsed);

    Scene assigned("Other");
    assigned = before;
    CHECK(assigned.Clock() == before.Clock());
}

TEST_CASE("At 1e9 s the clock still resolves single ticks", "[simclock][precision]")
{
    auto runner = MakeRunner(1.0, 1000.0);
    runner->SetSimTime(1.0e9);
    runner->StepAndWait(1000);
    const double elapsed = runner->AcquireSnapshot()->Clock().Time - 1.0e9;
    CHECK(elapsed == Catch::Approx(1.0).margin(1e-6));
}

TEST_CASE("Luau reads the clock and queues time scale changes", "[simclock][luau]")
{
    Scene scene("Script");
    scene.SetTimeScale(4.0);
    scene.SetSimTime(10.0);
    scene.AdvanceClock(0.5);

    auto context = std::make_shared<SceneBindings>();
    context->Read = &scene;
    std::vector<std::function<void(Scene&)>> queued;
    context->Write = [&queued](std::function<void(Scene&)> write) { queued.push_back(std::move(write)); };

    LuauVm vm;
    RegisterSceneBindings(vm, context);
    vm.Seal();
    auto handle = vm.Load(R"(local M = {}
function M.run()
    assert(time.sim_time() == 12)
    assert(time.elapsed() == 0.5)
    assert(time.scale() == 4)
    time.set_scale(0.25)
    assert(not pcall(time.set_scale, -1))
end
return M)", "=time");
    REQUIRE(handle.has_value());
    auto result = vm.Call(*handle, "run");
    INFO((result ? "" : result.error().Message));
    REQUIRE(result.has_value());
    REQUIRE(queued.size() == 1);
    queued[0](scene);
    CHECK(scene.Metadata().TimeScale == 0.25);
}

TEST_CASE("Clone keeps the TimeScale so scenes created through the manager play at their own scale", "[simclock]")
{
    Scene scene("Scaled");
    scene.SetTimeScale(4.9e-5);
    CHECK(scene.Clone().Metadata().TimeScale == 4.9e-5);
}

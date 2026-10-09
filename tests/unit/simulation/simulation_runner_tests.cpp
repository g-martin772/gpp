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

    bool WaitUntil(const std::function<bool()>& predicate,
                   std::chrono::milliseconds timeout = std::chrono::milliseconds(500))
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (predicate()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return predicate();
    }
}

TEST_CASE("EnqueueEdit reaches the render scene while running", "[simulation_runner]")
{
    Scene scene("Test");
    SimulationRunner runner(std::move(scene), std::make_shared<NoopModule>());
    runner.Start();

    std::uint64_t createdGuid = 0;
    runner.EnqueueEdit([&createdGuid](Scene& s)
    {
        const auto entity = s.CreateEntity("New", "Thing");
        createdGuid = s.GuidOf(entity);
    });

    const bool appeared = WaitUntil([&]
    {
        auto lock = runner.LockRenderScene();
        return lock->IsValid(lock->FindByGuid(createdGuid));
    });

    runner.Stop();
    REQUIRE(appeared);
}

TEST_CASE("EnqueueEdit reaches the render scene while paused", "[simulation_runner]")
{
    // Regression test: the sim thread used to skip Scene::SyncInto entirely whenever paused,
    // so edits applied to the sim scene (e.g. an editor adding/removing/editing entities) never
    // reached the render scene until the simulation was resumed.
    Scene scene("Test");
    SimulationRunner runner(std::move(scene), std::make_shared<NoopModule>());
    runner.Start();
    runner.SetPaused(true);

    std::uint64_t createdGuid = 0;
    runner.EnqueueEdit([&createdGuid](Scene& s)
    {
        const auto entity = s.CreateEntity("New", "Thing");
        createdGuid = s.GuidOf(entity);
    });

    const bool appeared = WaitUntil([&]
    {
        auto lock = runner.LockRenderScene();
        return lock->IsValid(lock->FindByGuid(createdGuid));
    });

    runner.Stop();
    REQUIRE(appeared);
}

namespace
{
    struct CountingModule final : ISimulationModule
    {
        std::atomic<int> Ticks{0};
        std::atomic<float> LastDelta{0.0f};
        void OnTick(Scene&, float deltaTime) override
        {
            LastDelta = deltaTime;
            ++Ticks;
        }
    };

    // Blocks inside OnTick until released, to model a simulation that hangs.
    struct HangingModule final : ISimulationModule
    {
        std::atomic<bool> Release{false};
        std::atomic<bool> Entered{false};
        void OnTick(Scene&, float) override
        {
            Entered = true;
            while (!Release.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    };
}

TEST_CASE("Tick rate is configurable at runtime and reflected in the stats", "[simulation_runner][tick_rate]")
{
    auto module = std::make_shared<CountingModule>();
    SimulationRunner runner(Scene("Test"), module);
    CHECK(runner.GetTickRate() == Catch::Approx(60.0));

    runner.SetTickRate(200.0);
    CHECK(runner.GetTickRate() == Catch::Approx(200.0));
    runner.SetTickRate(1e9);
    CHECK(runner.GetTickRate() == Catch::Approx(SimulationRunner::kMaxTickRate));
    runner.SetTickRate(-5.0);
    CHECK(runner.GetTickRate() == Catch::Approx(SimulationRunner::kMinTickRate));
    runner.SetTickRate(100.0);

    runner.Start();
    REQUIRE(WaitUntil([&] { return module->Ticks.load() >= 40; }, std::chrono::milliseconds(2000)));
    runner.Stop();

    CHECK(module->LastDelta.load() == Catch::Approx(0.01f).epsilon(0.01));
    const auto stats = runner.GetStats();
    CHECK(stats.TargetTickRate == Catch::Approx(100.0));
    CHECK(stats.TickCount >= 40);
}

TEST_CASE("The simulation runs at the configured tick rate", "[simulation_runner][tick_rate]")
{
    auto module = std::make_shared<CountingModule>();
    SimulationOptions options;
    options.FixedTimestep = std::chrono::duration<float>(1.0f / 50.0f);
    SimulationRunner runner(Scene("Test"), module, options);
    runner.Start();

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    const int atFifty = module->Ticks.load();

    runner.SetTickRate(200.0);
    const int before = module->Ticks.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    const int atTwoHundred = module->Ticks.load() - before;
    runner.Stop();

    CHECK(atFifty >= 35);
    CHECK(atFifty <= 60);
    CHECK(atTwoHundred >= 140);
    CHECK(atTwoHundred <= 230);
}

TEST_CASE("A hung simulation never blocks readers, edits or stats", "[simulation_runner][freeze]")
{
    auto module = std::make_shared<HangingModule>();
    SimulationRunner runner(Scene("Test"), module);
    runner.Start();
    REQUIRE(WaitUntil([&] { return module->Entered.load(); }));

    // Everything a UI / renderer thread does must return immediately while OnTick is stuck.
    const auto begin = std::chrono::steady_clock::now();
    for (int i = 0; i < 1000; ++i)
    {
        auto snapshot = runner.AcquireSnapshot();
        REQUIRE(static_cast<bool>(snapshot));
        (void)snapshot->Metadata();
        runner.EnqueueEdit([](Scene&) {});
        runner.SetTickRate(30.0 + (i % 50));
        (void)runner.GetStats();
    }
    const auto elapsed = std::chrono::steady_clock::now() - begin;
    CHECK(elapsed < std::chrono::milliseconds(500));

    // ...and the hang is visible in the stats.
    const bool stalled = WaitUntil([&] { return runner.GetStats().CurrentTickMs > 20.0; });
    CHECK(stalled);

    runner.RequestStop();
    module->Release = true;
    runner.Stop();
}

TEST_CASE("A reader holding a snapshot neither stalls the simulation nor sees it change", "[simulation_runner][snapshot]")
{
    auto module = std::make_shared<CountingModule>();
    SimulationRunner runner(Scene("Test"), module);
    runner.Start();

    const auto held = runner.AcquireSnapshot();
    const auto heldGeneration = held.Generation();

    const int before = module->Ticks.load();
    REQUIRE(WaitUntil([&] { return module->Ticks.load() >= before + 20; }, std::chrono::milliseconds(2000)));
    REQUIRE(WaitUntil([&] { return runner.AcquireSnapshot().Generation() > heldGeneration + 3; }));

    CHECK(held.Generation() == heldGeneration); // the held snapshot is immutable
    runner.Stop();
}

TEST_CASE("Snapshots published while running move forward monotonically", "[simulation_runner][snapshot]")
{
    auto module = std::make_shared<CountingModule>();
    SimulationRunner runner(Scene("Test"), module);
    runner.Start();

    std::uint64_t previous = 0;
    for (int i = 0; i < 50; ++i)
    {
        const auto generation = runner.AcquireSnapshot().Generation();
        CHECK(generation >= previous);
        previous = generation;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    runner.Stop();
    CHECK(previous > 1);
}

TEST_CASE("Pausing stops ticks, resuming does not replay the paused time", "[simulation_runner][pause]")
{
    auto module = std::make_shared<CountingModule>();
    SimulationRunner runner(Scene("Test"), module);
    runner.Start();
    REQUIRE(WaitUntil([&] { return module->Ticks.load() >= 5; }));

    runner.SetPaused(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    const int pausedAt = module->Ticks.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    CHECK(module->Ticks.load() == pausedAt);
    CHECK(runner.GetStats().Paused);

    runner.SetPaused(false);
    REQUIRE(WaitUntil([&] { return module->Ticks.load() > pausedAt; }));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    // ~6 ticks in 100 ms at 60 Hz - definitely not the ~18 a "catch up on paused time" bug would produce at once.
    CHECK(module->Ticks.load() - pausedAt < 15);
    runner.Stop();
}

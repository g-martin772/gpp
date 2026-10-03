#include <catch2/catch_test_macros.hpp>

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

#include <catch2/catch_test_macros.hpp>

import GPP;
import std;

using namespace GPP;

namespace
{
    // Drifts one entity by a counter and a draw from the runner's generator; the counter is hidden state.
    struct DriftModule final : ISimulationModule
    {
        std::uint64_t Target{0};
        std::shared_ptr<SimulationRandom> Rng;
        int Ticks{0};

        void OnTick(Scene& scene, float dt) override
        {
            ++Ticks;
            const auto entity = scene.FindByGuid(Target);
            if (!scene.IsValid(entity)) return;
            auto& transform = scene.Registry().get<TransformComponent>(entity);
            transform.Position.x += dt * static_cast<float>(Ticks);
            transform.Position.y += static_cast<float>(Rng->NextDouble());
        }

        [[nodiscard]] std::string SaveState() const override { return std::to_string(Ticks); }
        void LoadState(const std::string& state) override { Ticks = std::stoi(state); }
    };

    struct CountModule final : ISimulationModule
    {
        std::atomic<int> Ticks{0};
        void OnTick(Scene&, float) override { ++Ticks; }
    };

    Scene MakeScene(std::uint64_t& target)
    {
        Scene scene("Replay");
        scene.SetSeed(1234);
        const auto entity = scene.CreateEntity("Mover", "Box");
        scene.Registry().emplace<TransformComponent>(entity);
        target = scene.GuidOf(entity);
        return scene;
    }

    std::unique_ptr<SimulationRunner> MakeRunner(Scene scene, std::shared_ptr<DriftModule> module)
    {
        auto runner = std::make_unique<SimulationRunner>(std::move(scene), module);
        module->Rng = runner->Random();
        return runner;
    }

    bool WaitFor(const std::function<bool()>& predicate)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (predicate()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return predicate();
    }
}

TEST_CASE("Step advances exactly n ticks without a thread and publishes", "[step]")
{
    auto module = std::make_shared<CountModule>();
    SimulationRunner runner(Scene("S"), module);

    runner.StepAndWait(7);
    CHECK(module->Ticks == 7);
    CHECK(runner.Tick() == 7);
    runner.StepAndWait(3);
    CHECK(module->Ticks == 10);
    CHECK(runner.AcquireSnapshot().Generation() >= 2);
}

TEST_CASE("StepAndWait runs the requested ticks on the sim thread even while paused", "[step]")
{
    auto module = std::make_shared<CountModule>();
    SimulationRunner runner(Scene("S"), module);
    runner.Start();
    runner.SetPaused(true);

    runner.StepAndWait(5);
    CHECK(module->Ticks == 5);
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    CHECK(module->Ticks == 5);

    runner.Step(4);
    REQUIRE(WaitFor([&] { return module->Ticks == 9; }));
    runner.Stop();
}

TEST_CASE("Commands are stamped with the tick they applied at and can target a future tick", "[step][commands]")
{
    std::uint64_t guid = 0;
    SimulationRunner runner(MakeScene(guid), std::make_shared<CountModule>());

    runner.StepAndWait(3);
    runner.EnqueueCommand(SetFieldCommand{guid, "Transform", "Position", glm::vec3(1, 0, 0)});
    CommandOptions later;
    later.TargetTick = 6;
    runner.EnqueueCommand(SetFieldCommand{guid, "Transform", "Position", glm::vec3(2, 0, 0)}, later);

    runner.StepAndWait(2);
    auto log = runner.CommandLog();
    REQUIRE(log.size() == 1);
    CHECK(log[0].Tick == 3);

    runner.StepAndWait(3);
    log = runner.CommandLog();
    REQUIRE(log.size() == 2);
    CHECK(log[1].Tick == 6);
}

TEST_CASE("The command log is bounded", "[step][commands]")
{
    std::uint64_t guid = 0;
    SimulationOptions options;
    options.CommandLogCapacity = 4;
    SimulationRunner runner(MakeScene(guid), std::make_shared<CountModule>(), options);
    for (int i = 1; i <= 10; ++i)
    {
        runner.EnqueueCommand(SetFieldCommand{guid, "Transform", "Position", glm::vec3(static_cast<float>(i), 0, 0)});
    }
    runner.StepAndWait(1);
    CHECK(runner.CommandLog().size() == 4);
}

TEST_CASE("Replaying a recording twice yields identical scene state", "[step][replay]")
{
    std::uint64_t guid = 0;
    auto sourceModule = std::make_shared<DriftModule>();
    auto source = MakeRunner(MakeScene(guid), sourceModule);
    sourceModule->Target = guid;

    source->StepAndWait(5);
    source->BeginRecording();
    source->StepAndWait(3);
    source->EnqueueCommand(SetFieldCommand{guid, "Transform", "Scale", glm::vec3(2, 2, 2)});
    source->StepAndWait(4);
    source->EnqueueCommands({SpawnEntityCommand{999, "Components:\n  Transform:\n    Position: [1, 1, 1]"},
                             SetParentCommand{999, guid}});
    source->StepAndWait(6);
    auto recording = source->EndRecording();
    const auto expected = source->AcquireSnapshot()->SerializeToYaml();

    REQUIRE(recording.Ticks() == 13);
    REQUIRE(recording.Commands.size() == 3);

    std::string results[2];
    for (auto& result : results)
    {
        auto module = std::make_shared<DriftModule>();
        module->Target = guid;
        auto replay = MakeRunner(Scene("Empty"), module);
        replay->PrepareReplay(recording);
        replay->StepAndWait(recording.Ticks());
        CHECK(replay->Tick() == recording.EndTick);
        result = replay->AcquireSnapshot()->SerializeToYaml();
    }
    CHECK(results[0] == results[1]);
    CHECK(results[0] == expected);
}

TEST_CASE("Runner state round-trips module state and the random stream", "[step][state]")
{
    std::uint64_t guid = 0;
    auto module = std::make_shared<DriftModule>();
    auto runner = MakeRunner(MakeScene(guid), module);
    module->Target = guid;

    runner->StepAndWait(4);
    const auto state = runner->CaptureState();
    runner->StepAndWait(4);
    const auto afterFirst = runner->AcquireSnapshot()->SerializeToYaml();

    runner->RestoreState(state);
    CHECK(module->Ticks == 4);
    const auto drawn = runner->Random()->NextU64();
    runner->RestoreState(state);
    CHECK(runner->Random()->NextU64() == drawn);
    CHECK_FALSE(afterFirst.empty());
}

TEST_CASE("Scene seed survives clone and YAML", "[step][seed]")
{
    Scene scene("S");
    scene.SetSeed(42);
    CHECK(scene.Clone().Metadata().Seed == 42);
    Scene loaded;
    loaded.DeserializeFromYaml(scene.SerializeToYaml());
    CHECK(loaded.Metadata().Seed == 42);
}

TEST_CASE("Manual stepping stops the realtime loop but still honours Step", "[step][manual]")
{
    auto module = std::make_shared<CountModule>();
    SimulationRunner runner(Scene("S"), module);
    runner.Start();
    REQUIRE(WaitFor([&] { return module->Ticks > 2; }));

    runner.SetManualStepping(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    const int frozen = module->Ticks;
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    CHECK(module->Ticks == frozen);
    CHECK_FALSE(runner.IsPaused());

    runner.StepAndWait(6);
    CHECK(module->Ticks == frozen + 6);

    runner.SetManualStepping(false);
    REQUIRE(WaitFor([&] { return module->Ticks > frozen + 10; }));
    runner.Stop();
}

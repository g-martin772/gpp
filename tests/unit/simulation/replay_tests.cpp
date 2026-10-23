#include <catch2/catch_test_macros.hpp>

import GPP;
import std;

using namespace GPP;

namespace
{
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

    Scene MakeScene(std::uint64_t& target)
    {
        Scene scene("Replay");
        scene.SetSeed(1234);
        const auto entity = scene.CreateEntity("Mover", "Box");
        scene.Registry().emplace<TransformComponent>(entity);
        target = scene.GuidOf(entity);
        return scene;
    }

    std::filesystem::path TempFile(const std::string& name)
    {
        return std::filesystem::temp_directory_path() / ("gpp_replay_test_" + name);
    }

    std::shared_ptr<DriftModule> MakeDrift(std::uint64_t target)
    {
        auto module = std::make_shared<DriftModule>();
        module->Target = target;
        return module;
    }

    ReplayVerification Verify(const SimulationRecording& recording, std::uint64_t target)
    {
        auto module = MakeDrift(target);
        ReplayOptions options;
        options.OnRunnerCreated = [module](SimulationRunner& runner) { module->Rng = runner.Random(); };
        return VerifyReplay(recording, {module}, options);
    }

    // 40 ticks recorded from tick 3, checkpoint every 5 ticks, with commands at ticks 11 and 22.
    SimulationRecording Record(std::uint64_t& target)
    {
        auto module = std::make_shared<DriftModule>();
        SimulationRunner runner(MakeScene(target), module);
        module->Rng = runner.Random();
        module->Target = target;
        runner.StepAndWait(3);
        runner.BeginRecording(5);
        runner.StepAndWait(8);
        runner.EnqueueCommand(SetFieldCommand{target, "Transform", "Scale", glm::vec3(2, 2, 2)});
        runner.StepAndWait(11);
        runner.EnqueueCommands({SpawnEntityCommand{999, "Components:\n  Transform:\n    Position: [1, 1, 1]"},
                                SetParentCommand{999, target}});
        runner.StepAndWait(18);
        return runner.EndRecording();
    }
}

TEST_CASE("Scene hash ignores entity creation order and scene identity", "[replay][hash]")
{
    const auto make = [](bool reversed, const std::string& name)
    {
        Scene scene(name);
        scene.SetSeed(7);
        std::array<std::uint64_t, 3> guids{11, 22, 33};
        if (reversed) std::ranges::reverse(guids);
        for (const auto guid : guids)
        {
            const auto entity = scene.CreateEntityWithGuid(guid, "E" + std::to_string(guid), "Box");
            scene.Registry().emplace<TransformComponent>(entity).Position.x = static_cast<float>(guid);
        }
        return scene;
    };
    CHECK(HashScene(make(false, "A")) == HashScene(make(true, "B")));

    auto changed = make(false, "A");
    auto& position = changed.Registry().get<TransformComponent>(changed.FindByGuid(22)).Position;
    position.x = std::nextafter(position.x, 1000.0f);
    CHECK(HashScene(changed) != HashScene(make(false, "A")));

    auto clocked = make(false, "A");
    clocked.AdvanceClock(0.25);
    CHECK(HashScene(clocked) != HashScene(make(false, "A")));
}

TEST_CASE("A recording survives the disk and verifies repeatedly", "[replay]")
{
    std::uint64_t target = 0;
    const auto recording = Record(target);
    REQUIRE(recording.Ticks() == 37);
    REQUIRE(recording.Commands.size() == 3);
    REQUIRE(recording.Checkpoints.size() == 8);
    CHECK(recording.Checkpoints.front().Tick == 3);
    CHECK(recording.FinalHash.has_value());

    const auto first = Verify(recording, target);
    INFO(first.Message);
    CHECK(first.Ok);
    CHECK(first.CheckpointsChecked == 9);
    CHECK(Verify(recording, target).Ok);

    const auto path = TempFile("roundtrip.yaml");
    std::string error;
    REQUIRE(SaveRecording(recording, path, &error));
    const auto loaded = LoadRecording(path, &error);
    std::filesystem::remove(path);
    INFO(error);
    REQUIRE(loaded.has_value());
    CHECK(loaded->Checkpoints.size() == recording.Checkpoints.size());
    CHECK(loaded->Commands.size() == recording.Commands.size());
    CHECK(loaded->StartTick == 3);
    CHECK(loaded->State.RngState == recording.State.RngState);
    CHECK(loaded->State.Modules == recording.State.Modules);
    CHECK(HashScene(loaded->Initial) == HashScene(recording.Initial));
    CHECK(RecordingToYaml(*loaded) == RecordingToYaml(recording));

    const auto fromDisk = Verify(*loaded, target);
    INFO(fromDisk.Message);
    CHECK(fromDisk.Ok);
}

TEST_CASE("A perturbed command fails verification at the expected checkpoint", "[replay]")
{
    std::uint64_t target = 0;
    auto recording = Record(target);
    REQUIRE(recording.Commands[0].Tick == 11);
    std::get<SetFieldCommand>(recording.Commands[0].Applied).Value = glm::vec3(2, 2, 2.5f);

    const auto result = Verify(recording, target);
    CHECK_FALSE(result.Ok);
    REQUIRE(result.DivergedTick.has_value());
    CHECK(*result.DivergedTick == 13);
    CHECK(result.LastGoodTick == 8);
    CHECK(result.Expected != result.Actual);
}

TEST_CASE("A perturbed seed fails verification at the start", "[replay]")
{
    std::uint64_t target = 0;
    auto recording = Record(target);
    recording.State.RngState ^= 1;
    const auto result = Verify(recording, target);
    CHECK_FALSE(result.Ok);
    REQUIRE(result.DivergedTick.has_value());
    CHECK(*result.DivergedTick == recording.StartTick);

    auto tampered = Record(target);
    tampered.Checkpoints.back().Hash ^= 1;
    const auto late = Verify(tampered, target);
    REQUIRE(late.DivergedTick.has_value());
    CHECK(*late.DivergedTick == tampered.Checkpoints.back().Tick);
}

TEST_CASE("Recording files reject wrong versions and missing hashes", "[replay]")
{
    std::uint64_t target = 0;
    auto recording = Record(target);
    auto yaml = RecordingToYaml(recording);
    const auto at = yaml.find("Version: 1");
    REQUIRE(at != std::string::npos);
    yaml.replace(at, 10, "Version: 99");
    CHECK_THROWS_AS(RecordingFromYaml(yaml), std::runtime_error);
    CHECK_THROWS_AS(RecordingFromYaml("Format: Other"), std::runtime_error);
    CHECK_FALSE(LoadRecording(TempFile("missing.yaml")).has_value());

    recording.Checkpoints.clear();
    recording.FinalHash.reset();
    CHECK_FALSE(Verify(recording, target).Ok);
}

namespace
{
void PhysicsReplay(const std::uint64_t warmupTicks)
{
    const auto build = [](std::uint64_t& ball)
    {
        Scene scene("Physics");
        const auto floor = scene.CreateEntity("Floor");
        scene.Registry().emplace<TransformComponent>(floor, TransformComponent{.Position = {0.0f, -2.0f, 0.0f}});
        scene.Registry().emplace<RigidBodyComponent>(floor, RigidBodyComponent{.Type = RigidBodyType::Static});
        scene.Registry().emplace<ColliderComponent>(
            floor, ColliderComponent{.Shape = ColliderShape::Box, .HalfExtents = {10.0f, 0.5f, 10.0f}});
        const auto entity = scene.CreateEntity("Ball");
        scene.Registry().emplace<TransformComponent>(entity, TransformComponent{.Position = {0.0f, 3.0f, 0.0f}});
        scene.Registry().emplace<VelocityComponent>(entity, VelocityComponent{.Linear = {0.5f, 0.0f, 0.0f}});
        scene.Registry().emplace<RigidBodyComponent>(entity, RigidBodyComponent{.Type = RigidBodyType::Dynamic, .Mass = 1.0f});
        scene.Registry().emplace<ColliderComponent>(entity, ColliderComponent{.Shape = ColliderShape::Sphere, .Radius = 0.5f});
        ball = scene.GuidOf(entity);
        return scene;
    };
    const auto modules = [] {
        return std::vector<std::shared_ptr<ISimulationModule>>{
            std::make_shared<PhysicsSimulationModule>(std::make_shared<EventDispatcher>(), std::make_shared<Logger>())};
    };

    std::uint64_t ball = 0;
    SimulationRecording recording;
    {
        SimulationRunner source(build(ball), modules());
        if (warmupTicks > 0) source.StepAndWait(warmupTicks);
        source.BeginRecording(10);
        source.StepAndWait(20);
        source.EnqueueCommand(SetFieldCommand{ball, "Velocity", "Linear", glm::dvec3(-1, 4, 0.5)});
        source.StepAndWait(100);
        recording = source.EndRecording();
    }
    REQUIRE(recording.Checkpoints.size() >= 12);

    for (int run = 0; run < 2; ++run)
    {
        const auto result = VerifyReplay(recording, modules());
        INFO(result.Message);
        CHECK(result.Ok);
    }

    const auto path = TempFile("physics.yaml");
    REQUIRE(SaveRecording(recording, path));
    const auto loaded = LoadRecording(path);
    std::filesystem::remove(path);
    REQUIRE(loaded.has_value());
    const auto result = VerifyReplay(*loaded, modules());
    INFO(result.Message);
    CHECK(result.Ok);
}
}

TEST_CASE("A physics scene replays identically", "[replay][physics]")
{
    PhysicsReplay(0);
}

// PhysicsSimulationModule has no SaveState/LoadState: PhysX internals are not part of a recording, so a replay started from
// a scene that has already been stepped diverges within the first checkpoint interval.
TEST_CASE("A physics recording started mid-simulation replays identically", "[replay][physics][!shouldfail]")
{
    PhysicsReplay(1);
}

#include <catch2/catch_test_macros.hpp>

import GPP;
import std;

using namespace GPP;

namespace
{
    enum class Mode { Idle, Gravity, Joint, Pushed };

    struct Driver final : ISimulationModule
    {
        std::shared_ptr<PhysicsSimulationModule> Physics;
        Mode Kind = Mode::Idle;
        std::uint64_t Body = 0;
        std::uint64_t Other = 0;
        int Ticks = 0;
        bool Joined = false;

        void OnTick(Scene&, float) override
        {
            ++Ticks;
            if (Kind == Mode::Gravity)
                Physics->QueueCommand({PhysicsCommandKind::Force, Body, glm::dvec3(0.0, -9.81, 0.0)});
            if (Kind == Mode::Pushed)
            {
                const double phase = static_cast<double>(Ticks % 17) - 8.0;
                Physics->QueueCommand({PhysicsCommandKind::Force, Body, glm::dvec3(phase, -9.81, 0.5 * phase)});
                if (Ticks % 23 == 0)
                    Physics->QueueCommand({PhysicsCommandKind::Torque, Body, glm::dvec3(0.1, 0.2, 0.3)});
            }
            if (Kind == Mode::Joint)
            {
                if (!Joined)
                {
                    Joined = true;
                    (void)Physics->CreateJoint(Body, Other, PhysicsJointKind::Distance,
                                               PhysicsJointParams{.Distance = 1.5, .Stiffness = 40.0, .Damping = 1.0});
                }
                Physics->QueueCommand({PhysicsCommandKind::Force, Body, glm::dvec3(0.0, -9.81, 0.0)});
            }
        }

        [[nodiscard]] std::string SaveState() const override
        {
            return std::format("{} {}", Ticks, Joined ? 1 : 0);
        }

        void LoadState(const std::string& state) override
        {
            int joined = 0;
            std::istringstream in(state);
            in >> Ticks >> joined;
            Joined = joined != 0;
        }
    };

    struct Setup
    {
        Mode Kind = Mode::Idle;
        std::function<void(Scene&, std::uint64_t&, std::uint64_t&)> Build;
    };

    void AddBody(Scene& scene, const char* name, const glm::vec3& position, const RigidBodyType type,
                 const glm::vec3& velocity, std::uint64_t& guid, const bool withVelocity = true,
                 const glm::vec3& angular = {})
    {
        const auto entity = scene.CreateEntity(name);
        scene.Registry().emplace<TransformComponent>(entity, TransformComponent{.Position = position});
        if (withVelocity) scene.Registry().emplace<VelocityComponent>(entity, VelocityComponent{.Linear = velocity, .Angular = angular});
        scene.Registry().emplace<RigidBodyComponent>(entity, RigidBodyComponent{.Type = type, .Mass = 1.0f});
        scene.Registry().emplace<ColliderComponent>(entity, ColliderComponent{.Shape = ColliderShape::Sphere, .Radius = 0.5f});
        guid = scene.GuidOf(entity);
    }

    void AddFloor(Scene& scene, const float y)
    {
        const auto floor = scene.CreateEntity("Floor");
        scene.Registry().emplace<TransformComponent>(floor, TransformComponent{.Position = {0.0f, y, 0.0f}});
        scene.Registry().emplace<RigidBodyComponent>(floor, RigidBodyComponent{.Type = RigidBodyType::Static});
        scene.Registry().emplace<ColliderComponent>(
            floor, ColliderComponent{.Shape = ColliderShape::Box, .HalfExtents = {20.0f, 0.5f, 20.0f}});
    }

    void CheckRestore(const Setup& setup, const std::uint64_t warmup)
    {
        std::uint64_t body = 0;
        std::uint64_t other = 0;
        const auto modules = [&setup, &body, &other]
        {
            auto physics = std::make_shared<PhysicsSimulationModule>(std::make_shared<EventDispatcher>(),
                                                                     std::make_shared<Logger>());
            auto driver = std::make_shared<Driver>();
            driver->Physics = physics;
            driver->Kind = setup.Kind;
            driver->Body = body;
            driver->Other = other;
            return std::vector<std::shared_ptr<ISimulationModule>>{driver, physics};
        };

        Scene scene("Restore");
        setup.Build(scene, body, other);

        SimulationRecording recording;
        {
            SimulationRunner source(std::move(scene), modules());
            if (warmup > 0) source.StepAndWait(warmup);
            source.BeginRecording(1);
            source.StepAndWait(60);
            recording = source.EndRecording();
        }
        REQUIRE(recording.Checkpoints.size() >= 60);

        for (int run = 0; run < 2; ++run)
        {
            const auto result = VerifyReplay(recording, modules());
            INFO(result.Message);
            CHECK(result.Ok);
        }
    }
}

TEST_CASE("Physics restore is bit-identical for a static body", "[physics][restore]")
{
    Setup setup{Mode::Idle, [](Scene& scene, std::uint64_t&, std::uint64_t&) { AddFloor(scene, -3.0f); }};
    CheckRestore(setup, 0);
    CheckRestore(setup, 40);
}

TEST_CASE("Physics restore is bit-identical for a free dynamic body", "[physics][restore]")
{
    Setup setup{Mode::Idle, [](Scene& scene, std::uint64_t& body, std::uint64_t&)
                {
                    AddBody(scene, "Ball", {0.3f, 5.1f, -2.7f}, RigidBodyType::Dynamic, {0.4f, 1.0f, 0.2f}, body);
                }};
    CheckRestore(setup, 0);
    CheckRestore(setup, 40);
}

TEST_CASE("Physics restore is bit-identical for a dynamic body without velocity component", "[physics][restore]")
{
    Setup setup{Mode::Idle, [](Scene& scene, std::uint64_t& body, std::uint64_t&)
                {
                    AddBody(scene, "Ball", {120.3f, 5.1f, -2.7f}, RigidBodyType::Dynamic, {0.0f, 0.0f, 0.0f}, body, false);
                }};
    CheckRestore(setup, 40);
}

TEST_CASE("Physics restore is bit-identical for a falling body", "[physics][restore]")
{
    Setup setup{Mode::Gravity, [](Scene& scene, std::uint64_t& body, std::uint64_t&)
                {
                    AddFloor(scene, -2.0f);
                    AddBody(scene, "Ball", {0.0f, 6.0f, 0.0f}, RigidBodyType::Dynamic, {0.3f, 0.0f, 0.1f}, body);
                }};
    CheckRestore(setup, 0);
    CheckRestore(setup, 20);
    CheckRestore(setup, 40);
    CheckRestore(setup, 120);
}

TEST_CASE("Physics restore is bit-identical for a resting contact", "[physics][restore]")
{
    Setup setup{Mode::Gravity, [](Scene& scene, std::uint64_t& body, std::uint64_t&)
                {
                    AddFloor(scene, -2.0f);
                    AddBody(scene, "Ball", {1.0f, -1.0f, 0.5f}, RigidBodyType::Dynamic, {0.0f, 0.0f, 0.0f}, body);
                }};
    CheckRestore(setup, 0);
    CheckRestore(setup, 40);
    CheckRestore(setup, 300);
}

TEST_CASE("Physics restore is bit-identical with a joint", "[physics][restore]")
{
    Setup setup{Mode::Joint, [](Scene& scene, std::uint64_t& body, std::uint64_t& other)
                {
                    AddBody(scene, "Anchor", {0.0f, 4.0f, 0.0f}, RigidBodyType::Static, {}, other);
                    AddBody(scene, "Bob", {1.0f, 4.0f, 0.0f}, RigidBodyType::Dynamic, {0.0f, 0.0f, 0.5f}, body);
                }};
    CheckRestore(setup, 0);
    CheckRestore(setup, 40);
}

TEST_CASE("Physics restore is bit-identical under scripted forces", "[physics][restore]")
{
    Setup setup{Mode::Pushed, [](Scene& scene, std::uint64_t& body, std::uint64_t&)
                {
                    AddFloor(scene, -1.0f);
                    AddBody(scene, "Ball", {5000.0f, 2.0f, 3.0f}, RigidBodyType::Dynamic, {0.0f, 0.0f, 0.0f}, body);
                }};
    CheckRestore(setup, 0);
    CheckRestore(setup, 40);
}

TEST_CASE("Physics restore is bit-identical for a tumbling body", "[physics][restore]")
{
    Setup setup{Mode::Gravity, [](Scene& scene, std::uint64_t& body, std::uint64_t&)
                {
                    AddFloor(scene, -2.0f);
                    AddBody(scene, "Ball", {0.0f, 6.0f, 0.0f}, RigidBodyType::Dynamic, {0.3f, 0.0f, 0.2f}, body, true,
                            {1.3f, -2.1f, 0.7f});
                }};
    for (const std::uint64_t warmup : {1, 5, 30, 77, 90, 150, 400}) CheckRestore(setup, warmup);
}

TEST_CASE("Physics restore is bit-identical for a sliding body", "[physics][restore]")
{
    Setup setup{Mode::Pushed, [](Scene& scene, std::uint64_t& body, std::uint64_t&)
                {
                    AddFloor(scene, -2.0f);
                    AddBody(scene, "Ball", {0.0f, -1.0f, 0.0f}, RigidBodyType::Dynamic, {0.3f, 0.0f, 0.0f}, body);
                }};
    for (const std::uint64_t warmup : {10, 60, 200}) CheckRestore(setup, warmup);
}

TEST_CASE("Physics restore is bit-identical for several bodies", "[physics][restore]")
{
    Setup setup{Mode::Gravity, [](Scene& scene, std::uint64_t& body, std::uint64_t&)
                {
                    AddFloor(scene, -2.0f);
                    std::uint64_t extra = 0;
                    AddBody(scene, "A", {0.0f, 0.0f, 0.0f}, RigidBodyType::Dynamic, {0.2f, 0.0f, 0.0f}, body);
                    AddBody(scene, "B", {0.6f, 1.6f, 0.0f}, RigidBodyType::Dynamic, {0.0f, 0.0f, 0.1f}, extra);
                    AddBody(scene, "C", {-0.5f, 3.0f, 0.2f}, RigidBodyType::Dynamic, {0.0f, 0.0f, 0.0f}, extra, true,
                            {0.5f, 0.1f, 0.0f});
                }};
    for (const std::uint64_t warmup : {1, 30, 60, 100, 200}) CheckRestore(setup, warmup);
}

TEST_CASE("Physics bookmarks restore in place bit-identically", "[physics][restore]")
{
    for (const std::uint64_t warmup : {1, 40, 90, 200})
    {
        std::uint64_t body = 0;
        Scene scene("Bookmark");
        AddFloor(scene, -2.0f);
        AddBody(scene, "A", {0.0f, 6.0f, 0.0f}, RigidBodyType::Dynamic, {0.3f, 0.0f, 0.1f}, body, true, {1.0f, 2.0f, 0.5f});
        std::uint64_t second = 0;
        AddBody(scene, "B", {0.4f, 8.0f, 0.0f}, RigidBodyType::Dynamic, {0.0f, 0.0f, 0.0f}, second);

        auto physics = std::make_shared<PhysicsSimulationModule>(std::make_shared<EventDispatcher>(), std::make_shared<Logger>());
        auto driver = std::make_shared<Driver>();
        driver->Physics = physics;
        driver->Kind = Mode::Pushed;
        driver->Body = body;
        SimulationRunner runner(std::move(scene), std::vector<std::shared_ptr<ISimulationModule>>{driver, physics});

        runner.StepAndWait(warmup);
        std::optional<Scene> snapshot;
        RunnerState hidden;
        runner.EnqueueSystemEdit([&](Scene& live)
        {
            snapshot = live.Clone();
            hidden = runner.CaptureState();
        }, false);
        runner.StepAndWait(60);
        const auto expected = runner.StateHash();
        const auto expectedYaml = runner.AcquireSnapshot()->SerializeToYaml();

        runner.EnqueueSystemEdit([&](Scene& live)
        {
            Scene::SyncInto(*snapshot, live);
            runner.RestoreState(hidden);
        }, false);
        runner.StepAndWait(60);
        INFO("warmup " << warmup);
        CHECK(runner.AcquireSnapshot()->SerializeToYaml() == expectedYaml);
        CHECK(runner.StateHash() == expected);
    }
}

TEST_CASE("Physics loads the legacy joint-only state", "[physics][restore]")
{
    Scene scene("Legacy");
    std::uint64_t anchor = 0;
    std::uint64_t bob = 0;
    AddBody(scene, "Anchor", {0.0f, 4.0f, 0.0f}, RigidBodyType::Static, {}, anchor);
    AddBody(scene, "Bob", {1.0f, 4.0f, 0.0f}, RigidBodyType::Dynamic, {}, bob);
    PhysicsSimulationModule physics(nullptr, std::make_shared<Logger>());
    physics.OnInit(scene);
    const auto id = physics.CreateJoint(anchor, bob, PhysicsJointKind::Fixed, {});
    REQUIRE(id.has_value());
    const auto legacy = physics.HashState();
    REQUIRE_FALSE(legacy.empty());
    physics.OnShutdown(scene);

    Scene again("Legacy");
    again.CreateEntityWithGuid(anchor, "Anchor");
    PhysicsSimulationModule second(nullptr, std::make_shared<Logger>());
    second.OnInit(again);
    second.LoadState(legacy);
    CHECK(second.HashState() == legacy);
    second.OnShutdown(again);
}

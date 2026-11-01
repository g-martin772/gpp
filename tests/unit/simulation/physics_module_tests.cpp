#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

import GPP;
import std;

using namespace GPP;

TEST_CASE ("PhysicsSimulationModule steps a free dynamic body by its velocity", "[simulation][physics]")
{
    auto dispatcher = std::make_shared<EventDispatcher>();
    auto logger = std::make_shared<Logger>();
    PhysicsSimulationModule physics(dispatcher, logger);

    Scene scene("PhysicsTest");
    const auto body = scene.CreateEntity("Ball");
    scene.Registry().emplace<TransformComponent>(body, TransformComponent{.Position = {0.0f, 0.0f, 0.0f}});
    scene.Registry().emplace<VelocityComponent>(body, VelocityComponent{.Linear = {1.0f, 0.0f, 0.0f}});
    scene.Registry().emplace<RigidBodyComponent>(
        body, RigidBodyComponent{.Type = RigidBodyType::Dynamic, .Mass = 1.0f});
    scene.Registry().emplace<ColliderComponent>(
        body, ColliderComponent{.Shape = ColliderShape::Sphere, .Radius = 0.5f});

    physics.OnInit(scene);

    constexpr float dt = 1.0f / 60.0f;
    constexpr int steps = 30;
    for (int i = 0; i < steps; ++i)
    {
        physics.OnTick(scene, dt);
    }

    const auto& transform = scene.Registry().get<TransformComponent>(body);
    CHECK(transform.Position.x == Catch::Approx(1.0f * dt * steps).margin(0.01f));
    CHECK(transform.Position.y == Catch::Approx(0.0f).margin(0.001f));

    const auto& velocity = scene.Registry().get<VelocityComponent>(body);
    CHECK(velocity.Linear.x == Catch::Approx(1.0f).margin(0.01f));

    physics.OnShutdown(scene);
}

TEST_CASE ("PhysicsSimulationModule leaves static bodies in place", "[simulation][physics]")
{
    auto dispatcher = std::make_shared<EventDispatcher>();
    auto logger = std::make_shared<Logger>();
    PhysicsSimulationModule physics(dispatcher, logger);

    Scene scene("StaticTest");
    const auto floor = scene.CreateEntity("Floor");
    scene.Registry().emplace<TransformComponent>(floor, TransformComponent{.Position = {0.0f, -5.0f, 0.0f}});
    scene.Registry().emplace<RigidBodyComponent>(floor, RigidBodyComponent{.Type = RigidBodyType::Static});
    scene.Registry().emplace<ColliderComponent>(
        floor, ColliderComponent{.Shape = ColliderShape::Box, .HalfExtents = {10.0f, 0.5f, 10.0f}});

    physics.OnInit(scene);
    for (int i = 0; i < 10; ++i)
    {
        physics.OnTick(scene, 1.0f / 60.0f);
    }

    CHECK(scene.Registry().get<TransformComponent>(floor).Position == glm::dvec3(0.0f, -5.0f, 0.0f));
    physics.OnShutdown(scene);
}

TEST_CASE ("PhysicsSimulationModule cooks a ConvexMesh collider from its point cloud", "[simulation][physics]")
{
    auto dispatcher = std::make_shared<EventDispatcher>();
    auto logger = std::make_shared<Logger>();
    PhysicsSimulationModule physics(dispatcher, logger);

    Scene scene("ConvexMeshTest");
    const auto entity = scene.CreateEntity("Hull");
    scene.Registry().emplace<TransformComponent>(entity, TransformComponent{.Position = {0.0f, 0.0f, 0.0f}});
    scene.Registry().emplace<VelocityComponent>(entity, VelocityComponent{.Linear = {1.0f, 0.0f, 0.0f}});
    scene.Registry().emplace<RigidBodyComponent>(
        entity, RigidBodyComponent{.Type = RigidBodyType::Dynamic, .Mass = 1.0f});
    scene.Registry().emplace<ColliderComponent>(entity, ColliderComponent{
        .Shape = ColliderShape::ConvexMesh,
        .ConvexHullPoints = {
            {-0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, -0.5f}, {-0.5f, 0.5f, -0.5f}, {0.5f, 0.5f, -0.5f},
            {-0.5f, -0.5f, 0.5f}, {0.5f, -0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, 0.5f},
        }
    });

    physics.OnInit(scene);

    auto* actor = physics.FindActor(entity);
    REQUIRE(actor != nullptr);
    CHECK(actor->getNbShapes() == 1);

    constexpr float dt = 1.0f / 60.0f;
    constexpr int steps = 30;
    for (int i = 0; i < steps; ++i)
    {
        physics.OnTick(scene, dt);
    }

    const auto& transform = scene.Registry().get<TransformComponent>(entity);
    CHECK(transform.Position.x == Catch::Approx(1.0f * dt * steps).margin(0.01f));

    physics.OnShutdown(scene);
}

TEST_CASE ("PhysicsSimulationModule skips a ConvexMesh collider with too few points", "[simulation][physics]")
{
    auto dispatcher = std::make_shared<EventDispatcher>();
    auto logger = std::make_shared<Logger>();
    PhysicsSimulationModule physics(dispatcher, logger);

    Scene scene("ConvexMeshDegenerateTest");
    const auto entity = scene.CreateEntity("Sliver");
    scene.Registry().emplace<TransformComponent>(entity);
    scene.Registry().emplace<RigidBodyComponent>(entity, RigidBodyComponent{.Type = RigidBodyType::Dynamic});
    scene.Registry().emplace<ColliderComponent>(entity, ColliderComponent{
        .Shape = ColliderShape::ConvexMesh,
        .ConvexHullPoints = {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}}
    });

    CHECK_NOTHROW(physics.OnInit(scene));
    CHECK(physics.FindActor(entity) == nullptr);

    physics.OnShutdown(scene);
}

TEST_CASE ("PhysicsSimulationModule removes the actor when its collider is removed", "[simulation][physics]")
{
    auto dispatcher = std::make_shared<EventDispatcher>();
    auto logger = std::make_shared<Logger>();
    PhysicsSimulationModule physics(dispatcher, logger);

    Scene scene("RemovalTest");
    const auto entity = scene.CreateEntity("Box");
    scene.Registry().emplace<TransformComponent>(entity);
    scene.Registry().emplace<RigidBodyComponent>(entity);
    scene.Registry().emplace<ColliderComponent>(entity);

    physics.OnInit(scene);
    physics.OnTick(scene, 1.0f / 60.0f);

    scene.Registry().remove<ColliderComponent>(entity);

    // Must not crash: the module should notice the entity no longer qualifies and release its actor.
    CHECK_NOTHROW(physics.OnTick(scene, 1.0f / 60.0f));

    physics.OnShutdown(scene);
}

TEST_CASE ("PhysicsSimulationModule honors external transform and velocity writes on dynamic bodies",
           "[simulation][physics][authority]")
{
    auto dispatcher = std::make_shared<EventDispatcher>();
    auto logger = std::make_shared<Logger>();
    PhysicsSimulationModule physics(dispatcher, logger);

    Scene scene("AuthorityTest");
    const auto body = scene.CreateEntity("Ball");
    scene.Registry().emplace<TransformComponent>(body, TransformComponent{.Position = {0.0f, 0.0f, 0.0f}});
    scene.Registry().emplace<VelocityComponent>(body);
    scene.Registry().emplace<RigidBodyComponent>(
        body, RigidBodyComponent{.Type = RigidBodyType::Dynamic, .Mass = 1.0f});
    scene.Registry().emplace<ColliderComponent>(
        body, ColliderComponent{.Shape = ColliderShape::Sphere, .Radius = 0.5f});

    physics.OnInit(scene);
    constexpr float dt = 1.0f / 60.0f;
    for (int i = 0; i < 5; ++i) physics.OnTick(scene, dt);
    CHECK(scene.Registry().get<TransformComponent>(body).Position.x == Catch::Approx(0.0f).margin(0.001f));

    scene.Registry().get<TransformComponent>(body).Position = {10.0f, 2.0f, 0.0f};
    physics.OnTick(scene, dt);
    CHECK(scene.Registry().get<TransformComponent>(body).Position.x == Catch::Approx(10.0f).margin(0.01f));
    CHECK(scene.Registry().get<TransformComponent>(body).Position.y == Catch::Approx(2.0f).margin(0.01f));

    scene.Registry().get<VelocityComponent>(body).Linear = {60.0f, 0.0f, 0.0f};
    physics.OnTick(scene, dt);
    CHECK(scene.Registry().get<TransformComponent>(body).Position.x == Catch::Approx(11.0f).margin(0.05f));

    physics.OnShutdown(scene);
}

TEST_CASE ("PhysicsSimulationModule zeroes motion when a body without velocity data is teleported",
           "[simulation][physics][authority]")
{
    auto dispatcher = std::make_shared<EventDispatcher>();
    auto logger = std::make_shared<Logger>();
    PhysicsSimulationModule physics(dispatcher, logger);

    Scene scene("TeleportTest");
    const auto body = scene.CreateEntity("Ball");
    scene.Registry().emplace<TransformComponent>(body);
    scene.Registry().emplace<RigidBodyComponent>(
        body, RigidBodyComponent{.Type = RigidBodyType::Dynamic, .Mass = 1.0f, .EnableGravity = false});
    scene.Registry().emplace<ColliderComponent>(
        body, ColliderComponent{.Shape = ColliderShape::Sphere, .Radius = 0.5f});

    physics.OnInit(scene);
    physics.OnTick(scene, 1.0f / 60.0f);
    auto* dynamic = physics.FindActor(body)->is<physx::PxRigidDynamic>();
    REQUIRE(dynamic != nullptr);
    dynamic->setLinearVelocity(physx::PxVec3(5.0f, 0.0f, 0.0f));

    scene.Registry().get<TransformComponent>(body).Position = {3.0f, 0.0f, 0.0f};
    physics.OnTick(scene, 1.0f / 60.0f);
    CHECK(scene.Registry().get<TransformComponent>(body).Position.x == Catch::Approx(3.0f).margin(0.01f));
    physics.OnShutdown(scene);
}

TEST_CASE ("PhysicsSimulationModule instances can overlap in one process", "[simulation][physics]")
{
    auto dispatcher = std::make_shared<EventDispatcher>();
    auto logger = std::make_shared<Logger>();
    PhysicsSimulationModule first(dispatcher, logger);
    PhysicsSimulationModule second(dispatcher, logger);
    Scene firstScene("First");
    Scene secondScene("Second");

    first.OnInit(firstScene);
    CHECK_NOTHROW(second.OnInit(secondScene));
    first.OnShutdown(firstScene);
    CHECK_NOTHROW(second.OnTick(secondScene, 1.0f / 60.0f));
    second.OnShutdown(secondScene);

    PhysicsSimulationModule third(dispatcher, logger);
    CHECK_NOTHROW(third.OnInit(firstScene));
    third.OnShutdown(firstScene);
}

namespace
{
    auto AddBall(Scene& scene, const std::uint64_t guid, const glm::dvec3& position, const glm::dvec3& velocity, const int material = 0)
    {
        const auto entity = scene.CreateEntityWithGuid(guid, "Ball");
        scene.Registry().emplace<TransformComponent>(entity, TransformComponent{.Position = position});
        scene.Registry().emplace<VelocityComponent>(entity, VelocityComponent{.Linear = velocity});
        scene.Registry().emplace<RigidBodyComponent>(entity, RigidBodyComponent{.Type = RigidBodyType::Dynamic, .Mass = 1.0, .EnableGravity = false});
        scene.Registry().emplace<ColliderComponent>(
            entity, ColliderComponent{.Shape = ColliderShape::Sphere, .Radius = 0.5, .MaterialId = material});
        return entity;
    }

    PhysicsEventBatch RunUntilEvent(PhysicsSimulationModule& physics, Scene& scene, const int maxTicks)
    {
        for (int i = 0; i < maxTicks; ++i)
        {
            physics.OnTick(scene, 1.0f / 60.0f);
            auto batch = physics.TakeEvents();
            if (!batch.Contacts.empty() || !batch.Triggers.empty()) return batch;
        }
        return {};
    }
}

TEST_CASE ("PhysicsSimulationModule reports contact point, normal, impulse, velocity and materials",
           "[simulation][physics][events]")
{
    PhysicsSimulationModule physics(std::make_shared<EventDispatcher>(), std::make_shared<Logger>());
    physics.SetEventCapture(true);
    Scene scene("ContactTest");
    AddBall(scene, 20, {0.0, 3.0, 0.0}, {0.0, -4.0, 0.0}, 7);
    AddBall(scene, 10, {0.0, 0.0, 0.0}, {0.0, -1.0, 0.0}, 9);
    physics.OnInit(scene);

    const auto batch = RunUntilEvent(physics, scene, 120);
    REQUIRE(batch.Contacts.size() == 1);
    const auto& contact = batch.Contacts.front();
    CHECK(contact.GuidA == 10);
    CHECK(contact.GuidB == 20);
    CHECK(contact.MaterialA == 9);
    CHECK(contact.MaterialB == 7);
    CHECK(contact.Normal.y == Catch::Approx(-1.0).margin(0.05));
    CHECK(contact.RelativeVelocity.y == Catch::Approx(3.0).margin(0.05));
    CHECK(contact.Impulse > 1.0);
    CHECK(contact.Impulse < 2.5);
    CHECK(contact.Point.y == Catch::Approx(-1.0 / 6.0).margin(0.1));
    CHECK(physics.TakeEvents().Contacts.empty());
    physics.OnShutdown(scene);
}

TEST_CASE ("PhysicsSimulationModule sorts same-tick contacts by guid pair", "[simulation][physics][events]")
{
    PhysicsSimulationModule physics(std::make_shared<EventDispatcher>(), std::make_shared<Logger>());
    physics.SetEventCapture(true);
    Scene scene("SortTest");
    AddBall(scene, 90, {10.0, 3.0, 0.0}, {0.0, -4.0, 0.0});
    AddBall(scene, 80, {10.0, 0.0, 0.0}, {0.0, -1.0, 0.0});
    AddBall(scene, 30, {0.0, 3.0, 0.0}, {0.0, -4.0, 0.0});
    AddBall(scene, 40, {0.0, 0.0, 0.0}, {0.0, -1.0, 0.0});
    physics.OnInit(scene);

    const auto batch = RunUntilEvent(physics, scene, 120);
    REQUIRE(batch.Contacts.size() == 2);
    CHECK(batch.Contacts[0].GuidA == 30);
    CHECK(batch.Contacts[0].GuidB == 40);
    CHECK(batch.Contacts[1].GuidA == 80);
    CHECK(batch.Contacts[1].GuidB == 90);
    physics.OnShutdown(scene);
}

TEST_CASE ("PhysicsSimulationModule reports a body entering and leaving a trigger", "[simulation][physics][events]")
{
    PhysicsSimulationModule physics(std::make_shared<EventDispatcher>(), std::make_shared<Logger>());
    physics.SetEventCapture(true);
    Scene scene("TriggerTest");
    const auto zone = scene.CreateEntityWithGuid(5, "Zone");
    scene.Registry().emplace<TransformComponent>(zone);
    scene.Registry().emplace<RigidBodyComponent>(zone, RigidBodyComponent{.Type = RigidBodyType::Static});
    scene.Registry().emplace<ColliderComponent>(
        zone, ColliderComponent{.Shape = ColliderShape::Box, .HalfExtents = {1.0, 1.0, 1.0}, .IsTrigger = true});
    const auto body = AddBall(scene, 6, {-3.0, 0.0, 0.0}, {2.0, 0.0, 0.0});
    scene.Registry().get<ColliderComponent>(body).Radius = 0.25;
    physics.OnInit(scene);

    std::vector<PhysicsTriggerEvent> seen;
    for (int i = 0; i < 180; ++i)
    {
        physics.OnTick(scene, 1.0f / 60.0f);
        for (const auto& event : physics.TakeEvents().Triggers) seen.push_back(event);
    }
    REQUIRE(seen.size() == 2);
    CHECK(seen[0].TriggerGuid == 5);
    CHECK(seen[0].OtherGuid == 6);
    CHECK(seen[0].Entered);
    CHECK_FALSE(seen[1].Entered);
    CHECK(scene.Registry().get<TransformComponent>(body).Position.x == Catch::Approx(3.0).margin(0.05));
    physics.OnShutdown(scene);
}

TEST_CASE ("PhysicsSimulationModule keeps no events unless capture is on", "[simulation][physics][events]")
{
    PhysicsSimulationModule physics(std::make_shared<EventDispatcher>(), std::make_shared<Logger>());
    Scene scene("NoCaptureTest");
    AddBall(scene, 2, {0.0, 3.0, 0.0}, {0.0, -4.0, 0.0});
    AddBall(scene, 1, {0.0, 0.0, 0.0}, {0.0, -1.0, 0.0});
    physics.OnInit(scene);
    for (int i = 0; i < 120; ++i) physics.OnTick(scene, 1.0f / 60.0f);
    const auto batch = physics.TakeEvents();
    CHECK(batch.Contacts.empty());
    CHECK(batch.Triggers.empty());
    physics.OnShutdown(scene);
}

namespace
{
    Scene MakeJointScene()
    {
        Scene scene("Joints");
        const auto anchor = scene.CreateEntityWithGuid(1, "Anchor");
        scene.Registry().emplace<TransformComponent>(anchor, TransformComponent{.Position = {0.0, 0.0, 0.0}});
        scene.Registry().emplace<RigidBodyComponent>(anchor, RigidBodyComponent{.Type = RigidBodyType::Static});
        scene.Registry().emplace<ColliderComponent>(anchor, ColliderComponent{.Shape = ColliderShape::Sphere, .Radius = 0.5});
        const auto body = scene.CreateEntityWithGuid(2, "Body");
        scene.Registry().emplace<TransformComponent>(body, TransformComponent{.Position = {0.0, 3.0, 0.0}});
        scene.Registry().emplace<VelocityComponent>(body);
        scene.Registry().emplace<RigidBodyComponent>(body, RigidBodyComponent{.Type = RigidBodyType::Dynamic, .Mass = 1.0});
        scene.Registry().emplace<ColliderComponent>(body, ColliderComponent{.Shape = ColliderShape::Sphere, .Radius = 0.5});
        return scene;
    }
}

TEST_CASE ("PhysicsSimulationModule restores saved joints after a state load", "[simulation][physics][joints]")
{
    auto logger = std::make_shared<Logger>();
    Scene scene = MakeJointScene();
    constexpr std::uint64_t anchor = 1;
    constexpr std::uint64_t body = 2;

    PhysicsSimulationModule first(nullptr, logger);
    first.OnInit(scene);
    const auto id = first.CreateJoint(anchor, body, PhysicsJointKind::Fixed, {});
    REQUIRE(id.has_value());
    CHECK(first.SaveState().empty() == false);
    const auto saved = first.SaveState();
    first.OnShutdown(scene);

    Scene again = MakeJointScene();
    PhysicsSimulationModule second(nullptr, logger);
    second.OnInit(again);
    second.LoadState(saved);
    second.QueueCommand({.Kind = PhysicsCommandKind::Force, .Guid = body, .Vector = {500.0, 0.0, 0.0}});
    for (int i = 0; i < 30; ++i) second.OnTick(again, 1.0f / 60.0f);
    CHECK(second.JointBroken(*id) == std::optional<bool>(false));
    CHECK(again.Registry().get<TransformComponent>(again.FindByGuid(body)).Position.x == Catch::Approx(0.0).margin(0.05));
    CHECK(second.SaveState() == saved);
    second.OnShutdown(again);
}

TEST_CASE ("PhysicsSimulationModule queries need an initialised scene", "[simulation][physics]")
{
    PhysicsSimulationModule physics(nullptr, std::make_shared<Logger>());
    CHECK_FALSE(physics.Raycast({0.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, 10.0, 0xFFFFFFFFu).has_value());
    CHECK(physics.Overlap({0.0, 0.0, 0.0}, 1.0, 0xFFFFFFFFu).empty());
    CHECK_FALSE(physics.CreateJoint(1, 2, PhysicsJointKind::Fixed, {}).has_value());
}

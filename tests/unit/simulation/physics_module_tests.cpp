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

    CHECK(scene.Registry().get<TransformComponent>(floor).Position == glm::vec3(0.0f, -5.0f, 0.0f));
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

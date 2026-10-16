#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

import GPP;
import std;
import glm;

using namespace GPP;
using Catch::Approx;

namespace
{
    constexpr const char* kLegacyScene = R"(
Scene: Legacy
Entities:
  - Guid: 11
    Components:
      Metadata: {Guid: 11, Name: Ship, TypeTag: Mesh}
      Transform: {Position: [1, 2, 3], Rotation: [0, 0, 0, 1], Scale: [1, 1, 1]}
      Velocity: {Linear: [2, 0, 0], Angular: [0, 1, 0]}
      RigidBody: {Type: Dynamic, Mass: 5.0e+24}
      Collider: {Shape: Sphere, Radius: 0.5}
      Mesh: {AssetPath: ship.glb}
      Camera: {Fov: 60, NearPlane: 0.1, FarPlane: 1000}
)";
}

TEST_CASE("Legacy scenes migrate to SI on load", "[scene][units][migration]")
{
    Scene scene;
    scene.DeserializeFromYaml(kLegacyScene);
    const auto entity = scene.FindByGuid(11);
    REQUIRE(scene.IsValid(entity));
    const auto& registry = scene.Registry();
    const double length = Units::Legacy::kLengthM;

    CHECK(scene.Metadata().LoadedSchemaVersion == 1);
    CHECK(scene.Metadata().TimeScale == Approx(Units::Legacy::kTimeS));
    CHECK(Units::Legacy::kTimeS == Approx(4.9119e-5).epsilon(1e-4));

    const auto& transform = registry.get<TransformComponent>(entity);
    CHECK(transform.Position.x == Approx(1.0 * length));
    CHECK(transform.Position.z == Approx(3.0 * length));
    CHECK(transform.Scale == glm::vec3(1.0f));

    const auto& velocity = registry.get<VelocityComponent>(entity);
    CHECK(velocity.Linear.x == Approx(2.0 * length / Units::Legacy::kTimeS));
    CHECK(velocity.Angular.y == Approx(1.0 / Units::Legacy::kTimeS));

    CHECK(registry.get<RigidBodyComponent>(entity).Mass == 5.0e24);
    CHECK(registry.get<ColliderComponent>(entity).Radius == Approx(0.5 * length));
    CHECK(registry.get<MeshComponent>(entity).MetresPerUnit == Approx(length));
    CHECK(registry.get<CameraComponent>(entity).FarPlane == Approx(1000.0 * length));
}

TEST_CASE("Migrated scenes are written in the current schema and reload unchanged", "[scene][units][migration]")
{
    Scene legacy;
    legacy.DeserializeFromYaml(kLegacyScene);
    const auto yaml = legacy.SerializeToYaml();
    CHECK(yaml.find("SchemaVersion: 2") != std::string::npos);

    Scene reloaded;
    reloaded.DeserializeFromYaml(yaml);
    CHECK(reloaded.Metadata().LoadedSchemaVersion == kSceneSchemaVersion);
    CHECK(reloaded.Metadata().TimeScale == legacy.Metadata().TimeScale);
    CHECK(reloaded.SerializeToYaml() == yaml);
    const auto entity = reloaded.FindByGuid(11);
    CHECK(reloaded.Registry().get<TransformComponent>(entity).Position
          == legacy.Registry().get<TransformComponent>(legacy.FindByGuid(11)).Position);
}

TEST_CASE("Positions keep full double precision through YAML", "[scene][units]")
{
    Scene scene;
    const auto entity = scene.CreateEntity("Far", "Empty");
    const glm::dvec3 position(1.495978707e11 + 0.001, -2.0e13 + 1e-3, 3.0e-9);
    scene.Registry().emplace<TransformComponent>(entity, TransformComponent{.Position = position});
    scene.Registry().emplace<RigidBodyComponent>(entity, RigidBodyComponent{.Mass = 1.98841e30 * 1.0000000001});

    Scene reloaded;
    reloaded.DeserializeFromYaml(scene.SerializeToYaml());
    const auto copy = reloaded.FindByGuid(scene.GuidOf(entity));
    CHECK(reloaded.Registry().get<TransformComponent>(copy).Position == position);
    CHECK(reloaded.Registry().get<RigidBodyComponent>(copy).Mass == 1.98841e30 * 1.0000000001);
}

TEST_CASE("Time scale round trips and defaults to real time", "[scene][units]")
{
    Scene scene;
    CHECK(scene.Metadata().TimeScale == 1.0);
    scene.SetTimeScale(2.5e-5);
    Scene reloaded;
    reloaded.DeserializeFromYaml(scene.SerializeToYaml());
    CHECK(reloaded.Metadata().TimeScale == 2.5e-5);
}

TEST_CASE("Double fields accept narrower values and report SI doubles", "[scene][units][reflection]")
{
    Scene scene;
    const auto entity = scene.CreateEntityWithGuid(5, "A", "Empty");
    scene.Registry().emplace<TransformComponent>(entity);
    scene.Registry().emplace<RigidBodyComponent>(entity);
    REQUIRE(ApplyCommand(scene, SetFieldCommand{5, "Transform", "Position", glm::vec3(1.0f, 2.0f, 3.0f)}));
    CHECK(scene.Registry().get<TransformComponent>(entity).Position == glm::dvec3(1.0, 2.0, 3.0));
    REQUIRE(ApplyCommand(scene, SetFieldCommand{5, "RigidBody", "Mass", 1.5e30}));
    CHECK(scene.Registry().get<RigidBodyComponent>(entity).Mass == 1.5e30);

    const auto* info = ComponentRegistry::Instance().FindByName("RigidBody");
    REQUIRE(info != nullptr);
    const auto field = std::ranges::find(info->Fields, "Mass", &FieldInfo::Name);
    REQUIRE(field != info->Fields.end());
    CHECK(field->Type == FieldType::Double);
    CHECK(field->Meta.Dim == Units::Dimension::Mass);
}

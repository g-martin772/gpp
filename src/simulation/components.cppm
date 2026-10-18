module;
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <yaml-cpp/yaml.h>
export module GPP.Simulation:Components;

import std;
import GPP.Core;
import :Ecs;
import :Reflection;
import :ComponentRegistry;
import :LightComponents;
import :Particles.Components;

export namespace GPP
{
    struct MetadataComponent
    {
        std::uint64_t Guid{0};
        std::string Name;
        std::string TypeTag;
    };

    struct TransformComponent
    {
        glm::dvec3 Position{0.0, 0.0, 0.0}; // m, world frame
        glm::quat Rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 Scale{1.0f, 1.0f, 1.0f}; // dimensionless multiplier

        [[nodiscard]] glm::dmat4 GetMatrix() const
        {
            return glm::translate(glm::dmat4(1.0), Position)
                 * glm::dmat4(glm::mat4_cast(Rotation))
                 * glm::scale(glm::dmat4(1.0), glm::dvec3(Scale));
        }

        // Boundary SI -> float render space: translation is (Position - originM) / lengthUnitM and the model-space
        // scale (Scale * modelUnitM metres per model unit) is divided by lengthUnitM
        [[nodiscard]] glm::mat4 GetRenderMatrix(const glm::dvec3& originM, const double lengthUnitM,
                                                const double modelUnitM = 1.0) const
        {
            const glm::vec3 translation(glm::dvec3((Position - originM) / lengthUnitM));
            const glm::vec3 scale = Scale * static_cast<float>(modelUnitM / lengthUnitM);
            return glm::translate(glm::mat4(1.0f), translation) * glm::mat4_cast(Rotation)
                 * glm::scale(glm::mat4(1.0f), scale);
        }
    };

    struct HierarchyComponent
    {
        std::uint64_t ParentGuid{0};
    };

    struct VelocityComponent
    {
        glm::dvec3 Linear{0.0, 0.0, 0.0};  // m/s
        glm::dvec3 Angular{0.0, 0.0, 0.0}; // rad/s
    };

    enum class RigidBodyType : $u8 { Static, Kinematic, Dynamic };

    struct RigidBodyComponent
    {
        RigidBodyType Type{RigidBodyType::Dynamic};
        double Mass{1.0}; // kg
        float LinearDamping{0.0f};
        float AngularDamping{0.05f};
        bool EnableGravity{true};
    };

    enum class ColliderShape : $u8 { Box, Sphere, Capsule, Plane, ConvexMesh };

    struct ColliderComponent
    {
        ColliderShape Shape{ColliderShape::Box};
        glm::dvec3 HalfExtents{0.5, 0.5, 0.5}; // m
        double Radius{0.5};                    // m
        double HalfHeight{0.5};                // m
        float StaticFriction{0.5f};
        float DynamicFriction{0.5f};
        float Restitution{0.1f};
        bool IsTrigger{false};
        std::vector<glm::vec3> ConvexHullPoints;
    };

    struct PhysicsActorHandle
    {
        void* Actor{nullptr};
    };

    struct MeshComponent
    {
        std::string AssetPath;
        glm::dvec3 CenterOfMassOffset{0.0, 0.0, 0.0}; // m
        double MetresPerUnit{1.0};
    };

    struct CameraComponent
    {
        float Fov{60.0f};
        double NearPlane{0.1};   // m
        double FarPlane{1000.0}; // m
        bool Primary{true};
    };
}

namespace GPP
{
    export void RegisterBaseComponents()
    {
        static std::once_flag flag;
        std::call_once(flag, []
        {
            RegisterComponent<MetadataComponent>("Metadata", ComponentDescription<MetadataComponent>{
                .Inspectable = false,
                .GraphExposed = false,
                .Fields = {
                    Field("Guid", &MetadataComponent::Guid, {.ReadOnly = true}),
                    Field("Name", &MetadataComponent::Name),
                    Field("TypeTag", &MetadataComponent::TypeTag, {.Label = "Type Tag"}),
                }});

            RegisterComponent<TransformComponent>("Transform", ComponentDescription<TransformComponent>{
                .Fields = {
                    Field("Position", &TransformComponent::Position,
                          {.Speed = 1.0, .Dim = Units::Dimension::Length, .LegacyScale = Units::Legacy::kLengthM}),
                    Field("Rotation", &TransformComponent::Rotation, {.Speed = 0.5f, .Kind = FieldKind::Angle}),
                    Field("Scale", &TransformComponent::Scale, {.Speed = 0.01f}),
                }});

            RegisterComponent<HierarchyComponent>("Hierarchy", ComponentDescription<HierarchyComponent>{
                .Inspectable = false,
                .GraphExposed = false,
                .Fields = {
                    Field("ParentGuid", &HierarchyComponent::ParentGuid,
                          {.Label = "Parent", .Kind = FieldKind::EntityRef}),
                }});

            RegisterComponent<VelocityComponent>("Velocity", ComponentDescription<VelocityComponent>{
                .Fields = {
                    Field("Linear", &VelocityComponent::Linear,
                          {.ReadOnly = true, .Dim = Units::Dimension::Velocity,
                           .LegacyScale = Units::Legacy::kLengthM / Units::Legacy::kTimeS}),
                    Field("Angular", &VelocityComponent::Angular,
                          {.ReadOnly = true, .Dim = Units::Dimension::AngularRate,
                           .LegacyScale = 1.0 / Units::Legacy::kTimeS}),
                }});

            RegisterComponent<RigidBodyComponent>("RigidBody", ComponentDescription<RigidBodyComponent>{
                .DisplayName = "Rigid Body",
                .Fields = {
                    Field("Type", &RigidBodyComponent::Type, {.Options = {"Static", "Kinematic", "Dynamic"}}),
                    Field("Mass", &RigidBodyComponent::Mass,
                          {.Label = "Mass", .Min = 0.0, .Speed = 1.0, .Dim = Units::Dimension::Mass}),
                    Field("LinearDamping", &RigidBodyComponent::LinearDamping,
                          {.Label = "Linear Damping", .Min = 0.0f, .Max = 10.0f, .Speed = 0.01f}),
                    Field("AngularDamping", &RigidBodyComponent::AngularDamping,
                          {.Label = "Angular Damping", .Min = 0.0f, .Max = 10.0f, .Speed = 0.01f}),
                    Field("EnableGravity", &RigidBodyComponent::EnableGravity, {.Label = "Enable Gravity"}),
                }});

            RegisterComponent<ColliderComponent>("Collider", ComponentDescription<ColliderComponent>{
                .Note = "Shape/size changes apply on next scene reload",
                .Fields = {
                    Field("Shape", &ColliderComponent::Shape,
                          {.Options = {"Box", "Sphere", "Capsule", "Plane", "ConvexMesh"}}),
                    Field("HalfExtents", &ColliderComponent::HalfExtents,
                          {.Label = "Half Extents", .Min = 1e-3, .Speed = 0.1, .VisibleField = "Shape",
                           .VisibleMask = 0b00001, .Dim = Units::Dimension::Length,
                           .LegacyScale = Units::Legacy::kLengthM}),
                    Field("Radius", &ColliderComponent::Radius,
                          {.Min = 1e-3, .Speed = 0.1, .VisibleField = "Shape", .VisibleMask = 0b00110,
                           .Dim = Units::Dimension::Length, .LegacyScale = Units::Legacy::kLengthM}),
                    Field("HalfHeight", &ColliderComponent::HalfHeight,
                          {.Label = "Half Height", .Min = 1e-3, .Speed = 0.1, .VisibleField = "Shape",
                           .VisibleMask = 0b00100, .Dim = Units::Dimension::Length,
                           .LegacyScale = Units::Legacy::kLengthM}),
                    Field("StaticFriction", &ColliderComponent::StaticFriction,
                          {.Label = "Static Friction", .Min = 0.0f, .Max = 10.0f, .Speed = 0.01f}),
                    Field("DynamicFriction", &ColliderComponent::DynamicFriction,
                          {.Label = "Dynamic Friction", .Min = 0.0f, .Max = 10.0f, .Speed = 0.01f}),
                    Field("Restitution", &ColliderComponent::Restitution,
                          {.Min = 0.0f, .Max = 1.0f, .Speed = 0.01f}),
                    Field("IsTrigger", &ColliderComponent::IsTrigger, {.Label = "Is Trigger"}),
                    Computed<ColliderComponent, int>(
                        "HullPoints", [](const ColliderComponent& c) { return static_cast<int>(c.ConvexHullPoints.size()); },
                        nullptr, {.Label = "Hull Points", .VisibleField = "Shape", .VisibleMask = 0b10000}),
                }});

            RegisterComponent<MeshComponent>("Mesh", ComponentDescription<MeshComponent>{
                .Fields = {
                    Field("AssetPath", &MeshComponent::AssetPath, {.Label = "Asset Path", .Kind = FieldKind::AssetPath}),
                    Field("CenterOfMassOffset", &MeshComponent::CenterOfMassOffset,
                          {.Label = "Center Of Mass Offset", .Speed = 0.01, .Dim = Units::Dimension::Length,
                           .LegacyScale = Units::Legacy::kLengthM}),
                    Field("MetresPerUnit", &MeshComponent::MetresPerUnit,
                          {.Label = "Metres Per Model Unit", .Min = 1e-9, .Speed = 0.01,
                           .LegacyDefault = Units::Legacy::kLengthM}),
                }});

            RegisterComponent<CameraComponent>("Camera", ComponentDescription<CameraComponent>{
                .Fields = {
                    Field("Fov", &CameraComponent::Fov, {.Label = "FOV (deg)", .Min = 1.0, .Max = 179.0, .Speed = 0.1}),
                    Field("NearPlane", &CameraComponent::NearPlane,
                          {.Label = "Near Plane", .Min = 1e-6, .Speed = 1.0, .Dim = Units::Dimension::Length,
                           .LegacyScale = Units::Legacy::kLengthM}),
                    Field("FarPlane", &CameraComponent::FarPlane,
                          {.Label = "Far Plane", .Min = 1e-3, .Speed = 1.0, .Dim = Units::Dimension::Length,
                           .LegacyScale = Units::Legacy::kLengthM}),
                    Field("Primary", &CameraComponent::Primary),
                }});

            RegisterLightComponents();
            RegisterParticleComponents();

            ComponentRegistrationOptions transientOptions;
            transientOptions.SyncToRenderState = false;
            transientOptions.Serializable = false;
            RegisterComponent<PhysicsActorHandle>("__PhysicsActorHandle", transientOptions);
        });
    }
}

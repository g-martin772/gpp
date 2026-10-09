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
        glm::vec3 Position{0.0f, 0.0f, 0.0f};
        glm::quat Rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 Scale{1.0f, 1.0f, 1.0f};

        [[nodiscard]] glm::mat4 GetMatrix() const
        {
            return glm::translate(glm::mat4(1.0f), Position)
                 * glm::mat4_cast(Rotation)
                 * glm::scale(glm::mat4(1.0f), Scale);
        }
    };

    struct HierarchyComponent
    {
        std::uint64_t ParentGuid{0};
    };

    struct VelocityComponent
    {
        glm::vec3 Linear{0.0f, 0.0f, 0.0f};
        glm::vec3 Angular{0.0f, 0.0f, 0.0f};
    };

    enum class RigidBodyType : $u8 { Static, Kinematic, Dynamic };

    struct RigidBodyComponent
    {
        RigidBodyType Type{RigidBodyType::Dynamic};
        float Mass{1.0f};
        float LinearDamping{0.0f};
        float AngularDamping{0.05f};
        bool EnableGravity{true};
    };

    enum class ColliderShape : $u8 { Box, Sphere, Capsule, Plane, ConvexMesh };

    struct ColliderComponent
    {
        ColliderShape Shape{ColliderShape::Box};
        glm::vec3 HalfExtents{0.5f, 0.5f, 0.5f};
        float Radius{0.5f};
        float HalfHeight{0.5f};
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
        glm::vec3 CenterOfMassOffset{0.0f, 0.0f, 0.0f};
    };

    struct CameraComponent
    {
        float Fov{60.0f};
        float NearPlane{0.1f};
        float FarPlane{1000.0f};
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
                    Field("Position", &TransformComponent::Position, {.Speed = 0.1f}),
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
                    Field("Linear", &VelocityComponent::Linear, {.ReadOnly = true}),
                    Field("Angular", &VelocityComponent::Angular, {.ReadOnly = true}),
                }});

            RegisterComponent<RigidBodyComponent>("RigidBody", ComponentDescription<RigidBodyComponent>{
                .DisplayName = "Rigid Body",
                .Fields = {
                    Field("Type", &RigidBodyComponent::Type, {.Options = {"Static", "Kinematic", "Dynamic"}}),
                    Field("Mass", &RigidBodyComponent::Mass,
                          {.Label = "Mass (kg)", .Speed = 1.0e22f, .Format = "%.3e"}),
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
                          {.Label = "Half Extents", .Min = 0.01f, .Max = 1000.0f, .Speed = 0.01f,
                           .VisibleField = "Shape", .VisibleMask = 0b00001}),
                    Field("Radius", &ColliderComponent::Radius,
                          {.Min = 0.01f, .Max = 1000.0f, .Speed = 0.01f,
                           .VisibleField = "Shape", .VisibleMask = 0b00110}),
                    Field("HalfHeight", &ColliderComponent::HalfHeight,
                          {.Label = "Half Height", .Min = 0.01f, .Max = 1000.0f, .Speed = 0.01f,
                           .VisibleField = "Shape", .VisibleMask = 0b00100}),
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
                          {.Label = "Center Of Mass Offset", .Speed = 0.01f}),
                }});

            RegisterComponent<CameraComponent>("Camera", ComponentDescription<CameraComponent>{
                .Fields = {
                    Field("Fov", &CameraComponent::Fov, {.Label = "FOV", .Min = 1.0f, .Max = 179.0f, .Speed = 0.1f}),
                    Field("NearPlane", &CameraComponent::NearPlane,
                          {.Label = "Near Plane", .Min = 0.001f, .Max = 1000.0f, .Speed = 0.01f}),
                    Field("FarPlane", &CameraComponent::FarPlane,
                          {.Label = "Far Plane", .Min = 1.0f, .Max = 1.0e6f, .Speed = 1.0f}),
                    Field("Primary", &CameraComponent::Primary),
                }});

            ComponentRegistrationOptions transientOptions;
            transientOptions.SyncToRenderState = false;
            transientOptions.Serializable = false;
            RegisterComponent<PhysicsActorHandle>("__PhysicsActorHandle", transientOptions);
        });
    }
}

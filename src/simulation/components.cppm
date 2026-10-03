module;
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <yaml-cpp/yaml.h>
export module GPP.Simulation:Components;

import std;
import GPP.Core;
import :Ecs;
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

namespace YAML
{
    template <>
    struct convert<glm::vec3>
    {
        static Node encode(const glm::vec3& v)
        {
            Node node;
            node.push_back(v.x);
            node.push_back(v.y);
            node.push_back(v.z);
            node.SetStyle(EmitterStyle::Flow);
            return node;
        }

        static bool decode(const Node& node, glm::vec3& out)
        {
            if (!node.IsSequence() || node.size() != 3) return false;
            out.x = node[0].as<float>();
            out.y = node[1].as<float>();
            out.z = node[2].as<float>();
            return true;
        }
    };

    template <>
    struct convert<glm::quat>
    {
        static Node encode(const glm::quat& q)
        {
            Node node;
            node.push_back(q.x);
            node.push_back(q.y);
            node.push_back(q.z);
            node.push_back(q.w);
            node.SetStyle(EmitterStyle::Flow);
            return node;
        }

        static bool decode(const Node& node, glm::quat& out)
        {
            if (!node.IsSequence() || node.size() != 4) return false;
            out.x = node[0].as<float>();
            out.y = node[1].as<float>();
            out.z = node[2].as<float>();
            out.w = node[3].as<float>();
            return true;
        }
    };

    template <>
    struct convert<GPP::RigidBodyType>
    {
        static Node encode(const GPP::RigidBodyType& value)
        {
            switch (value)
            {
            case GPP::RigidBodyType::Static: return Node(std::string("Static"));
            case GPP::RigidBodyType::Kinematic: return Node(std::string("Kinematic"));
            case GPP::RigidBodyType::Dynamic: default: return Node(std::string("Dynamic"));
            }
        }

        static bool decode(const Node& node, GPP::RigidBodyType& out)
        {
            const auto value = node.as<std::string>();
            if (value == "Static") out = GPP::RigidBodyType::Static;
            else if (value == "Kinematic") out = GPP::RigidBodyType::Kinematic;
            else out = GPP::RigidBodyType::Dynamic;
            return true;
        }
    };

    template <>
    struct convert<GPP::ColliderShape>
    {
        static Node encode(const GPP::ColliderShape& value)
        {
            switch (value)
            {
            case GPP::ColliderShape::Sphere: return Node(std::string("Sphere"));
            case GPP::ColliderShape::Capsule: return Node(std::string("Capsule"));
            case GPP::ColliderShape::Plane: return Node(std::string("Plane"));
            case GPP::ColliderShape::ConvexMesh: return Node(std::string("ConvexMesh"));
            case GPP::ColliderShape::Box: default: return Node(std::string("Box"));
            }
        }

        static bool decode(const Node& node, GPP::ColliderShape& out)
        {
            const auto value = node.as<std::string>();
            if (value == "Sphere") out = GPP::ColliderShape::Sphere;
            else if (value == "Capsule") out = GPP::ColliderShape::Capsule;
            else if (value == "Plane") out = GPP::ColliderShape::Plane;
            else if (value == "ConvexMesh") out = GPP::ColliderShape::ConvexMesh;
            else out = GPP::ColliderShape::Box;
            return true;
        }
    };

    template <>
    struct convert<GPP::MetadataComponent>
    {
        static Node encode(const GPP::MetadataComponent& value)
        {
            Node node;
            node["Guid"] = value.Guid;
            node["Name"] = value.Name;
            node["TypeTag"] = value.TypeTag;
            return node;
        }

        static bool decode(const Node& node, GPP::MetadataComponent& out)
        {
            out.Guid = node["Guid"] ? node["Guid"].as<std::uint64_t>() : 0;
            out.Name = node["Name"] ? node["Name"].as<std::string>() : std::string{};
            out.TypeTag = node["TypeTag"] ? node["TypeTag"].as<std::string>() : std::string{};
            return true;
        }
    };

    template <>
    struct convert<GPP::TransformComponent>
    {
        static Node encode(const GPP::TransformComponent& value)
        {
            Node node;
            node["Position"] = value.Position;
            node["Rotation"] = value.Rotation;
            node["Scale"] = value.Scale;
            return node;
        }

        static bool decode(const Node& node, GPP::TransformComponent& out)
        {
            if (node["Position"]) out.Position = node["Position"].as<glm::vec3>();
            if (node["Rotation"]) out.Rotation = node["Rotation"].as<glm::quat>();
            if (node["Scale"]) out.Scale = node["Scale"].as<glm::vec3>();
            return true;
        }
    };

    template <>
    struct convert<GPP::HierarchyComponent>
    {
        static Node encode(const GPP::HierarchyComponent& value)
        {
            Node node;
            node["ParentGuid"] = value.ParentGuid;
            return node;
        }

        static bool decode(const Node& node, GPP::HierarchyComponent& out)
        {
            out.ParentGuid = node["ParentGuid"] ? node["ParentGuid"].as<std::uint64_t>() : 0;
            return true;
        }
    };

    template <>
    struct convert<GPP::VelocityComponent>
    {
        static Node encode(const GPP::VelocityComponent& value)
        {
            Node node;
            node["Linear"] = value.Linear;
            node["Angular"] = value.Angular;
            return node;
        }

        static bool decode(const Node& node, GPP::VelocityComponent& out)
        {
            if (node["Linear"]) out.Linear = node["Linear"].as<glm::vec3>();
            if (node["Angular"]) out.Angular = node["Angular"].as<glm::vec3>();
            return true;
        }
    };

    template <>
    struct convert<GPP::RigidBodyComponent>
    {
        static Node encode(const GPP::RigidBodyComponent& value)
        {
            Node node;
            node["Type"] = value.Type;
            node["Mass"] = value.Mass;
            node["LinearDamping"] = value.LinearDamping;
            node["AngularDamping"] = value.AngularDamping;
            node["EnableGravity"] = value.EnableGravity;
            return node;
        }

        static bool decode(const Node& node, GPP::RigidBodyComponent& out)
        {
            if (node["Type"]) out.Type = node["Type"].as<GPP::RigidBodyType>();
            if (node["Mass"]) out.Mass = node["Mass"].as<float>();
            if (node["LinearDamping"]) out.LinearDamping = node["LinearDamping"].as<float>();
            if (node["AngularDamping"]) out.AngularDamping = node["AngularDamping"].as<float>();
            if (node["EnableGravity"]) out.EnableGravity = node["EnableGravity"].as<bool>();
            return true;
        }
    };

    template <>
    struct convert<GPP::ColliderComponent>
    {
        static Node encode(const GPP::ColliderComponent& value)
        {
            Node node;
            node["Shape"] = value.Shape;
            node["HalfExtents"] = value.HalfExtents;
            node["Radius"] = value.Radius;
            node["HalfHeight"] = value.HalfHeight;
            node["StaticFriction"] = value.StaticFriction;
            node["DynamicFriction"] = value.DynamicFriction;
            node["Restitution"] = value.Restitution;
            node["IsTrigger"] = value.IsTrigger;
            return node;
        }

        static bool decode(const Node& node, GPP::ColliderComponent& out)
        {
            if (node["Shape"]) out.Shape = node["Shape"].as<GPP::ColliderShape>();
            if (node["HalfExtents"]) out.HalfExtents = node["HalfExtents"].as<glm::vec3>();
            if (node["Radius"]) out.Radius = node["Radius"].as<float>();
            if (node["HalfHeight"]) out.HalfHeight = node["HalfHeight"].as<float>();
            if (node["StaticFriction"]) out.StaticFriction = node["StaticFriction"].as<float>();
            if (node["DynamicFriction"]) out.DynamicFriction = node["DynamicFriction"].as<float>();
            if (node["Restitution"]) out.Restitution = node["Restitution"].as<float>();
            if (node["IsTrigger"]) out.IsTrigger = node["IsTrigger"].as<bool>();
            return true;
        }
    };

    template <>
    struct convert<GPP::MeshComponent>
    {
        static Node encode(const GPP::MeshComponent& value)
        {
            Node node;
            node["AssetPath"] = value.AssetPath;
            node["CenterOfMassOffset"] = value.CenterOfMassOffset;
            return node;
        }

        static bool decode(const Node& node, GPP::MeshComponent& out)
        {
            if (node["AssetPath"]) out.AssetPath = node["AssetPath"].as<std::string>();
            if (node["CenterOfMassOffset"])
                out.CenterOfMassOffset = node["CenterOfMassOffset"].as<glm::vec3>();
            return true;
        }
    };

    template <>
    struct convert<GPP::CameraComponent>
    {
        static Node encode(const GPP::CameraComponent& value)
        {
            Node node;
            node["Fov"] = value.Fov;
            node["NearPlane"] = value.NearPlane;
            node["FarPlane"] = value.FarPlane;
            node["Primary"] = value.Primary;
            return node;
        }

        static bool decode(const Node& node, GPP::CameraComponent& out)
        {
            if (node["Fov"]) out.Fov = node["Fov"].as<float>();
            if (node["NearPlane"]) out.NearPlane = node["NearPlane"].as<float>();
            if (node["FarPlane"]) out.FarPlane = node["FarPlane"].as<float>();
            if (node["Primary"]) out.Primary = node["Primary"].as<bool>();
            return true;
        }
    };
}

namespace GPP
{
    export void RegisterBaseComponents()
    {
        static std::once_flag flag;
        std::call_once(flag, []
        {
            RegisterComponent<MetadataComponent>("Metadata");
            RegisterComponent<TransformComponent>("Transform");
            RegisterComponent<HierarchyComponent>("Hierarchy");
            RegisterComponent<VelocityComponent>("Velocity");
            RegisterComponent<RigidBodyComponent>("RigidBody");
            RegisterComponent<ColliderComponent>("Collider");
            RegisterComponent<MeshComponent>("Mesh");
            RegisterComponent<CameraComponent>("Camera");

            ComponentRegistrationOptions transientOptions;
            transientOptions.SyncToRenderState = false;
            transientOptions.Serializable = false;
            RegisterComponent<PhysicsActorHandle>("__PhysicsActorHandle", transientOptions);
        });
    }
}

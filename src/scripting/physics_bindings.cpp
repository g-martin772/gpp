module;
#include <entt/entt.hpp>
module GPP.Scripting;

import std;
import glm;
import GPP.Core;
import GPP.Simulation;
import :Luau;
import :Bindings;
import :PhysicsBindings;

namespace GPP
{
    namespace
    {
        constexpr std::string_view kPrelude = R"lua(
local function xyz(fn, name, value)
    local kind = typeof(value)
    if kind == "vector" or kind == "table" then
        local x, y, z = value.x, value.y, value.z
        if type(x) == "number" and type(y) == "number" and type(z) == "number" then return x, y, z end
    end
    error(`physics.{fn}: {name} must be a vector or \{x, y, z\}`, 3)
end

local function layer_mask(fn, mask)
    if mask == nil then return -1 end
    if type(mask) ~= "number" then error(`physics.{fn}: layer_mask must be a number`, 3) end
    return mask
end

function physics.raycast(origin, direction, max_dist, layer_mask_bits)
    local ox, oy, oz = xyz("raycast", "origin", origin)
    local dx, dy, dz = xyz("raycast", "direction", direction)
    local entity, px, py, pz, nx, ny, nz, distance =
        physics.__raycast(ox, oy, oz, dx, dy, dz, max_dist, layer_mask("raycast", layer_mask_bits))
    if entity == nil then return nil end
    return {
        entity = entity,
        point = vector.create(px, py, pz),
        normal = vector.create(nx, ny, nz),
        distance = distance,
        point64 = { x = px, y = py, z = pz },
    }
end

function physics.spherecast(origin, radius, direction, max_dist, layer_mask_bits)
    local ox, oy, oz = xyz("spherecast", "origin", origin)
    local dx, dy, dz = xyz("spherecast", "direction", direction)
    local entity, px, py, pz, nx, ny, nz, distance =
        physics.__spherecast(ox, oy, oz, radius, dx, dy, dz, max_dist, layer_mask("spherecast", layer_mask_bits))
    if entity == nil then return nil end
    return {
        entity = entity,
        point = vector.create(px, py, pz),
        normal = vector.create(nx, ny, nz),
        distance = distance,
        point64 = { x = px, y = py, z = pz },
    }
end

function physics.overlap(center, radius, layer_mask_bits)
    local cx, cy, cz = xyz("overlap", "center", center)
    return physics.__overlap(cx, cy, cz, radius, layer_mask("overlap", layer_mask_bits))
end

function physics.add_force(entity, force)
    local x, y, z = xyz("add_force", "force", force)
    physics.__push("add_force", entity, x, y, z)
end

function physics.add_torque(entity, torque)
    local x, y, z = xyz("add_torque", "torque", torque)
    physics.__push("add_torque", entity, x, y, z)
end

function physics.add_impulse(entity, impulse, point)
    local x, y, z = xyz("add_impulse", "impulse", impulse)
    if point == nil then
        physics.__push("add_impulse", entity, x, y, z)
    else
        local px, py, pz = xyz("add_impulse", "point", point)
        physics.__push("add_impulse", entity, x, y, z, px, py, pz)
    end
end

function physics.set_velocity(entity, velocity)
    local x, y, z = xyz("set_velocity", "velocity", velocity)
    physics.__push("set_velocity", entity, x, y, z)
end

function physics.set_angular_velocity(entity, velocity)
    local x, y, z = xyz("set_angular_velocity", "velocity", velocity)
    physics.__push("set_angular_velocity", entity, x, y, z)
end

function physics.set_kinematic_target(entity, position, rotation)
    local x, y, z = xyz("set_kinematic_target", "position", position)
    if rotation == nil then
        physics.__kinematic(entity, x, y, z)
    else
        local rx, ry, rz = xyz("set_kinematic_target", "rotation", rotation)
        physics.__kinematic(entity, x, y, z, rx, ry, rz)
    end
end

local joint_keys = {
    anchor = true, axis = true, distance = true, min_distance = true, stiffness = true, damping = true,
    break_force = true, break_torque = true,
}

function physics.create_joint(a, b, kind, params)
    params = params or {}
    for key in params do
        if not joint_keys[key] then error(`physics.create_joint: unknown parameter '{key}'`, 2) end
    end
    for _, key in { "distance", "min_distance", "stiffness", "damping", "break_force", "break_torque" } do
        if params[key] ~= nil and type(params[key]) ~= "number" then
            error(`physics.create_joint: {key} must be a number`, 2)
        end
    end
    local ax, ay, az, bx, by, bz
    if params.anchor ~= nil then ax, ay, az = xyz("create_joint", "anchor", params.anchor) end
    if params.axis ~= nil then bx, by, bz = xyz("create_joint", "axis", params.axis) end
    return physics.__create_joint(a, b, kind, ax, ay, az, bx, by, bz, params.distance, params.min_distance,
        params.stiffness, params.damping, params.break_force, params.break_torque)
end
)lua";

        [[nodiscard]] std::uint32_t ToMask(const double value)
        {
            return static_cast<std::uint32_t>(static_cast<std::int64_t>(value));
        }

        struct Api
        {
            std::shared_ptr<SceneBindings> Scene;
            std::function<PhysicsSimulationModule*()> Physics;

            [[nodiscard]] PhysicsSimulationModule& Module(const std::string_view fn) const
            {
                auto* physics = Physics ? Physics() : nullptr;
                if (!physics) throw std::runtime_error(std::format("physics.{}: no physics module in this simulation", fn));
                return *physics;
            }

            [[nodiscard]] static glm::dvec3 Vector(LuauNativeCall& call, const int first, const std::string_view fn)
            {
                const glm::dvec3 v(call.Number(first), call.Number(first + 1), call.Number(first + 2));
                if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z))
                {
                    throw std::runtime_error(std::format("physics.{}: vector components must be finite numbers", fn));
                }
                return v;
            }

            [[nodiscard]] static double Positive(LuauNativeCall& call, const int index, const std::string_view fn,
                                                 const std::string_view name)
            {
                const double value = call.Number(index, -1.0);
                if (!std::isfinite(value) || !(value > 0.0))
                {
                    throw std::runtime_error(std::format("physics.{}: {} must be a positive number", fn, name));
                }
                return value;
            }

            [[nodiscard]] std::string Describe(const std::uint64_t guid) const
            {
                const auto entity = Scene->Read->FindByGuid(guid);
                if (const auto* metadata = Scene->Read->Registry().try_get<MetadataComponent>(entity);
                    metadata && !metadata->Name.empty())
                {
                    return "'" + metadata->Name + "'";
                }
                return std::format("{}", guid);
            }

            [[nodiscard]] std::uint64_t Body(LuauNativeCall& call, const int index, const std::string_view fn,
                                             const std::optional<RigidBodyType> required = std::nullopt) const
            {
                if (!call.IsEntity(index)) throw std::runtime_error(std::format("physics.{}: expected an entity", fn));
                const std::uint64_t guid = call.Entity(index);
                if (!Scene->Read) throw std::runtime_error(std::format("physics.{}: no scene is loaded", fn));
                const auto entity = Scene->Read->FindByGuid(guid);
                if (!Scene->Read->IsValid(entity)) throw std::runtime_error(std::format("physics.{}: unknown entity", fn));
                const auto& registry = Scene->Read->Registry();
                const auto* body = registry.try_get<RigidBodyComponent>(entity);
                if (!body || !registry.all_of<ColliderComponent>(entity))
                {
                    throw std::runtime_error(std::format("physics.{}: entity {} has no RigidBody and Collider", fn, Describe(guid)));
                }
                if (required && body->Type != *required)
                {
                    throw std::runtime_error(std::format("physics.{}: entity {} must be a {} body", fn, Describe(guid),
                                                         *required == RigidBodyType::Dynamic ? "Dynamic" : "Kinematic"));
                }
                return guid;
            }
        };

        void PushHit(LuauNativeCall& call, const PhysicsHit& hit)
        {
            call.PushEntity(hit.Guid);
            call.PushNumber(hit.Point.x);
            call.PushNumber(hit.Point.y);
            call.PushNumber(hit.Point.z);
            call.PushNumber(hit.Normal.x);
            call.PushNumber(hit.Normal.y);
            call.PushNumber(hit.Normal.z);
            call.PushNumber(hit.Distance);
        }
    }

    void RegisterPhysicsBindings(LuauVm& vm, const std::shared_ptr<SceneBindings>& scene,
                                 std::function<PhysicsSimulationModule*()> physics)
    {
        auto api = std::make_shared<Api>(Api{scene, std::move(physics)});

        vm.Register("physics", "__raycast", [api](LuauNativeCall& call)
        {
            auto& module = api->Module("raycast");
            const auto origin = Api::Vector(call, 1, "raycast");
            const auto direction = Api::Vector(call, 4, "raycast");
            if (!(glm::length(direction) > 0.0)) throw std::runtime_error("physics.raycast: direction must not be zero");
            const double maxDistance = Api::Positive(call, 7, "raycast", "max_dist");
            const auto hit = module.Raycast(origin, direction, maxDistance, ToMask(call.Number(8, -1.0)));
            if (!hit) return 0;
            PushHit(call, *hit);
            return 8;
        });
        vm.Register("physics", "__spherecast", [api](LuauNativeCall& call)
        {
            auto& module = api->Module("spherecast");
            const auto origin = Api::Vector(call, 1, "spherecast");
            const double radius = Api::Positive(call, 4, "spherecast", "radius");
            const auto direction = Api::Vector(call, 5, "spherecast");
            if (!(glm::length(direction) > 0.0)) throw std::runtime_error("physics.spherecast: direction must not be zero");
            const double maxDistance = Api::Positive(call, 8, "spherecast", "max_dist");
            const auto hit = module.Spherecast(origin, radius, direction, maxDistance, ToMask(call.Number(9, -1.0)));
            if (!hit) return 0;
            PushHit(call, *hit);
            return 8;
        });
        vm.Register("physics", "__overlap", [api](LuauNativeCall& call)
        {
            auto& module = api->Module("overlap");
            const auto center = Api::Vector(call, 1, "overlap");
            const double radius = Api::Positive(call, 4, "overlap", "radius");
            std::vector<LuauValue> entities;
            for (const auto guid : module.Overlap(center, radius, ToMask(call.Number(5, -1.0)))) entities.emplace_back(guid);
            call.PushValues(entities);
            return 1;
        });
        vm.Register("physics", "__push", [api](LuauNativeCall& call)
        {
            const std::string fn = call.String(1);
            auto& module = api->Module(fn);
            PhysicsCommand command;
            command.Guid = api->Body(call, 2, fn, RigidBodyType::Dynamic);
            command.Vector = Api::Vector(call, 3, fn);
            if (fn == "add_force") command.Kind = PhysicsCommandKind::Force;
            else if (fn == "add_torque") command.Kind = PhysicsCommandKind::Torque;
            else if (fn == "set_velocity") command.Kind = PhysicsCommandKind::LinearVelocity;
            else if (fn == "set_angular_velocity") command.Kind = PhysicsCommandKind::AngularVelocity;
            else if (call.IsNil(6)) command.Kind = PhysicsCommandKind::Impulse;
            else
            {
                command.Kind = PhysicsCommandKind::ImpulseAtPoint;
                command.Point = Api::Vector(call, 6, fn);
            }
            module.QueueCommand(std::move(command));
            return 0;
        });
        vm.Register("physics", "__kinematic", [api](LuauNativeCall& call)
        {
            auto& module = api->Module("set_kinematic_target");
            PhysicsCommand command;
            command.Kind = PhysicsCommandKind::KinematicTarget;
            command.Guid = api->Body(call, 1, "set_kinematic_target", RigidBodyType::Kinematic);
            command.Vector = Api::Vector(call, 2, "set_kinematic_target");
            if (!call.IsNil(5))
            {
                const auto degrees = Api::Vector(call, 5, "set_kinematic_target");
                command.Rotation = glm::quat(glm::radians(glm::vec3(degrees)));
            }
            module.QueueCommand(std::move(command));
            return 0;
        });
        vm.Register("physics", "set_gravity_scale", [api](LuauNativeCall& call)
        {
            const std::uint64_t guid = api->Body(call, 1, "set_gravity_scale");
            const double scale = call.Number(2, std::numeric_limits<double>::quiet_NaN());
            if (!std::isfinite(scale)) throw std::runtime_error("physics.set_gravity_scale: scale must be a finite number");
            if (api->Scene->Write) api->Scene->Write(MakeComponentFieldWrite(guid, "RigidBody", "GravityScale", scale));
            return 0;
        });
        vm.Register("physics", "__create_joint", [api](LuauNativeCall& call)
        {
            auto& module = api->Module("create_joint");
            const std::uint64_t a = api->Body(call, 1, "create_joint");
            const std::uint64_t b = call.IsNil(2) ? 0 : api->Body(call, 2, "create_joint");
            if (b == a) throw std::runtime_error("physics.create_joint: a joint needs two different entities");
            const std::string kindName = call.String(3);
            PhysicsJointKind kind;
            if (kindName == "fixed") kind = PhysicsJointKind::Fixed;
            else if (kindName == "hinge") kind = PhysicsJointKind::Hinge;
            else if (kindName == "distance") kind = PhysicsJointKind::Distance;
            else throw std::runtime_error("physics.create_joint: kind must be \"fixed\", \"hinge\" or \"distance\"");

            PhysicsJointParams params;
            if (!call.IsNil(4)) params.Anchor = Api::Vector(call, 4, "create_joint");
            if (!call.IsNil(7)) params.Axis = Api::Vector(call, 7, "create_joint");
            if (!call.IsNil(10)) params.Distance = call.Number(10);
            if (!call.IsNil(11)) params.MinDistance = call.Number(11);
            params.Stiffness = call.Number(12);
            params.Damping = call.Number(13);
            params.BreakForce = call.Number(14);
            params.BreakTorque = call.Number(15);
            for (const double value : {params.Stiffness, params.Damping, params.BreakForce, params.BreakTorque,
                                       params.Distance.value_or(0.0), params.MinDistance.value_or(0.0)})
            {
                if (!std::isfinite(value) || value < 0.0)
                {
                    throw std::runtime_error("physics.create_joint: distances, stiffness, damping and break limits must be non-negative numbers");
                }
            }
            const auto id = module.CreateJoint(a, b, kind, params);
            if (!id) throw std::runtime_error("physics.create_joint: " + id.error());
            call.PushNumber(static_cast<double>(*id));
            return 1;
        });
        vm.Register("physics", "break_joint", [api](LuauNativeCall& call)
        {
            call.PushBoolean(api->Module("break_joint").BreakJoint(static_cast<std::uint32_t>(call.Number(1))));
            return 1;
        });
        vm.Register("physics", "joint_broken", [api](LuauNativeCall& call)
        {
            const auto broken = api->Module("joint_broken").JointBroken(static_cast<std::uint32_t>(call.Number(1)));
            if (!broken) return 0;
            call.PushBoolean(*broken);
            return 1;
        });
        if (auto result = vm.RunTrusted(kPrelude, "=gpp_physics"); !result)
        {
            throw std::runtime_error("gpp physics prelude failed: " + result.error().Message);
        }
    }
}

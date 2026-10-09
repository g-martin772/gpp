module;
#include <entt/entt.hpp>
module GPP.Scripting;

import std;
import glm;
import GPP.Core;
import GPP.Simulation;
import :Luau;
import :Bindings;

namespace GPP
{
    LuauType ToLuauType(const FieldType type)
    {
        switch (type)
        {
        case FieldType::Bool: return LuauType::Bool;
        case FieldType::Int:
        case FieldType::Enum: return LuauType::Int;
        case FieldType::Vec2: return LuauType::Vec2;
        case FieldType::Vec3: return LuauType::Vec3;
        case FieldType::Vec4: return LuauType::Vec4;
        case FieldType::String: return LuauType::String;
        case FieldType::Entity: return LuauType::Entity;
        case FieldType::Float: default: return LuauType::Float;
        }
    }

    const FieldInfo* FindComponentField(const std::string& component, const std::string& field)
    {
        const auto* info = ComponentRegistry::Instance().FindByName(component);
        if (!info) return nullptr;
        const auto it = std::ranges::find(info->Fields, field, &FieldInfo::Name);
        return it != info->Fields.end() ? &*it : nullptr;
    }

    // monostate when the entity, component or field is missing.
    FieldValue ReadComponentField(const Scene& scene, const std::uint64_t guid,
                                                       const std::string& component, const std::string& field)
    {
        const auto* info = FindComponentField(component, field);
        const entt::entity entity = scene.FindByGuid(guid);
        if (!info || !scene.IsValid(entity) || !info->Get) return std::monostate{};
        return info->Get(scene.Registry(), entity);
    }

    // The returned write resolves the entity and field again when it runs, so it is safe to queue.
    std::function<void(Scene&)> MakeComponentFieldWrite(std::uint64_t guid, std::string component,
                                                                             std::string field, FieldValue value)
    {
        return [guid, component = std::move(component), field = std::move(field), value = std::move(value)](Scene& scene)
        {
            const auto* info = FindComponentField(component, field);
            const entt::entity entity = scene.FindByGuid(guid);
            if (info && info->Set && scene.IsValid(entity)) info->Set(scene.Registry(), entity, value);
        };
    }

    // Installs scene.exists/get/set(entity, component, field[, value]) natives; entities are guid userdata.
    void RegisterSceneBindings(LuauVm& vm, const std::shared_ptr<SceneBindings>& context)
    {
        vm.Register("scene", "exists", [context](LuauNativeCall& call)
        {
            const entt::entity entity = context->Read ? context->Read->FindByGuid(call.Entity(1)) : entt::entity{entt::null};
            call.PushBoolean(context->Read && context->Read->IsValid(entity));
            return 1;
        });
        vm.Register("scene", "get", [context](LuauNativeCall& call)
        {
            if (!context->Read) return 0;
            call.PushValue(ReadComponentField(*context->Read, call.Entity(1), call.String(2), call.String(3)));
            return 1;
        });
        vm.Register("scene", "set", [context](LuauNativeCall& call)
        {
            const auto* info = FindComponentField(call.String(2), call.String(3));
            LuauValue value = info ? call.ToValue(4, ToLuauType(info->Type)) : LuauValue{};
            const bool ok = info && info->Set && context->Write && !std::holds_alternative<std::monostate>(value);
            if (ok) context->Write(MakeComponentFieldWrite(call.Entity(1), call.String(2), call.String(3), std::move(value)));
            call.PushBoolean(ok);
            return 1;
        });
    }

    // Global vec2()/vec4() constructors with operator metatables (vec3 is Luau's native vector); vec_meta holds the metatables.
    void RegisterMathBindings(LuauVm& vm)
    {
        vm.Register("gpp", "vecmeta", [](LuauNativeCall& call)
        {
            call.StoreVectorMetatables();
            return 0;
        });
        constexpr std::string_view prelude = R"lua(
local M2, M4 = {}, {}
local function install(mt, fields, n)
    mt.__index = mt
    local function zip(a, b, f)
        local r = {}
        for _, k in fields do r[k] = f(a[k], b[k]) end
        return setmetatable(r, mt)
    end
    local function scale(a, s, f)
        local r = {}
        for _, k in fields do r[k] = f(a[k], s) end
        return setmetatable(r, mt)
    end
    local function numOrVec(a, b, vf, sf)
        if type(a) == "number" then return scale(b, a, function(x, s) return sf(s, x) end) end
        if type(b) == "number" then return scale(a, b, sf) end
        return zip(a, b, vf)
    end
    mt.__add = function(a, b) return zip(a, b, function(x, y) return x + y end) end
    mt.__sub = function(a, b) return zip(a, b, function(x, y) return x - y end) end
    mt.__mul = function(a, b) return numOrVec(a, b, function(x, y) return x * y end, function(x, y) return x * y end) end
    mt.__div = function(a, b) return numOrVec(a, b, function(x, y) return x / y end, function(x, y) return x / y end) end
    mt.__unm = function(a) return scale(a, -1, function(x, y) return x * y end) end
    mt.__eq = function(a, b)
        for _, k in fields do if a[k] ~= b[k] then return false end end
        return true
    end
    mt.__tostring = function(a)
        local parts = {}
        for i, k in fields do parts[i] = tostring(a[k]) end
        return "(" .. table.concat(parts, ", ") .. ")"
    end
end
install(M2, {"x", "y"})
install(M4, {"x", "y", "z", "w"})
function vec2(x, y) return setmetatable({x = x, y = y}, M2) end
function vec4(x, y, z, w) return setmetatable({x = x, y = y, z = z, w = w}, M4) end
vec_meta = {vec2 = M2, vec4 = M4}
gpp.vecmeta(M2, M4)
)lua";
        if (auto result = vm.RunTrusted(prelude, "=gpp_math"); !result)
        {
            throw std::runtime_error("gpp math prelude failed: " + result.error().Message);
        }
    }

    std::expected<int, LuauError> LoadScript(LuauVm& vm, const AssetDirectories& assets,
                                                                  const std::string& name)
    {
        const auto source = assets.Read(kScriptAssetKind, name);
        if (!source) return std::unexpected(LuauError{"script '" + name + "' not found", name, 0});
        return vm.Load(*source, "=" + name);
    }

    std::vector<std::string> ListScripts(const AssetDirectories& assets)
    {
        return assets.List(kScriptAssetKind);
    }
}

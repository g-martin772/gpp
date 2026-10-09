module;
#include <entt/entt.hpp>
export module GPP.Scripting:Bindings;

import std;
import glm;
import GPP.Core;
import GPP.Simulation;
import :Luau;

export namespace GPP
{
    constexpr const char* kScriptAssetKind = "Scripts";

    [[nodiscard]] LuauType ToLuauType(FieldType type);
    [[nodiscard]] const FieldInfo* FindComponentField(const std::string& component, const std::string& field);
    // monostate when the entity, component or field is missing.
    [[nodiscard]] FieldValue ReadComponentField(const Scene& scene, std::uint64_t guid, const std::string& component,
                                                const std::string& field);
    // The returned write resolves the entity and field again when it runs, so it is safe to queue.
    [[nodiscard]] std::function<void(Scene&)> MakeComponentFieldWrite(std::uint64_t guid, std::string component,
                                                                      std::string field, FieldValue value);

    struct SceneBindings
    {
        const Scene* Read{nullptr};
        std::function<void(std::function<void(Scene&)>)> Write;
        // Preferred over Write for scene.set when present, so hosts can log and replay the edit.
        std::function<void(Command)> WriteCommand;
    };

    // Installs scene.exists/get/set/find/name/query/look_at natives; entities are guid userdata.
    void RegisterSceneBindings(LuauVm& vm, const std::shared_ptr<SceneBindings>& context);
    // Global vec2()/vec4() constructors with operator metatables (vec3 is Luau's native vector); vec_meta holds them.
    void RegisterMathBindings(LuauVm& vm);
    // Rebinds math.random to the generator `source` returns (null falls back to a process-local one) and
    // makes math.randomseed a no-op, so script randomness follows the runner's seed.
    void RegisterRandomBindings(LuauVm& vm, std::function<SimulationRandom*()> source);
    [[nodiscard]] std::expected<int, LuauError> LoadScript(LuauVm& vm, const AssetDirectories& assets, const std::string& name);
    [[nodiscard]] std::vector<std::string> ListScripts(const AssetDirectories& assets);
}

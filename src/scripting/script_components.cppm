module;
#include <yaml-cpp/yaml.h>
export module GPP.Scripting:ScriptComponents;

import std;
import glm;
import GPP.Core;
import GPP.Simulation;
import :Luau;
import :Bindings;

export namespace GPP
{
    enum class ScriptPropType { Float, Int, Bool, Vec3, Color, String, Entity };

    struct ScriptProperty
    {
        std::string Name;
        ScriptPropType Type{ScriptPropType::Float};
        FieldValue Default;
        float Min{0.0f};
        float Max{0.0f};
        bool HasRange{false};
    };

    // What a script's header table declares: `properties = { {name=, type=, default=, min=, max=}, ... }`.
    struct ScriptDescriptor
    {
        std::string Name;
        std::vector<ScriptProperty> Properties;
        std::string Error;

        [[nodiscard]] bool Ok() const { return Error.empty(); }
        [[nodiscard]] const ScriptProperty* Find(std::string_view name) const;
    };

    struct ScriptEntry
    {
        std::string Name;
        // Only explicitly set properties; the rest fall back to the descriptor's defaults.
        std::map<std::string, FieldValue> Props;
    };

    struct ScriptsComponent
    {
        std::vector<ScriptEntry> Entries;
    };

    [[nodiscard]] std::optional<ScriptPropType> ScriptPropTypeFromName(std::string_view name);
    [[nodiscard]] std::string_view ScriptPropTypeName(ScriptPropType type);
    [[nodiscard]] FieldType ToFieldType(ScriptPropType type);
    [[nodiscard]] FieldValue DefaultPropValue(ScriptPropType type);
    // monostate when the value cannot represent the type (e.g. a string for a float).
    [[nodiscard]] FieldValue CoerceProperty(const FieldValue& value, ScriptPropType type);
    [[nodiscard]] FieldMeta PropertyMeta(const ScriptProperty& property);
    // Every declared property with the entry's value (coerced) or the default; undeclared entry props are dropped.
    [[nodiscard]] std::vector<std::pair<std::string, FieldValue>> ResolveProps(const ScriptDescriptor& descriptor,
                                                                             const ScriptEntry& entry);

    [[nodiscard]] YAML::Node ScriptsToYaml(const ScriptsComponent& scripts);
    [[nodiscard]] ScriptsComponent ScriptsFromYaml(const YAML::Node& node);
    [[nodiscard]] YAML::Node PropValueToYaml(const FieldValue& value);
    [[nodiscard]] FieldValue PropValueFromYaml(const YAML::Node& node);

    constexpr const char* kScriptsComponentName = "Scripts";
    void RegisterScriptComponents();

    // Reads script headers through a private sandboxed VM; descriptors are cached until the source changes.
    class ScriptCatalog
    {
    public:
        explicit ScriptCatalog(const AssetDirectories& assets,
                               std::chrono::milliseconds recheck = std::chrono::milliseconds(500));
        ~ScriptCatalog();
        ScriptCatalog(const ScriptCatalog&) = delete;
        ScriptCatalog& operator=(const ScriptCatalog&) = delete;

        [[nodiscard]] std::shared_ptr<const ScriptDescriptor> Describe(const std::string& name);

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };

    // Registers gpp.declare and the describe helper used by ScriptCatalog; the sink receives each declaration.
    void RegisterScriptDeclarations(LuauVm& vm, const std::shared_ptr<ScriptDescriptor>& sink);
}

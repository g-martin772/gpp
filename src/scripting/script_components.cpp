module;
#include <yaml-cpp/yaml.h>
module GPP.Scripting;

import std;
import glm;
import GPP.Core;
import GPP.Simulation;
import :Luau;
import :Bindings;
import :ScriptComponents;

namespace GPP
{
    const ScriptProperty* ScriptDescriptor::Find(const std::string_view name) const
    {
        const auto it = std::ranges::find(Properties, name, &ScriptProperty::Name);
        return it != Properties.end() ? &*it : nullptr;
    }

    std::optional<ScriptPropType> ScriptPropTypeFromName(const std::string_view name)
    {
        if (name == "float") return ScriptPropType::Float;
        if (name == "int") return ScriptPropType::Int;
        if (name == "bool") return ScriptPropType::Bool;
        if (name == "vec3") return ScriptPropType::Vec3;
        if (name == "color") return ScriptPropType::Color;
        if (name == "string") return ScriptPropType::String;
        if (name == "entity") return ScriptPropType::Entity;
        return std::nullopt;
    }

    std::string_view ScriptPropTypeName(const ScriptPropType type)
    {
        switch (type)
        {
        case ScriptPropType::Float: return "float";
        case ScriptPropType::Int: return "int";
        case ScriptPropType::Bool: return "bool";
        case ScriptPropType::Vec3: return "vec3";
        case ScriptPropType::Color: return "color";
        case ScriptPropType::String: return "string";
        case ScriptPropType::Entity: return "entity";
        }
        return "float";
    }

    FieldType ToFieldType(const ScriptPropType type)
    {
        switch (type)
        {
        case ScriptPropType::Int: return FieldType::Int;
        case ScriptPropType::Bool: return FieldType::Bool;
        case ScriptPropType::Vec3:
        case ScriptPropType::Color: return FieldType::Vec3;
        case ScriptPropType::String: return FieldType::String;
        case ScriptPropType::Entity: return FieldType::Entity;
        case ScriptPropType::Float: default: return FieldType::Float;
        }
    }

    FieldValue DefaultPropValue(const ScriptPropType type)
    {
        switch (type)
        {
        case ScriptPropType::Int: return 0;
        case ScriptPropType::Bool: return false;
        case ScriptPropType::Vec3: return glm::vec3(0.0f);
        case ScriptPropType::Color: return glm::vec3(1.0f);
        case ScriptPropType::String: return std::string{};
        case ScriptPropType::Entity: return std::uint64_t{0};
        case ScriptPropType::Float: default: return 0.0f;
        }
    }

    FieldValue CoerceProperty(const FieldValue& value, const ScriptPropType type)
    {
        const auto number = [&]() -> std::optional<double>
        {
            if (const auto* f = std::get_if<float>(&value)) return *f;
            if (const auto* i = std::get_if<int>(&value)) return *i;
            return std::nullopt;
        };
        switch (type)
        {
        case ScriptPropType::Float:
            if (const auto n = number()) return static_cast<float>(*n);
            break;
        case ScriptPropType::Int:
            if (const auto n = number()) return static_cast<int>(*n);
            break;
        case ScriptPropType::Bool:
            if (const auto* b = std::get_if<bool>(&value)) return *b;
            if (const auto n = number()) return *n != 0.0;
            break;
        case ScriptPropType::Vec3:
        case ScriptPropType::Color:
            if (const auto* v = std::get_if<glm::vec3>(&value)) return *v;
            if (const auto* v = std::get_if<glm::vec4>(&value)) return glm::vec3(*v);
            break;
        case ScriptPropType::String:
            if (const auto* s = std::get_if<std::string>(&value)) return *s;
            if (const auto n = number()) return std::format("{}", *n);
            break;
        case ScriptPropType::Entity:
            if (const auto* g = std::get_if<std::uint64_t>(&value)) return *g;
            if (const auto* i = std::get_if<int>(&value); i && *i >= 0) return static_cast<std::uint64_t>(*i);
            break;
        }
        return std::monostate{};
    }

    FieldMeta PropertyMeta(const ScriptProperty& property)
    {
        FieldMeta meta;
        if (property.HasRange)
        {
            meta.Min = property.Min;
            meta.Max = property.Max;
        }
        if (property.Type == ScriptPropType::Color) meta.Kind = FieldKind::Color;
        if (property.Type == ScriptPropType::Entity) meta.Kind = FieldKind::EntityRef;
        return meta;
    }

    std::vector<std::pair<std::string, FieldValue>> ResolveProps(const ScriptDescriptor& descriptor,
                                                                 const ScriptEntry& entry)
    {
        std::vector<std::pair<std::string, FieldValue>> result;
        for (const auto& property : descriptor.Properties)
        {
            FieldValue value = std::monostate{};
            if (const auto it = entry.Props.find(property.Name); it != entry.Props.end())
            {
                value = CoerceProperty(it->second, property.Type);
            }
            if (std::holds_alternative<std::monostate>(value)) value = property.Default;
            if (property.HasRange)
            {
                if (auto* f = std::get_if<float>(&value)) *f = std::clamp(*f, property.Min, property.Max);
                else if (auto* i = std::get_if<int>(&value))
                {
                    *i = std::clamp(*i, static_cast<int>(property.Min), static_cast<int>(property.Max));
                }
            }
            if (const auto* guid = std::get_if<std::uint64_t>(&value); guid && *guid == 0) value = std::monostate{};
            result.emplace_back(property.Name, std::move(value));
        }
        return result;
    }

    YAML::Node PropValueToYaml(const FieldValue& value)
    {
        YAML::Node node;
        std::visit([&node](const auto& v)
        {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::monostate>) node = YAML::Node(YAML::NodeType::Null);
            else if constexpr (std::is_same_v<T, glm::vec2> || std::is_same_v<T, glm::vec3> || std::is_same_v<T, glm::vec4>)
                node = YAML::convert<T>::encode(v);
            else node = v;
        }, value);
        return node;
    }

    FieldValue PropValueFromYaml(const YAML::Node& node)
    {
        if (node.IsSequence())
        {
            std::vector<float> parts;
            for (const auto& part : node) parts.push_back(part.as<float>(0.0f));
            if (parts.size() == 2) return glm::vec2(parts[0], parts[1]);
            if (parts.size() == 3) return glm::vec3(parts[0], parts[1], parts[2]);
            if (parts.size() == 4) return glm::vec4(parts[0], parts[1], parts[2], parts[3]);
            return std::monostate{};
        }
        if (!node.IsScalar()) return std::monostate{};
        const std::string text = node.Scalar();
        if (text == "true") return true;
        if (text == "false") return false;
        std::int64_t whole = 0;
        const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), whole);
        if (ec == std::errc{} && end == text.data() + text.size())
        {
            if (whole >= std::numeric_limits<int>::min() && whole <= std::numeric_limits<int>::max()) return static_cast<int>(whole);
            return static_cast<std::uint64_t>(whole);
        }
        std::uint64_t big = 0;
        const auto [bigEnd, bigEc] = std::from_chars(text.data(), text.data() + text.size(), big);
        if (bigEc == std::errc{} && bigEnd == text.data() + text.size()) return big;
        float real = 0.0f;
        const auto [realEnd, realEc] = std::from_chars(text.data(), text.data() + text.size(), real);
        if (realEc == std::errc{} && realEnd == text.data() + text.size()) return real;
        return text;
    }

    YAML::Node ScriptsToYaml(const ScriptsComponent& scripts)
    {
        YAML::Node list(YAML::NodeType::Sequence);
        for (const auto& entry : scripts.Entries)
        {
            YAML::Node item;
            item["Name"] = entry.Name;
            if (!entry.Props.empty())
            {
                YAML::Node props(YAML::NodeType::Map);
                for (const auto& [name, value] : entry.Props)
                {
                    if (!std::holds_alternative<std::monostate>(value)) props[name] = PropValueToYaml(value);
                }
                item["Props"] = props;
            }
            list.push_back(item);
        }
        return list;
    }

    ScriptsComponent ScriptsFromYaml(const YAML::Node& node)
    {
        ScriptsComponent scripts;
        if (!node || !node.IsSequence()) return scripts;
        for (const auto& item : node)
        {
            ScriptEntry entry;
            entry.Name = item["Name"] ? item["Name"].as<std::string>() : std::string{};
            if (const auto props = item["Props"]; props && props.IsMap())
            {
                for (const auto& pair : props)
                {
                    auto value = PropValueFromYaml(pair.second);
                    if (!std::holds_alternative<std::monostate>(value)) entry.Props[pair.first.as<std::string>()] = std::move(value);
                }
            }
            scripts.Entries.push_back(std::move(entry));
        }
        return scripts;
    }

    void RegisterScriptComponents()
    {
        static std::once_flag flag;
        std::call_once(flag, []
        {
            RegisterComponent<ScriptsComponent>(kScriptsComponentName, ComponentDescription<ScriptsComponent>{
                .DisplayName = "Scripts",
                .Inspectable = false,
                .GraphExposed = false,
                .EncodeExtra = [](const ScriptsComponent& c, YAML::Node& node) { node = ScriptsToYaml(c); },
                .DecodeExtra = [](ScriptsComponent& c, const YAML::Node& node) { c = ScriptsFromYaml(node); },
            });
        });
    }

    void RegisterScriptDeclarations(LuauVm& vm, const std::shared_ptr<ScriptDescriptor>& sink)
    {
        vm.Register("gpp", "declare", [sink](LuauNativeCall& call)
        {
            const int index = static_cast<int>(call.Number(1));
            const std::string where = "properties[" + std::to_string(index) + "]";
            const std::string name = call.String(2);
            if (name.empty()) throw std::runtime_error(where + ": 'name' must be a non-empty string");
            if (sink->Find(name)) throw std::runtime_error(where + ": duplicate property '" + name + "'");
            const auto type = ScriptPropTypeFromName(call.String(3));
            if (!type)
            {
                throw std::runtime_error(where + ": unknown type '" + call.String(3) +
                                         "' (float, int, bool, vec3, color, string, entity)");
            }
            ScriptProperty property;
            property.Name = name;
            property.Type = *type;
            property.Default = DefaultPropValue(*type);
            if (!call.IsNil(4))
            {
                auto value = CoerceProperty(call.ToValueAuto(4, *type == ScriptPropType::Int), *type);
                if (std::holds_alternative<std::monostate>(value))
                {
                    throw std::runtime_error(where + ": 'default' does not match type '" + call.String(3) + "'");
                }
                property.Default = std::move(value);
            }
            if (call.IsNumber(5) && call.IsNumber(6))
            {
                property.HasRange = true;
                property.Min = static_cast<float>(call.Number(5));
                property.Max = static_cast<float>(call.Number(6));
            }
            sink->Properties.push_back(std::move(property));
            return 0;
        });
    }

    namespace
    {
        constexpr std::string_view kDescribePrelude = R"lua(
local script = {}
function script.describe(mod)
    local props = type(mod) == "table" and mod.properties
    if type(props) ~= "table" then return end
    for i, p in props do
        if type(p) ~= "table" then error("properties[" .. i .. "] must be a table", 0) end
        gpp.declare(i, p.name, p.type, p.default, p.min, p.max)
    end
end
gpp.script = script
)lua";
    }

    struct ScriptCatalog::Impl
    {
        struct Entry
        {
            std::size_t Hash{0};
            std::shared_ptr<const ScriptDescriptor> Descriptor;
            std::chrono::steady_clock::time_point CheckedAt{};
        };

        const AssetDirectories& Assets;
        LuauVm Vm;
        std::shared_ptr<ScriptDescriptor> Sink = std::make_shared<ScriptDescriptor>();
        int Helper{0};
        std::map<std::string, Entry> Cache;
        std::string SetupError;
        std::chrono::milliseconds Recheck{500};

        explicit Impl(const AssetDirectories& assets) : Assets(assets)
        {
            try
            {
                RegisterMathBindings(Vm);
                RegisterScriptDeclarations(Vm, Sink);
                if (auto result = Vm.RunTrusted(kDescribePrelude, "=gpp_describe"); !result)
                {
                    SetupError = result.error().Message;
                    return;
                }
                Vm.Seal();
                if (auto helper = Vm.Load("return gpp.script", "=describe")) Helper = *helper;
                else SetupError = helper.error().Message;
            }
            catch (const std::exception& e)
            {
                SetupError = e.what();
            }
        }

        std::shared_ptr<const ScriptDescriptor> Build(const std::string& name, const std::optional<std::string>& source)
        {
            auto descriptor = std::make_shared<ScriptDescriptor>();
            descriptor->Name = name;
            if (!SetupError.empty()) { descriptor->Error = SetupError; return descriptor; }
            if (!source) { descriptor->Error = "script '" + name + "' not found"; return descriptor; }
            auto module = Vm.Load(*source, "=" + name);
            if (!module) { descriptor->Error = module.error().Message; return descriptor; }
            *Sink = ScriptDescriptor{};
            const std::array<int, 1> refs{*module};
            auto described = Vm.CallWith(Helper, "describe", refs);
            Vm.Release(*module);
            descriptor->Properties = std::move(Sink->Properties);
            if (!described) descriptor->Error = described.error().Message;
            return descriptor;
        }
    };

    ScriptCatalog::ScriptCatalog(const AssetDirectories& assets, const std::chrono::milliseconds recheck)
        : m_Impl(std::make_unique<Impl>(assets))
    {
        m_Impl->Recheck = recheck;
    }
    ScriptCatalog::~ScriptCatalog() = default;

    std::shared_ptr<const ScriptDescriptor> ScriptCatalog::Describe(const std::string& name)
    {
        auto& entry = m_Impl->Cache[name];
        const auto now = std::chrono::steady_clock::now();
        if (entry.Descriptor && now - entry.CheckedAt < m_Impl->Recheck) return entry.Descriptor;
        entry.CheckedAt = now;
        const auto source = m_Impl->Assets.Read(kScriptAssetKind, name);
        const std::size_t hash = source ? std::hash<std::string>{}(*source) : 0;
        if (!entry.Descriptor || entry.Hash != hash)
        {
            entry.Descriptor = m_Impl->Build(name, source);
            entry.Hash = hash;
        }
        return entry.Descriptor;
    }
}

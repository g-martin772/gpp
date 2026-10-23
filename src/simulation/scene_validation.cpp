module;
#include <entt/entt.hpp>
#include <yaml-cpp/yaml.h>
module GPP.Simulation;

import :SceneValidation;
import :Scene;
import :Components;
import :ComponentRegistry;
import :Reflection;
import GPP.Core;
import std;

namespace GPP
{
    std::string SceneDiagnostic::Format(const std::string_view file) const
    {
        std::string out(file);
        if (Line > 0) out += std::format(":{}:{}", Line, Column);
        out += Severity == DiagnosticSeverity::Error ? ": error: " : ": warning: ";
        if (EntityGuid != 0 || !EntityName.empty())
        {
            out += EntityName.empty() ? std::format("entity {}", EntityGuid)
                                      : std::format("entity '{}' ({})", EntityName, EntityGuid);
            if (!Component.empty()) out += ", " + Component + (Field.empty() ? "" : "." + Field);
            out += ": ";
        }
        return out + Message;
    }

    std::size_t CountDiagnostics(const std::span<const SceneDiagnostic> diagnostics, const DiagnosticSeverity severity)
    {
        return static_cast<std::size_t>(std::ranges::count(diagnostics, severity, &SceneDiagnostic::Severity));
    }

    namespace
    {
        struct Sink
        {
            std::vector<SceneDiagnostic> Items;
            SceneDiagnostic Context;

            void Add(const DiagnosticSeverity severity, std::string message, const YAML::Mark& mark,
                     std::string field = {})
            {
                auto diagnostic = Context;
                diagnostic.Severity = severity;
                diagnostic.Message = std::move(message);
                diagnostic.Field = std::move(field);
                if (mark.line >= 0)
                {
                    diagnostic.Line = mark.line + 1;
                    diagnostic.Column = mark.column + 1;
                }
                Items.push_back(std::move(diagnostic));
            }
            void Warn(std::string message, const YAML::Mark& mark, std::string field = {})
            {
                Add(DiagnosticSeverity::Warning, std::move(message), mark, std::move(field));
            }
            void Fail(std::string message, const YAML::Mark& mark, std::string field = {})
            {
                Add(DiagnosticSeverity::Error, std::move(message), mark, std::move(field));
            }
        };

        std::string Join(const std::vector<std::string>& values)
        {
            std::string out;
            for (const auto& value : values) out += (out.empty() ? "" : ", ") + value;
            return out;
        }

        bool IsPlainNumber(const std::string& text)
        {
            double plain = 0.0;
            const auto result = std::from_chars(text.data(), text.data() + text.size(), plain);
            return result.ec == std::errc{} && result.ptr == text.data() + text.size();
        }

        bool CheckField(Sink& sink, const FieldInfo& field, const YAML::Node& value)
        {
            if (field.Type == FieldType::Enum)
            {
                if (!value.IsScalar())
                {
                    sink.Fail("expected one of: " + Join(field.Meta.Options), value.Mark(), field.Name);
                    return false;
                }
                const auto text = value.as<std::string>();
                if (std::ranges::find(field.Meta.Options, text) == field.Meta.Options.end())
                {
                    sink.Warn(std::format("unknown value '{}', the default is used (expected one of: {})", text,
                                          Join(field.Meta.Options)),
                              value.Mark(), field.Name);
                }
                return true;
            }
            if (field.Meta.Dim == Units::Dimension::None) return true;

            if (value.IsScalar())
            {
                const auto text = value.as<std::string>();
                if (IsPlainNumber(text) || Units::ParseQuantity(text, field.Meta.Dim)) return true;
                sink.Fail(std::format("cannot parse '{}' as a quantity or number", text), value.Mark(), field.Name);
                return false;
            }
            if (value.IsMap() && value["value"] && value["unit"])
            {
                const auto unit = value["unit"].as<std::string>("");
                if (!Units::FindUnit(field.Meta.Dim, unit))
                {
                    sink.Fail(std::format("unknown unit '{}'", unit), value["unit"].Mark(), field.Name);
                    return false;
                }
            }
            return true;
        }

        void ValidateComponent(Sink& sink, const ComponentTypeInfo& info, const YAML::Node& node, const int version)
        {
            if (!node.IsMap() && !node.IsNull() && !info.Fields.empty())
            {
                sink.Fail("expected a mapping of fields", node.Mark());
                return;
            }

            bool ok = true;
            if (info.StrictFields && node.IsMap())
            {
                for (const auto& pair : node)
                {
                    const auto key = pair.first.as<std::string>("");
                    const auto field = std::ranges::find_if(info.Fields, [&](const FieldInfo& f)
                    {
                        return f.Serialized && f.Name == key;
                    });
                    if (field == info.Fields.end())
                    {
                        sink.Warn(std::format("unknown field '{}' (ignored)", key), pair.first.Mark(), key);
                        continue;
                    }
                    ok = CheckField(sink, *field, pair.second) && ok;
                }
            }
            if (!ok || !info.Decode) return;

            entt::registry scratch;
            const auto entity = scratch.create();
            try
            {
                if (version < 2 && info.MigrateLegacy)
                {
                    YAML::Node migrated = YAML::Clone(node);
                    info.MigrateLegacy(migrated);
                    info.Decode(scratch, entity, migrated);
                }
                else
                {
                    info.Decode(scratch, entity, node);
                }
            }
            catch (const YAML::Exception& error)
            {
                std::string field;
                if (node.IsMap() && error.mark.line >= 0)
                {
                    for (const auto& pair : node)
                    {
                        if (pair.second.Mark().line == error.mark.line) field = pair.first.as<std::string>("");
                    }
                }
                sink.Fail(error.msg, error.mark.line >= 0 ? error.mark : node.Mark(), std::move(field));
            }
            catch (const std::exception& error)
            {
                sink.Fail(error.what(), node.Mark());
            }
        }

        std::string EntityName(const YAML::Node& components)
        {
            if (!components || !components.IsMap()) return {};
            const auto metadata = components["Metadata"];
            return metadata && metadata.IsMap() && metadata["Name"] ? metadata["Name"].as<std::string>("") : std::string{};
        }

        void ValidateEntity(Sink& sink, const YAML::Node& entityNode, const int version,
                            std::unordered_set<std::uint64_t>& seenGuids)
        {
            sink.Context = {};
            if (!entityNode.IsMap())
            {
                sink.Fail("entity must be a mapping with Guid and Components", entityNode.Mark());
                return;
            }

            if (const auto guid = entityNode["Guid"])
            {
                std::uint64_t value = 0;
                try
                {
                    value = guid.as<std::uint64_t>();
                    sink.Context.EntityGuid = value;
                    if (!seenGuids.insert(value).second)
                        sink.Warn("duplicate Guid, components are merged into the first entity", guid.Mark());
                }
                catch (const YAML::Exception& error)
                {
                    sink.Fail("Guid is not an unsigned integer", error.mark.line >= 0 ? error.mark : guid.Mark());
                }
            }
            else
            {
                sink.Warn("entity has no Guid, one is generated on every load", entityNode.Mark());
            }

            const auto components = entityNode["Components"];
            sink.Context.EntityName = EntityName(components);
            if (!components) return;
            if (!components.IsMap())
            {
                sink.Fail("Components must be a mapping", components.Mark());
                return;
            }

            for (const auto& pair : components)
            {
                const auto name = pair.first.as<std::string>("");
                const auto* info = ComponentRegistry::Instance().FindByName(name);
                sink.Context.Component = name;
                if (!info)
                    sink.Warn("unknown component (ignored)", pair.first.Mark());
                else if (!info->Decode)
                    sink.Warn("component has no YAML decoder (ignored)", pair.first.Mark());
                else
                    ValidateComponent(sink, *info, pair.second, version);
            }
            sink.Context.Component.clear();
        }

        void CheckEntityRefs(Sink& sink, const YAML::Node& entities, const std::unordered_set<std::uint64_t>& guids)
        {
            for (const auto& entityNode : entities)
            {
                if (!entityNode.IsMap() || !entityNode["Components"] || !entityNode["Components"].IsMap()) continue;
                sink.Context = {};
                if (entityNode["Guid"]) sink.Context.EntityGuid = entityNode["Guid"].as<std::uint64_t>(0);
                sink.Context.EntityName = EntityName(entityNode["Components"]);
                for (const auto& pair : entityNode["Components"])
                {
                    const auto* info = ComponentRegistry::Instance().FindByName(pair.first.as<std::string>(""));
                    if (!info || !pair.second.IsMap()) continue;
                    sink.Context.Component = info->Name;
                    for (const auto& field : info->Fields)
                    {
                        if (field.Type != FieldType::Entity || !field.Serialized || field.Meta.ReadOnly) continue;
                        const auto value = pair.second[field.Name];
                        if (!value || !value.IsScalar()) continue;
                        const auto target = value.as<std::uint64_t>(0);
                        if (target != 0 && !guids.contains(target))
                            sink.Warn(std::format("references entity {} which is not in this scene", target),
                                      value.Mark(), field.Name);
                    }
                }
            }
        }
    }

    std::vector<SceneDiagnostic> ValidateSceneYaml(const std::string& text)
    {
        RegisterBaseComponents();
        Sink sink;

        YAML::Node parsed;
        try
        {
            parsed = YAML::Load(text);
        }
        catch (const YAML::Exception& error)
        {
            sink.Fail(error.msg, error.mark);
            return std::move(sink.Items);
        }
        const YAML::Node root = parsed;
        if (!root.IsMap())
        {
            sink.Fail("document is empty or not a mapping", root.Mark());
            return std::move(sink.Items);
        }

        int version = 1;
        if (const auto node = root["SchemaVersion"])
        {
            try
            {
                version = node.as<int>();
                if (version > kSceneSchemaVersion)
                {
                    sink.Warn(std::format("scene schema version {} is newer than this build supports ({}); "
                                          "unknown data may be dropped",
                                          version, kSceneSchemaVersion),
                              node.Mark());
                }
            }
            catch (const YAML::Exception&)
            {
                sink.Fail("SchemaVersion is not an integer", node.Mark());
            }
        }
        for (const auto* key : {"Seed", "TimeScale"})
        {
            const auto node = root[key];
            if (!node) continue;
            try
            {
                if (std::string_view(key) == "Seed") (void)node.as<std::uint64_t>();
                else (void)node.as<double>();
            }
            catch (const YAML::Exception&)
            {
                sink.Fail(std::format("{} has an invalid value", key), node.Mark());
            }
        }

        const auto entities = root["Entities"];
        if (!entities) return std::move(sink.Items);
        if (!entities.IsSequence())
        {
            sink.Warn("Entities is not a sequence, no entities are loaded", entities.Mark());
            return std::move(sink.Items);
        }

        std::unordered_set<std::uint64_t> guids;
        for (const auto& entityNode : entities) ValidateEntity(sink, entityNode, version, guids);
        CheckEntityRefs(sink, entities, guids);
        return std::move(sink.Items);
    }

    std::vector<SceneDiagnostic> ValidateSceneFile(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
        {
            return {SceneDiagnostic{.Severity = DiagnosticSeverity::Error, .Message = "cannot open file"}};
        }
        const std::string text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        return ValidateSceneYaml(text);
    }
}

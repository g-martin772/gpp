module;
#include <entt/entt.hpp>
#include <yaml-cpp/yaml.h>
module GPP.Simulation;

import :Clipboard;
import std;

namespace GPP
{
    namespace
    {
        constexpr std::string_view kComponentTag = "Component";
        constexpr std::string_view kEntitiesTag = "Entities";

        std::string Emit(const YAML::Node& node)
        {
            return YAML::Dump(node);
        }

        bool Copyable(const ComponentTypeInfo* info)
        {
            return info && info->Add && info->Remove && info->Encode && info->Decode;
        }

        void RemapReferences(YAML::Node& components, const std::unordered_map<std::uint64_t, std::uint64_t>& remap,
                             const Scene& scene)
        {
            for (auto it = components.begin(); it != components.end(); ++it)
            {
                const auto* info = ComponentRegistry::Instance().FindByName(it->first.as<std::string>(""));
                if (!info) continue;
                for (const auto& field : info->Fields)
                {
                    if (field.Type != FieldType::Entity || !field.Serialized) continue;
                    auto value = it->second[field.Name];
                    if (!value || !value.IsScalar()) continue;
                    const auto old = value.as<std::uint64_t>(0);
                    if (old == 0) continue;
                    if (const auto mapped = remap.find(old); mapped != remap.end()) value = mapped->second;
                    else if (!scene.IsValid(scene.FindByGuid(old))) value = std::uint64_t{0};
                }
            }
        }
    }

    std::string CopyComponentYaml(const Scene& scene, const std::uint64_t guid, const std::string& component)
    {
        const auto entity = scene.FindByGuid(guid);
        const auto* info = ComponentRegistry::Instance().FindByName(component);
        if (!scene.IsValid(entity) || !Copyable(info) || !info->Has(scene.Registry(), entity)) return {};
        YAML::Node data;
        info->Encode(scene.Registry(), entity, data);
        YAML::Node root;
        root["Clipboard"] = std::string(kComponentTag);
        root["Component"] = component;
        root["Data"] = data;
        return Emit(root);
    }

    std::string CopyEntitiesYaml(const Scene& scene, const std::span<const std::uint64_t> guids)
    {
        YAML::Node entities(YAML::NodeType::Sequence);
        for (const auto guid : guids)
        {
            const auto entity = scene.FindByGuid(guid);
            if (!scene.IsValid(entity)) continue;
            const auto serialized = YAML::Load(scene.SerializeEntity(entity));
            YAML::Node item;
            item["Guid"] = guid;
            item["Components"] = serialized["Components"];
            entities.push_back(item);
        }
        if (entities.size() == 0) return {};
        YAML::Node root;
        root["Clipboard"] = std::string(kEntitiesTag);
        root["Entities"] = entities;
        return Emit(root);
    }

    std::optional<ClipboardContent> ParseClipboard(const std::string_view text)
    {
        if (text.find("Clipboard") == std::string_view::npos) return std::nullopt;
        try
        {
            const auto root = YAML::Load(std::string(text));
            if (!root.IsMap()) return std::nullopt;
            const auto tag = root["Clipboard"].as<std::string>("");
            ClipboardContent content;
            if (tag == kComponentTag)
            {
                content.Kind = ClipboardKind::Component;
                content.Component = root["Component"].as<std::string>("");
                content.Data = root["Data"];
                if (content.Component.empty() || !content.Data.IsDefined() || !content.Data.IsMap()) return std::nullopt;
                if (!Copyable(ComponentRegistry::Instance().FindByName(content.Component))) return std::nullopt;
                return content;
            }
            if (tag == kEntitiesTag)
            {
                content.Kind = ClipboardKind::Entities;
                const auto entities = root["Entities"];
                if (!entities.IsSequence()) return std::nullopt;
                for (const auto& item : entities)
                {
                    if (!item.IsMap() || !item["Components"].IsMap()) continue;
                    content.Entities.push_back({item["Guid"].as<std::uint64_t>(0), item["Components"]});
                }
                if (content.Entities.empty()) return std::nullopt;
                return content;
            }
        }
        catch (const std::exception&)
        {
        }
        return std::nullopt;
    }

    std::vector<Command> MakePasteComponentCommands(const Scene& scene, const ClipboardContent& content,
                                                    const std::span<const std::uint64_t> targets)
    {
        std::vector<Command> commands;
        const auto* info = ComponentRegistry::Instance().FindByName(content.Component);
        if (content.Kind != ClipboardKind::Component || !Copyable(info) || content.Component == "Metadata") return commands;
        const auto yaml = Emit(content.Data);
        for (const auto guid : targets)
        {
            const auto entity = scene.FindByGuid(guid);
            if (!scene.IsValid(entity)) continue;
            if (info->Has(scene.Registry(), entity)) commands.push_back(MakeSetComponentCommand(guid, content.Component, yaml));
            else commands.push_back(AddComponentCommand{guid, content.Component, yaml});
        }
        return commands;
    }

    std::vector<Command> MakePasteEntityCommands(const Scene& scene, const ClipboardContent& content,
                                                 const std::function<std::uint64_t()>& newGuid,
                                                 std::vector<std::uint64_t>* created)
    {
        std::vector<Command> commands;
        if (content.Kind != ClipboardKind::Entities) return commands;
        std::unordered_map<std::uint64_t, std::uint64_t> remap;
        std::vector<std::uint64_t> fresh;
        for (const auto& entity : content.Entities)
        {
            fresh.push_back(newGuid());
            if (entity.Guid != 0) remap[entity.Guid] = fresh.back();
        }
        for (std::size_t i = 0; i < content.Entities.size(); ++i)
        {
            auto components = YAML::Clone(content.Entities[i].Components);
            RemapReferences(components, remap, scene);
            if (const auto hierarchy = components["Hierarchy"]; hierarchy && hierarchy["ParentGuid"].as<std::uint64_t>(0) == 0)
            {
                components.remove("Hierarchy");
            }
            YAML::Node root;
            root["Components"] = components;
            commands.push_back(SpawnEntityCommand{fresh[i], Emit(root)});
        }
        if (created) *created = std::move(fresh);
        return commands;
    }

    bool AllHaveComponent(const Scene& scene, const std::span<const std::uint64_t> guids, const ComponentTypeInfo& info)
    {
        if (guids.empty() || !info.Has) return false;
        return std::ranges::all_of(guids, [&](const std::uint64_t guid)
        {
            const auto entity = scene.FindByGuid(guid);
            return scene.IsValid(entity) && info.Has(scene.Registry(), entity);
        });
    }

    std::optional<FieldSample> SampleField(const Scene& scene, const std::span<const std::uint64_t> guids,
                                           const ComponentTypeInfo& info, const FieldInfo& field)
    {
        if (!field.Get || !AllHaveComponent(scene, guids, info)) return std::nullopt;
        FieldSample sample;
        bool first = true;
        for (const auto guid : guids)
        {
            auto value = field.Get(scene.Registry(), scene.FindByGuid(guid));
            if (first)
            {
                sample.Value = std::move(value);
                first = false;
            }
            else if (!(value == sample.Value))
            {
                sample.Mixed = true;
            }
        }
        return sample;
    }

    namespace
    {
        template <typename V>
        bool MergeVector(const FieldValue& own, const FieldValue& displayed, const FieldValue& edited, FieldValue& out)
        {
            const auto* o = std::get_if<V>(&own);
            const auto* d = std::get_if<V>(&displayed);
            const auto* e = std::get_if<V>(&edited);
            if (!o || !d || !e) return false;
            V merged = *o;
            for (int i = 0; i < V::length(); ++i)
            {
                if ((*d)[i] != (*e)[i]) merged[i] = (*e)[i];
            }
            out = merged;
            return true;
        }
    }

    FieldValue MergeChangedComponents(const FieldValue& own, const FieldValue& displayed, const FieldValue& edited)
    {
        FieldValue merged;
        if (MergeVector<glm::vec2>(own, displayed, edited, merged) || MergeVector<glm::vec3>(own, displayed, edited, merged) ||
            MergeVector<glm::vec4>(own, displayed, edited, merged) || MergeVector<glm::dvec3>(own, displayed, edited, merged))
        {
            return merged;
        }
        return edited;
    }

    std::vector<Command> MakeSetFieldCommands(const Scene& scene, const std::span<const std::uint64_t> guids,
                                              const ComponentTypeInfo& info, const FieldInfo& field,
                                              const FieldValue& edited, const FieldValue* displayed)
    {
        std::vector<Command> commands;
        for (const auto guid : guids)
        {
            FieldValue value = edited;
            const auto entity = scene.FindByGuid(guid);
            if (displayed && field.Get && scene.IsValid(entity))
            {
                value = MergeChangedComponents(field.Get(scene.Registry(), entity), *displayed, edited);
            }
            commands.push_back(SetFieldCommand{guid, info.Name, field.Name, std::move(value)});
        }
        return commands;
    }

    std::vector<Command> MakeAddComponentCommands(const Scene& scene, const std::span<const std::uint64_t> guids,
                                                  const ComponentTypeInfo& info)
    {
        std::vector<Command> commands;
        if (!info.Add) return commands;
        for (const auto guid : guids)
        {
            const auto entity = scene.FindByGuid(guid);
            if (scene.IsValid(entity) && !info.Has(scene.Registry(), entity)) commands.push_back(AddComponentCommand{guid, info.Name, {}});
        }
        return commands;
    }

    std::vector<Command> MakeRemoveComponentCommands(const Scene& scene, const std::span<const std::uint64_t> guids,
                                                     const ComponentTypeInfo& info)
    {
        std::vector<Command> commands;
        if (!info.Remove) return commands;
        for (const auto guid : guids)
        {
            const auto entity = scene.FindByGuid(guid);
            if (scene.IsValid(entity) && info.Has(scene.Registry(), entity)) commands.push_back(RemoveComponentCommand{guid, info.Name});
        }
        return commands;
    }
}

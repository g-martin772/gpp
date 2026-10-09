module;
#include <entt/entt.hpp>
#include <yaml-cpp/yaml.h>
module GPP.Simulation;

import :Commands;
import std;

namespace GPP
{
    namespace
    {
        constexpr entt::entity kNull{entt::null};

        struct FieldAccess
        {
            const ComponentTypeInfo* Info{nullptr};
            const FieldInfo* Field{nullptr};
        };

        const FieldInfo* FindField(const ComponentTypeInfo& info, const std::string& name)
        {
            const auto it = std::ranges::find(info.Fields, name, &FieldInfo::Name);
            return it != info.Fields.end() ? &*it : nullptr;
        }

        bool IsAncestor(const Scene& scene, std::uint64_t candidate, const std::uint64_t of)
        {
            for (std::size_t depth = 0; candidate != 0 && depth < 1024; ++depth)
            {
                if (candidate == of) return true;
                const auto entity = scene.FindByGuid(candidate);
                if (!scene.IsValid(entity)) return false;
                const auto* hierarchy = scene.Registry().try_get<HierarchyComponent>(entity);
                candidate = hierarchy ? hierarchy->ParentGuid : 0;
            }
            return false;
        }

        bool Apply(Scene& scene, const SetFieldCommand& c, Command* inverse)
        {
            const auto entity = scene.FindByGuid(c.Guid);
            const auto* info = ComponentRegistry::Instance().FindByName(c.Component);
            if (!scene.IsValid(entity) || !info || !info->Has(scene.Registry(), entity)) return false;
            const auto* field = FindField(*info, c.Field);
            if (!field || !field->Set || !field->Get) return false;
            const FieldValue previous = field->Get(scene.Registry(), entity);
            if (previous == c.Value || !field->Set(scene.Registry(), entity, c.Value)) return false;
            scene.MarkDirty(entity);
            if (inverse) *inverse = SetFieldCommand{c.Guid, c.Component, c.Field, previous};
            return true;
        }

        bool Apply(Scene& scene, const AddComponentCommand& c, Command* inverse)
        {
            const auto entity = scene.FindByGuid(c.Guid);
            const auto* info = ComponentRegistry::Instance().FindByName(c.Component);
            if (!scene.IsValid(entity) || !info || !info->Add || info->Has(scene.Registry(), entity)) return false;
            info->Add(scene.Registry(), entity);
            if (!c.Yaml.empty() && info->Decode)
            {
                try { info->Decode(scene.Registry(), entity, YAML::Load(c.Yaml)); }
                catch (const YAML::Exception&) {}
            }
            scene.MarkDirty(entity);
            if (inverse) *inverse = RemoveComponentCommand{c.Guid, c.Component};
            return true;
        }

        bool Apply(Scene& scene, const RemoveComponentCommand& c, Command* inverse)
        {
            const auto entity = scene.FindByGuid(c.Guid);
            const auto* info = ComponentRegistry::Instance().FindByName(c.Component);
            if (!scene.IsValid(entity) || !info || !info->Remove || c.Component == "Metadata" ||
                !info->Has(scene.Registry(), entity))
            {
                return false;
            }
            if (inverse)
            {
                std::string yaml;
                if (info->Encode)
                {
                    YAML::Node node;
                    info->Encode(scene.Registry(), entity, node);
                    yaml = YAML::Dump(node);
                }
                *inverse = AddComponentCommand{c.Guid, c.Component, std::move(yaml)};
            }
            info->Remove(scene.Registry(), entity);
            scene.MarkDirty(entity);
            return true;
        }

        bool Apply(Scene& scene, const SpawnEntityCommand& c, Command* inverse)
        {
            if (scene.SpawnFromYaml(c.Guid, c.Yaml) == kNull) return false;
            if (inverse) *inverse = DestroyEntityCommand{c.Guid};
            return true;
        }

        bool Apply(Scene& scene, const DestroyEntityCommand& c, Command* inverse)
        {
            const auto entity = scene.FindByGuid(c.Guid);
            if (!scene.IsValid(entity)) return false;
            if (inverse) *inverse = SpawnEntityCommand{c.Guid, scene.SerializeEntity(entity)};
            scene.DestroyEntity(entity);
            return true;
        }

        bool Apply(Scene& scene, const CloneEntityCommand& c, Command* inverse)
        {
            if (scene.CloneEntity(scene.FindByGuid(c.SourceGuid), c.NewGuid) == kNull) return false;
            if (inverse) *inverse = DestroyEntityCommand{c.NewGuid};
            return true;
        }

        bool Apply(Scene& scene, const SetParentCommand& c, Command* inverse)
        {
            const auto entity = scene.FindByGuid(c.Guid);
            if (!scene.IsValid(entity)) return false;
            auto& registry = scene.Registry();
            const auto* current = registry.try_get<HierarchyComponent>(entity);
            const std::uint64_t previous = current ? current->ParentGuid : 0;
            if (previous == c.Parent) return false;
            if (c.Parent != 0 && (!scene.IsValid(scene.FindByGuid(c.Parent)) || IsAncestor(scene, c.Parent, c.Guid)))
            {
                return false;
            }
            if (c.Parent == 0) registry.remove<HierarchyComponent>(entity);
            else registry.emplace_or_replace<HierarchyComponent>(entity, c.Parent);
            scene.MarkDirty(entity);
            if (inverse) *inverse = SetParentCommand{c.Guid, previous};
            return true;
        }

        bool Apply(Scene& scene, const SetExtensionCommand& c, Command* inverse)
        {
            const auto* existing = scene.FindExtension(c.Name);
            if (inverse)
            {
                *inverse = existing ? SetExtensionCommand{c.Name, YAML::Dump(*existing), false}
                                    : SetExtensionCommand{c.Name, {}, true};
            }
            if (c.Remove)
            {
                if (!existing) return false;
                scene.RemoveExtension(c.Name);
                return true;
            }
            try { scene.SetExtension(c.Name, YAML::Load(c.Yaml)); }
            catch (const YAML::Exception&) { return false; }
            return true;
        }

        // Keeps the last SetField per (entity, component, field); everything else is untouched.
        void KeepLastFieldWrites(std::vector<Command>& commands)
        {
            std::set<std::tuple<std::uint64_t, std::string, std::string>> seen;
            std::vector<Command> kept;
            for (auto it = commands.rbegin(); it != commands.rend(); ++it)
            {
                if (const auto* set = std::get_if<SetFieldCommand>(&*it);
                    set && !seen.emplace(set->Guid, set->Component, set->Field).second)
                {
                    continue;
                }
                kept.push_back(std::move(*it));
            }
            std::ranges::reverse(kept);
            commands = std::move(kept);
        }

        std::string_view FieldTag(const FieldValue& value)
        {
            static constexpr std::array<std::string_view, 9> kTags{"none", "bool", "int", "float", "vec2",
                                                                   "vec3", "vec4", "string", "entity"};
            return kTags[value.index()];
        }

        YAML::Node EncodeValue(const FieldValue& value)
        {
            YAML::Node node;
            node["T"] = std::string(FieldTag(value));
            std::visit([&](const auto& v)
            {
                if constexpr (!std::is_same_v<std::decay_t<decltype(v)>, std::monostate>) node["V"] = v;
            }, value);
            return node;
        }

        FieldValue DecodeValue(const YAML::Node& node)
        {
            const auto tag = node["T"].as<std::string>("none");
            const auto v = node["V"];
            if (!v) return std::monostate{};
            if (tag == "bool") return v.as<bool>();
            if (tag == "int") return v.as<int>();
            if (tag == "float") return v.as<float>();
            if (tag == "vec2") return v.as<glm::vec2>();
            if (tag == "vec3") return v.as<glm::vec3>();
            if (tag == "vec4") return v.as<glm::vec4>();
            if (tag == "string") return v.as<std::string>();
            if (tag == "entity") return v.as<std::uint64_t>();
            return std::monostate{};
        }
    }

    bool ApplyCommand(Scene& scene, const Command& command, Command* inverse)
    {
        return std::visit([&](const auto& c) { return Apply(scene, c, inverse); }, command);
    }

    std::string CommandToYaml(const Command& command)
    {
        YAML::Node n;
        std::visit([&](const auto& c)
        {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, SetFieldCommand>)
            {
                n["Type"] = "SetField"; n["Guid"] = c.Guid; n["Component"] = c.Component; n["Field"] = c.Field;
                n["Value"] = EncodeValue(c.Value);
            }
            else if constexpr (std::is_same_v<T, AddComponentCommand>)
            {
                n["Type"] = "AddComponent"; n["Guid"] = c.Guid; n["Component"] = c.Component; n["Yaml"] = c.Yaml;
            }
            else if constexpr (std::is_same_v<T, RemoveComponentCommand>)
            {
                n["Type"] = "RemoveComponent"; n["Guid"] = c.Guid; n["Component"] = c.Component;
            }
            else if constexpr (std::is_same_v<T, SpawnEntityCommand>)
            {
                n["Type"] = "SpawnEntity"; n["Guid"] = c.Guid; n["Yaml"] = c.Yaml;
            }
            else if constexpr (std::is_same_v<T, DestroyEntityCommand>)
            {
                n["Type"] = "DestroyEntity"; n["Guid"] = c.Guid;
            }
            else if constexpr (std::is_same_v<T, CloneEntityCommand>)
            {
                n["Type"] = "CloneEntity"; n["Source"] = c.SourceGuid; n["New"] = c.NewGuid;
            }
            else if constexpr (std::is_same_v<T, SetParentCommand>)
            {
                n["Type"] = "SetParent"; n["Guid"] = c.Guid; n["Parent"] = c.Parent;
            }
            else
            {
                n["Type"] = "SetExtension"; n["Name"] = c.Name; n["Yaml"] = c.Yaml; n["Remove"] = c.Remove;
            }
        }, command);
        return YAML::Dump(n);
    }

    std::optional<Command> CommandFromYaml(const std::string& yaml)
    {
        try
        {
            const auto n = YAML::Load(yaml);
            const auto type = n["Type"].as<std::string>("");
            const auto guid = [&] { return n["Guid"].as<std::uint64_t>(0); };
            if (type == "SetField")
            {
                return SetFieldCommand{guid(), n["Component"].as<std::string>(), n["Field"].as<std::string>(),
                                       DecodeValue(n["Value"])};
            }
            if (type == "AddComponent")
            {
                return AddComponentCommand{guid(), n["Component"].as<std::string>(), n["Yaml"].as<std::string>("")};
            }
            if (type == "RemoveComponent") return RemoveComponentCommand{guid(), n["Component"].as<std::string>()};
            if (type == "SpawnEntity") return SpawnEntityCommand{guid(), n["Yaml"].as<std::string>("")};
            if (type == "DestroyEntity") return DestroyEntityCommand{guid()};
            if (type == "CloneEntity")
            {
                return CloneEntityCommand{n["Source"].as<std::uint64_t>(0), n["New"].as<std::uint64_t>(0)};
            }
            if (type == "SetParent") return SetParentCommand{guid(), n["Parent"].as<std::uint64_t>(0)};
            if (type == "SetExtension")
            {
                return SetExtensionCommand{n["Name"].as<std::string>(), n["Yaml"].as<std::string>(""),
                                           n["Remove"].as<bool>(false)};
            }
        }
        catch (const YAML::Exception&) {}
        return std::nullopt;
    }

    void CommandHistory::Record(UndoEntry entry, const std::chrono::milliseconds coalesceWindow)
    {
        std::scoped_lock lock(m_Mutex);
        m_Redo.clear();
        if (!m_Undo.empty() && !entry.CoalesceKey.empty())
        {
            auto& top = m_Undo.back();
            if (top.CoalesceKey == entry.CoalesceKey && entry.Time - top.Time <= coalesceWindow)
            {
                entry.Undo.insert(entry.Undo.end(), std::make_move_iterator(top.Undo.begin()),
                                  std::make_move_iterator(top.Undo.end()));
                top.Redo.insert(top.Redo.end(), std::make_move_iterator(entry.Redo.begin()),
                                std::make_move_iterator(entry.Redo.end()));
                top.Undo = std::move(entry.Undo);
                KeepLastFieldWrites(top.Undo);
                KeepLastFieldWrites(top.Redo);
                top.Time = entry.Time;
                return;
            }
        }
        m_Undo.push_back(std::move(entry));
        if (m_Undo.size() > kCapacity) m_Undo.pop_front();
    }

    std::optional<UndoEntry> CommandHistory::TakeUndo()
    {
        std::scoped_lock lock(m_Mutex);
        if (m_Undo.empty()) return std::nullopt;
        auto entry = std::move(m_Undo.back());
        m_Undo.pop_back();
        return entry;
    }

    std::optional<UndoEntry> CommandHistory::TakeRedo()
    {
        std::scoped_lock lock(m_Mutex);
        if (m_Redo.empty()) return std::nullopt;
        auto entry = std::move(m_Redo.back());
        m_Redo.pop_back();
        return entry;
    }

    void CommandHistory::PushUndo(UndoEntry entry)
    {
        entry.CoalesceKey.clear();
        std::scoped_lock lock(m_Mutex);
        m_Undo.push_back(std::move(entry));
        if (m_Undo.size() > kCapacity) m_Undo.pop_front();
    }

    void CommandHistory::PushRedo(UndoEntry entry)
    {
        std::scoped_lock lock(m_Mutex);
        m_Redo.push_back(std::move(entry));
    }

    void CommandHistory::Clear()
    {
        std::scoped_lock lock(m_Mutex);
        m_Undo.clear();
        m_Redo.clear();
    }

    bool CommandHistory::CanUndo() const
    {
        std::scoped_lock lock(m_Mutex);
        return !m_Undo.empty();
    }

    bool CommandHistory::CanRedo() const
    {
        std::scoped_lock lock(m_Mutex);
        return !m_Redo.empty();
    }

    std::string CommandHistory::UndoLabel() const
    {
        std::scoped_lock lock(m_Mutex);
        return m_Undo.empty() ? std::string{} : m_Undo.back().Label;
    }

    std::string CommandHistory::RedoLabel() const
    {
        std::scoped_lock lock(m_Mutex);
        return m_Redo.empty() ? std::string{} : m_Redo.back().Label;
    }
}

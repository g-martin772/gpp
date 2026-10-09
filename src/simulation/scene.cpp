module;
#include <entt/entt.hpp>
#include <yaml-cpp/yaml.h>
module GPP.Simulation;

import :Scene;
import std;

namespace GPP
{
    namespace
    {
        std::vector<entt::entity> AllEntities(const entt::registry& registry)
        {
            std::vector<entt::entity> entities;
            if (const auto* pool = registry.storage<entt::entity>())
            {
                // storage iterates back-to-front
                entities.assign(pool->begin(), pool->end());
                std::ranges::reverse(entities);
            }
            return entities;
        }
    }

    void ChangeSet::Merge(const ChangeSet& other)
    {
        if (All) return;
        if (other.All)
        {
            All = true;
            Guids.clear();
            return;
        }
        Guids.insert(other.Guids.begin(), other.Guids.end());
    }

    void Scene::MarkDirty(const entt::entity entity)
    {
        if (m_Dirty.All) return;
        if (const auto guid = m_GuidIndex.GuidOf(entity); guid != 0)
        {
            m_Dirty.Guids.insert(guid);
        }
        else
        {
            m_Dirty.All = true;
            m_Dirty.Guids.clear();
        }
    }

    ChangeSet Scene::TakeChanges()
    {
        ChangeSet taken = std::move(m_Dirty);
        m_Dirty = ChangeSet{};
        return taken;
    }

    Scene::Scene(std::string name, std::uint64_t id)
        : m_Metadata{.Id = id, .Name = std::move(name)}
    {
        RegisterBaseComponents();
    }

    Scene::Scene(const Scene& other)
        : m_Metadata(other.m_Metadata)
    {
        CopyAllFrom(other);
        CopyExtensionsFrom(other);
    }

    Scene& Scene::operator=(const Scene& other)
    {
        if (this != &other)
        {
            m_Registry.clear();
            m_GuidIndex = GuidIndex{};
            m_Metadata = other.m_Metadata;
            CopyAllFrom(other);
            CopyExtensionsFrom(other);
            MarkAllDirty();
        }
        return *this;
    }

    entt::entity Scene::CreateEntity(std::string name, std::string typeTag)
    {
        return CreateEntityWithGuid(GenerateGuid(), std::move(name), std::move(typeTag));
    }

    entt::entity Scene::CreateEntityWithGuid(const std::uint64_t guid, std::string name, std::string typeTag)
    {
        if (guid == 0 || m_GuidIndex.Find(guid) != entt::entity{entt::null}) return entt::entity{entt::null};
        const auto entity = m_Registry.create();
        m_Registry.emplace<MetadataComponent>(entity, MetadataComponent{
            .Guid = guid, .Name = std::move(name), .TypeTag = std::move(typeTag)
        });
        m_GuidIndex.Track(entity, guid);
        MarkDirty(entity);
        return entity;
    }

    entt::entity Scene::CloneEntity(const entt::entity source, const std::uint64_t newGuid)
    {
        if (!m_Registry.valid(source)) return entt::entity{entt::null};
        const auto entity = CreateEntityWithGuid(newGuid);
        if (entity == entt::entity{entt::null}) return entity;
        ComponentRegistry::Instance().ForEach([&](const ComponentTypeInfo& info)
        {
            if (info.SyncToRenderState && info.SyncEntity && info.Has(m_Registry, source))
            {
                info.SyncEntity(m_Registry, source, m_Registry, entity);
            }
        });
        m_Registry.get<MetadataComponent>(entity).Guid = newGuid;
        MarkDirty(entity);
        return entity;
    }

    void Scene::SetExtension(std::string name, const YAML::Node& value)
    {
        m_Extensions[std::move(name)] = YAML::Clone(value);
        ++m_ExtensionRevision;
    }

    void Scene::RemoveExtension(const std::string& name)
    {
        if (m_Extensions.erase(name) > 0) ++m_ExtensionRevision;
    }

    const YAML::Node* Scene::FindExtension(const std::string& name) const
    {
        const auto it = m_Extensions.find(name);
        return it != m_Extensions.end() ? &it->second : nullptr;
    }

    void Scene::CopyExtensionsFrom(const Scene& source)
    {
        m_Extensions.clear();
        for (const auto& [name, node] : source.m_Extensions) m_Extensions[name] = YAML::Clone(node);
        m_ExtensionRevision = source.m_ExtensionRevision;
    }

    void Scene::DestroyEntity(entt::entity entity)
    {
        if (!m_Registry.valid(entity)) return;
        MarkDirty(entity);
        m_GuidIndex.Untrack(entity);
        m_Registry.destroy(entity);
    }

    void Scene::CopyAllFrom(const Scene& source)
    {
        for (auto entity : AllEntities(source.m_Registry))
        {
            [[maybe_unused]] auto created = m_Registry.create(entity);
        }

        ComponentRegistry::Instance().ForEach([&](const ComponentTypeInfo& info)
        {
            info.CopyAll(source.m_Registry, m_Registry);
        });

        for (const auto& [guid, entity] : source.m_GuidIndex.GuidToEntity)
        {
            m_GuidIndex.Track(entity, guid);
        }
    }

    Scene Scene::Clone() const
    {
        Scene clone(m_Metadata.Name, GenerateGuid());
        clone.m_Registry.clear();
        clone.m_GuidIndex = GuidIndex{};
        clone.CopyAllFrom(*this);
        clone.CopyExtensionsFrom(*this);
        return clone;
    }

    void Scene::SyncInto(const Scene& source, Scene& target)
    {
        if (target.m_ExtensionRevision != source.m_ExtensionRevision) target.CopyExtensionsFrom(source);
        std::vector<entt::entity> stale;
        for (const auto& [guid, entity] : target.m_GuidIndex.GuidToEntity)
        {
            if (source.m_GuidIndex.Find(guid) == entt::entity{entt::null})
            {
                stale.push_back(entity);
            }
        }
        for (auto entity : stale)
        {
            target.m_GuidIndex.Untrack(entity);
            target.m_Registry.destroy(entity);
        }

        ComponentRegistry::Instance().ForEach([&](const ComponentTypeInfo& info)
        {
            if (!info.SyncToRenderState) return;
            info.SyncAll(source.m_Registry, source.m_GuidIndex, target.m_Registry, target.m_GuidIndex);
        });
    }

    void Scene::SyncChanges(const Scene& source, Scene& target, const ChangeSet& changes)
    {
        if (changes.All)
        {
            SyncInto(source, target);
            return;
        }
        if (target.m_ExtensionRevision != source.m_ExtensionRevision) target.CopyExtensionsFrom(source);

        using SyncFn = decltype(ComponentTypeInfo::SyncEntity);
        std::vector<SyncFn> types;
        ComponentRegistry::Instance().ForEach([&](const ComponentTypeInfo& info)
        {
            if (info.SyncToRenderState && info.SyncEntity) types.push_back(info.SyncEntity);
        });

        for (const auto guid : changes.Guids)
        {
            const auto srcEntity = source.m_GuidIndex.Find(guid);
            const auto dstEntity = target.m_GuidIndex.Find(guid);
            if (srcEntity == entt::entity{entt::null})
            {
                if (dstEntity != entt::entity{entt::null})
                {
                    target.m_GuidIndex.Untrack(dstEntity);
                    target.m_Registry.destroy(dstEntity);
                }
                continue;
            }

            const auto entity = target.m_GuidIndex.GetOrCreate(target.m_Registry, guid);
            for (const auto& sync : types)
            {
                sync(source.m_Registry, srcEntity, target.m_Registry, entity);
            }
        }
    }

    std::string Scene::SerializeToYaml() const
    {
        YAML::Emitter out;
        out << YAML::BeginMap;
        out << YAML::Key << "Scene" << YAML::Value << m_Metadata.Name;
        out << YAML::Key << "Entities" << YAML::Value << YAML::BeginSeq;

        for (auto entity : AllEntities(m_Registry))
        {
            if (!m_Registry.all_of<MetadataComponent>(entity)) continue;

            YAML::Node entityNode;
            entityNode["Guid"] = m_GuidIndex.GuidOf(entity);

            YAML::Node componentsNode;
            ComponentRegistry::Instance().ForEach([&](const ComponentTypeInfo& info)
            {
                if (!info.Serializable || !info.Has(m_Registry, entity)) return;
                YAML::Node componentNode;
                info.Encode(m_Registry, entity, componentNode);
                componentsNode[info.Name] = componentNode;
            });
            entityNode["Components"] = componentsNode;

            out << entityNode;
        }

        out << YAML::EndSeq;
        for (const auto& [name, node] : m_Extensions)
        {
            out << YAML::Key << name << YAML::Value << node;
        }
        out << YAML::EndMap;
        return out.c_str();
    }

    void Scene::DeserializeFromYaml(const std::string& yaml)
    {
        m_Registry.clear();
        m_GuidIndex = GuidIndex{};
        m_Extensions.clear();
        ++m_ExtensionRevision;
        MarkAllDirty();

        const auto root = YAML::Load(yaml);
        if (root["Scene"]) m_Metadata.Name = root["Scene"].as<std::string>();
        if (root.IsMap())
        {
            for (const auto& pair : root)
            {
                const auto key = pair.first.as<std::string>();
                if (key != "Scene" && key != "Entities") m_Extensions[key] = YAML::Clone(pair.second);
            }
        }

        const auto entities = root["Entities"];
        if (!entities || !entities.IsSequence()) return;

        for (const auto& entityNode : entities)
        {
            const auto guid = entityNode["Guid"] ? entityNode["Guid"].as<std::uint64_t>() : GenerateGuid();
            const auto entity = m_GuidIndex.GetOrCreate(m_Registry, guid);

            const auto componentsNode = entityNode["Components"];
            if (!componentsNode) continue;

            for (const auto& pair : componentsNode)
            {
                const auto name = pair.first.as<std::string>();
                const auto* info = ComponentRegistry::Instance().FindByName(name);
                if (!info || !info->Decode) continue;
                info->Decode(m_Registry, entity, pair.second);
            }
        }
    }
}

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

    Scene::Scene(std::string name, std::uint64_t id)
        : m_Metadata{.Id = id, .Name = std::move(name)}
    {
        RegisterBaseComponents();
    }

    Scene::Scene(const Scene& other)
        : m_Metadata(other.m_Metadata)
    {
        CopyAllFrom(other);
    }

    Scene& Scene::operator=(const Scene& other)
    {
        if (this != &other)
        {
            m_Registry.clear();
            m_GuidIndex = GuidIndex{};
            m_Metadata = other.m_Metadata;
            CopyAllFrom(other);
        }
        return *this;
    }

    entt::entity Scene::CreateEntity(std::string name, std::string typeTag)
    {
        const auto entity = m_Registry.create();
        const auto guid = GenerateGuid();
        m_Registry.emplace<MetadataComponent>(entity, MetadataComponent{
            .Guid = guid, .Name = std::move(name), .TypeTag = std::move(typeTag)
        });
        m_GuidIndex.Track(entity, guid);
        return entity;
    }

    void Scene::DestroyEntity(entt::entity entity)
    {
        if (!m_Registry.valid(entity)) return;
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
        return clone;
    }

    void Scene::SyncInto(const Scene& source, Scene& target)
    {
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
        out << YAML::EndMap;
        return out.c_str();
    }

    void Scene::DeserializeFromYaml(const std::string& yaml)
    {
        m_Registry.clear();
        m_GuidIndex = GuidIndex{};

        const auto root = YAML::Load(yaml);
        if (root["Scene"]) m_Metadata.Name = root["Scene"].as<std::string>();

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

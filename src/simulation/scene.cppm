module;
#include <entt/entt.hpp>
#include <yaml-cpp/yaml.h>
export module GPP.Simulation:Scene;

import std;
import GPP.Core;
import :Ecs;
import :ComponentRegistry;
import :Components;

namespace GPP
{
    export struct SceneMetadata
    {
        std::uint64_t Id{0};
        std::string Name;
    };

    export struct ChangeSet
    {
        bool All = false;
        std::unordered_set<std::uint64_t> Guids;

        [[nodiscard]] bool Empty() const noexcept { return !All && Guids.empty(); }
        void Merge(const ChangeSet& other);
        void Clear() noexcept { All = false; Guids.clear(); }
    };

    export class Scene
    {
    public:
        explicit Scene(std::string name = "Scene", std::uint64_t id = GenerateGuid());

        Scene(const Scene& other);
        Scene& operator=(const Scene& other);
        Scene(Scene&&) noexcept = default;
        Scene& operator=(Scene&&) noexcept = default;
        ~Scene() = default;

        [[nodiscard]] entt::registry& Registry() noexcept { return m_Registry; }
        [[nodiscard]] const entt::registry& Registry() const noexcept { return m_Registry; }
        [[nodiscard]] const SceneMetadata& Metadata() const noexcept { return m_Metadata; }
        void SetName(std::string name) { m_Metadata.Name = std::move(name); }

        entt::entity CreateEntity(std::string name = {}, std::string typeTag = {});
        entt::entity CreateEntityWithGuid(std::uint64_t guid, std::string name = {}, std::string typeTag = {});
        entt::entity CloneEntity(entt::entity source, std::uint64_t newGuid);
        void DestroyEntity(entt::entity entity);
        [[nodiscard]] bool IsValid(entt::entity entity) const { return m_Registry.valid(entity); }

        [[nodiscard]] entt::entity FindByGuid(std::uint64_t guid) const { return m_GuidIndex.Find(guid); }
        [[nodiscard]] std::uint64_t GuidOf(entt::entity entity) const { return m_GuidIndex.GuidOf(entity); }
        [[nodiscard]] const GuidIndex& Guids() const noexcept { return m_GuidIndex; }

        [[nodiscard]] Scene Clone() const;

        static void SyncInto(const Scene& source, Scene& target);
        static void SyncChanges(const Scene& source, Scene& target, const ChangeSet& changes);

        void SetExtension(std::string name, const YAML::Node& value);
        void RemoveExtension(const std::string& name);
        [[nodiscard]] const YAML::Node* FindExtension(const std::string& name) const;
        [[nodiscard]] const std::map<std::string, YAML::Node>& Extensions() const noexcept { return m_Extensions; }

        void MarkDirty(entt::entity entity);
        void MarkAllDirty() noexcept { m_Dirty.All = true; m_Dirty.Guids.clear(); }
        [[nodiscard]] ChangeSet TakeChanges();

        [[nodiscard]] std::string SerializeToYaml() const;
        void DeserializeFromYaml(const std::string& yaml);

    private:
        void CopyAllFrom(const Scene& source);
        void CopyExtensionsFrom(const Scene& source);

        entt::registry m_Registry;
        GuidIndex m_GuidIndex;
        SceneMetadata m_Metadata;
        std::map<std::string, YAML::Node> m_Extensions;
        std::uint64_t m_ExtensionRevision{0};
        ChangeSet m_Dirty{.All = true};
    };
}

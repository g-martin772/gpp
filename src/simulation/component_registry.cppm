module;
#include <entt/entt.hpp>
#include <yaml-cpp/yaml.h>
export module GPP.Simulation:ComponentRegistry;

import std;
import GPP.Core;
import :Ecs;

namespace GPP
{
    export struct GuidIndex
    {
        std::unordered_map<std::uint64_t, entt::entity> GuidToEntity;
        std::unordered_map<entt::entity, std::uint64_t> EntityToGuid;

        [[nodiscard]] entt::entity Find(std::uint64_t guid) const
        {
            const auto it = GuidToEntity.find(guid);
            return it != GuidToEntity.end() ? it->second : entt::entity{entt::null};
        }

        [[nodiscard]] std::uint64_t GuidOf(entt::entity entity) const
        {
            const auto it = EntityToGuid.find(entity);
            return it != EntityToGuid.end() ? it->second : 0;
        }

        void Track(entt::entity entity, std::uint64_t guid)
        {
            GuidToEntity[guid] = entity;
            EntityToGuid[entity] = guid;
        }

        void Untrack(entt::entity entity)
        {
            if (const auto it = EntityToGuid.find(entity); it != EntityToGuid.end())
            {
                GuidToEntity.erase(it->second);
                EntityToGuid.erase(it);
            }
        }

        entt::entity GetOrCreate(entt::registry& registry, std::uint64_t guid)
        {
            if (const auto existing = Find(guid); existing != entt::entity{entt::null})
            {
                return existing;
            }
            const auto entity = registry.create();
            Track(entity, guid);
            return entity;
        }
    };

    export std::uint64_t GenerateGuid()
    {
        static std::atomic<std::uint64_t> counter{1};
        static const std::uint64_t processSeed = []
        {
            std::random_device rd;
            return (static_cast<std::uint64_t>(rd()) << 32) ^ static_cast<std::uint64_t>(rd());
        }();
        const auto value = processSeed ^ (counter.fetch_add(1, std::memory_order_relaxed) * 0x9E3779B97F4A7C15ull);
        return value != 0 ? value : 1;
    }

    export template <typename T>
    concept YamlConvertible = requires(const T& value, const YAML::Node& node, T& out)
    {
        { YAML::convert<T>::encode(value) } -> std::same_as<YAML::Node>;
        { YAML::convert<T>::decode(node, out) } -> std::same_as<bool>;
    };

    export struct ComponentTypeInfo
    {
        std::string Name;
        bool Serializable = false;
        bool SyncToRenderState = true;

        std::function<bool(const entt::registry&, entt::entity)> Has;
        std::function<void(entt::registry&, entt::entity)> Remove;
        std::function<void(const entt::registry&, entt::entity, YAML::Node&)> Encode;
        std::function<void(entt::registry&, entt::entity, const YAML::Node&)> Decode;

        std::function<void(const entt::registry&, entt::registry&)> CopyAll;
        std::function<void(const entt::registry&, const GuidIndex&, entt::registry&, GuidIndex&)> SyncAll;
        std::function<void(const entt::registry&, entt::entity, entt::registry&, entt::entity)> SyncEntity;
    };

    export class ComponentRegistry
    {
    public:
        static ComponentRegistry& Instance()
        {
            static ComponentRegistry instance;
            return instance;
        }

        void Register(std::type_index typeId, ComponentTypeInfo info)
        {
            std::scoped_lock lock(m_Mutex);
            if (!m_ByName.contains(info.Name))
            {
                m_Order.push_back(typeId);
            }
            m_ByName.insert_or_assign(info.Name, typeId);
            m_Infos.insert_or_assign(typeId, std::move(info));
        }

        [[nodiscard]] const ComponentTypeInfo* FindByName(const std::string& name) const
        {
            std::scoped_lock lock(m_Mutex);
            const auto nameIt = m_ByName.find(name);
            if (nameIt == m_ByName.end()) return nullptr;
            const auto infoIt = m_Infos.find(nameIt->second);
            return infoIt != m_Infos.end() ? &infoIt->second : nullptr;
        }

        template <typename Fn>
        void ForEach(Fn&& fn) const
        {
            std::vector<ComponentTypeInfo> snapshot;
            {
                std::scoped_lock lock(m_Mutex);
                snapshot.reserve(m_Order.size());
                for (const auto& typeId : m_Order) snapshot.push_back(m_Infos.at(typeId));
            }
            for (const auto& info : snapshot) fn(info);
        }

    private:
        ComponentRegistry() = default;

        mutable std::mutex m_Mutex;
        std::vector<std::type_index> m_Order;
        std::unordered_map<std::type_index, ComponentTypeInfo> m_Infos;
        std::unordered_map<std::string, std::type_index> m_ByName;
    };

    export struct ComponentRegistrationOptions
    {
        bool SyncToRenderState = true;
        bool Serializable = true;
    };

    // To add a YAML::convert<T> specialization for your own component, #include
    // <yaml-cpp/yaml.h> directly in that translation unit before defining it: the primary
    // YAML::convert template is attached to the global module, and specializing it is only valid
    // where the header itself (not just an imported module that happens to use it) is visible.
    export template <typename T>
    void RegisterComponent(std::string name, ComponentRegistrationOptions options = {})
    {
        ComponentTypeInfo info;
        info.Name = name;
        info.SyncToRenderState = options.SyncToRenderState;

        info.Has = [](const entt::registry& r, entt::entity e) { return r.all_of<T>(e); };
        info.Remove = [](entt::registry& r, entt::entity e) { r.remove<T>(e); };

        info.CopyAll = [](const entt::registry& src, entt::registry& dst)
        {
            for (auto e : src.template view<T>())
            {
                if (dst.valid(e)) dst.template emplace_or_replace<T>(e, src.template get<T>(e));
            }
        };

        info.SyncAll = [](const entt::registry& src, const GuidIndex& srcIndex,
                          entt::registry& dst, GuidIndex& dstIndex)
        {
            for (auto [e, component] : src.template view<T>().each())
            {
                const auto guid = srcIndex.GuidOf(e);
                if (guid == 0) continue;
                const auto target = dstIndex.GetOrCreate(dst, guid);
                dst.template emplace_or_replace<T>(target, component);
            }

            std::vector<entt::entity> toRemove;
            for (auto e : dst.template view<T>())
            {
                const auto guid = dstIndex.GuidOf(e);
                const auto srcEntity = guid != 0 ? srcIndex.Find(guid) : entt::entity{entt::null};
                if (srcEntity == entt::entity{entt::null} || !src.template all_of<T>(srcEntity))
                {
                    toRemove.push_back(e);
                }
            }
            for (auto e : toRemove) dst.template remove<T>(e);
        };

        info.SyncEntity = [](const entt::registry& src, entt::entity srcEntity,
                             entt::registry& dst, entt::entity dstEntity)
        {
            if (const auto* component = src.template try_get<T>(srcEntity))
            {
                dst.template emplace_or_replace<T>(dstEntity, *component);
            }
            else
            {
                dst.template remove<T>(dstEntity);
            }
        };

        if constexpr (YamlConvertible<T>)
        {
            info.Serializable = options.Serializable;
            info.Encode = [](const entt::registry& r, entt::entity e, YAML::Node& node)
            {
                node = YAML::convert<T>::encode(r.template get<T>(e));
            };
            info.Decode = [](entt::registry& r, entt::entity e, const YAML::Node& node)
            {
                T value{};
                YAML::convert<T>::decode(node, value);
                r.template emplace_or_replace<T>(e, std::move(value));
            };
        }

        ComponentRegistry::Instance().Register(std::type_index(typeid(T)), std::move(info));
    }
}

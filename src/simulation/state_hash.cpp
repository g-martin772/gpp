module;
#include <entt/entt.hpp>
#include <yaml-cpp/yaml.h>
module GPP.Simulation;

import :StateHash;
import std;

namespace GPP
{
    std::uint64_t HashScene(const Scene& scene, const StateHashOptions options)
    {
        StateHasher hasher;
        const auto& metadata = scene.Metadata();
        hasher.U64(metadata.Seed);
        hasher.F64(metadata.TimeScale);
        const auto& clock = scene.Clock();
        hasher.F64(clock.Time);
        hasher.F64(clock.Elapsed);
        hasher.F64(clock.TimeCarry);
        hasher.F64(clock.ElapsedCarry);

        std::vector<ComponentTypeInfo> infos;
        ComponentRegistry::Instance().ForEach([&](const ComponentTypeInfo& info)
        {
            if (info.Serializable && info.Has && info.Encode) infos.push_back(info);
        });

        std::vector<std::pair<std::uint64_t, entt::entity>> entities(scene.Guids().GuidToEntity.begin(),
                                                                     scene.Guids().GuidToEntity.end());
        std::ranges::sort(entities);
        hasher.U64(entities.size());
        const auto& registry = scene.Registry();
        for (const auto& [guid, entity] : entities)
        {
            hasher.U64(guid);
            for (const auto& info : infos)
            {
                if (!info.Has(registry, entity)) continue;
                YAML::Node node;
                info.Encode(registry, entity, node);
                hasher.String(info.Name);
                hasher.String(YAML::Dump(node));
            }
            hasher.U64(~0ull);
        }

        if (options.IncludeExtensions)
        {
            for (const auto& [name, node] : scene.Extensions())
            {
                hasher.String(name);
                hasher.String(YAML::Dump(node));
            }
        }
        return hasher.Value();
    }
}

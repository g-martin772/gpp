module;
#include <entt/entt.hpp>
export module GPP.Simulation:ExclusiveFlags;

import std;
import :Reflection;
import :ComponentRegistry;
import :Components;

namespace GPP
{
    export struct ExclusiveFlagEdit
    {
        std::uint64_t Guid{0};
        std::string Component;
        std::string Field;
        bool Value{false};
    };

    // `preferred` wins a conflict and takes the flag when nobody holds it; `demoted` (a new entity) loses a conflict
    export [[nodiscard]] std::vector<ExclusiveFlagEdit> ExclusiveFlagEdits(const entt::registry& registry,
                                                                           const std::uint64_t preferred = 0,
                                                                           const std::uint64_t demoted = 0)
    {
        std::vector<ExclusiveFlagEdit> edits;
        ComponentRegistry::Instance().ForEach([&](const ComponentTypeInfo& info)
        {
            for (const auto& field : info.Fields)
            {
                if (!field.Meta.Exclusive || field.Type != FieldType::Bool || !field.Get || !field.Set || !info.Has) continue;
                std::vector<std::uint64_t> all;
                std::vector<std::uint64_t> holders;
                for (const auto [entity, metadata] : registry.view<const MetadataComponent>().each())
                {
                    if (!info.Has(registry, entity)) continue;
                    all.push_back(metadata.Guid);
                    const auto value = field.Get(registry, entity);
                    if (const auto* flag = std::get_if<bool>(&value); flag && *flag) holders.push_back(metadata.Guid);
                }
                std::ranges::reverse(all);
                std::ranges::reverse(holders);
                const auto add = [&](const std::uint64_t guid, const bool value)
                { edits.push_back({guid, info.Name, field.Name, value}); };
                if (holders.size() > 1)
                {
                    std::uint64_t keep = holders.front();
                    if (std::ranges::contains(holders, preferred)) keep = preferred;
                    else if (const auto it = std::ranges::find_if(holders, [&](const auto guid) { return guid != demoted; });
                             it != holders.end())
                    {
                        keep = *it;
                    }
                    for (const auto guid : holders)
                    {
                        if (guid != keep) add(guid, false);
                    }
                }
                else if (holders.empty() && !all.empty())
                {
                    add(std::ranges::contains(all, preferred) ? preferred : all.front(), true);
                }
            }
        });
        return edits;
    }

    export void NormalizeExclusiveFlags(entt::registry& registry)
    {
        for (const auto& edit : ExclusiveFlagEdits(registry))
        {
            const auto* info = ComponentRegistry::Instance().FindByName(edit.Component);
            if (!info) continue;
            const auto field = std::ranges::find(info->Fields, edit.Field, &FieldInfo::Name);
            if (field == info->Fields.end()) continue;
            for (const auto [entity, metadata] : registry.view<const MetadataComponent>().each())
            {
                if (metadata.Guid == edit.Guid) field->Set(registry, entity, edit.Value);
            }
        }
    }
}

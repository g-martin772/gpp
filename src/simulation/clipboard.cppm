module;
#include <entt/entt.hpp>
#include <yaml-cpp/yaml.h>
export module GPP.Simulation:Clipboard;

import std;
import :Reflection;
import :ComponentRegistry;
import :Scene;
import :Commands;

namespace GPP
{
    export enum class ClipboardKind { Component, Entities };

    export struct ClipboardEntity
    {
        std::uint64_t Guid{0};
        YAML::Node Components;
    };

    export struct ClipboardContent
    {
        ClipboardKind Kind{ClipboardKind::Component};
        std::string Component;
        YAML::Node Data;
        std::vector<ClipboardEntity> Entities;
    };

    export struct FieldSample
    {
        FieldValue Value;
        bool Mixed{false};
    };

    export [[nodiscard]] std::string CopyComponentYaml(const Scene& scene, std::uint64_t guid, const std::string& component);
    export [[nodiscard]] std::string CopyEntitiesYaml(const Scene& scene, std::span<const std::uint64_t> guids);
    export [[nodiscard]] std::optional<ClipboardContent> ParseClipboard(std::string_view text);

    export [[nodiscard]] std::vector<Command> MakePasteComponentCommands(const Scene& scene, const ClipboardContent& content,
                                                                         std::span<const std::uint64_t> targets);
    export [[nodiscard]] std::vector<Command> MakePasteEntityCommands(const Scene& scene, const ClipboardContent& content,
                                                                      const std::function<std::uint64_t()>& newGuid,
                                                                      std::vector<std::uint64_t>* created = nullptr);

    export [[nodiscard]] bool AllHaveComponent(const Scene& scene, std::span<const std::uint64_t> guids,
                                               const ComponentTypeInfo& info);
    export [[nodiscard]] std::optional<FieldSample> SampleField(const Scene& scene, std::span<const std::uint64_t> guids,
                                                                const ComponentTypeInfo& info, const FieldInfo& field);

    export [[nodiscard]] FieldValue MergeChangedComponents(const FieldValue& own, const FieldValue& displayed,
                                                           const FieldValue& edited);
    export [[nodiscard]] std::vector<Command> MakeSetFieldCommands(const Scene& scene, std::span<const std::uint64_t> guids,
                                                                   const ComponentTypeInfo& info, const FieldInfo& field,
                                                                   const FieldValue& edited,
                                                                   const FieldValue* displayed = nullptr);
    export [[nodiscard]] std::vector<Command> MakeAddComponentCommands(const Scene& scene, std::span<const std::uint64_t> guids,
                                                                       const ComponentTypeInfo& info);
    export [[nodiscard]] std::vector<Command> MakeRemoveComponentCommands(const Scene& scene,
                                                                          std::span<const std::uint64_t> guids,
                                                                          const ComponentTypeInfo& info);
}

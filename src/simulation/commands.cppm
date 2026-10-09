module;
#include <entt/entt.hpp>
#include <yaml-cpp/yaml.h>
export module GPP.Simulation:Commands;

import std;
import :Reflection;
import :ComponentRegistry;
import :Components;
import :Scene;

namespace GPP
{
    export struct SetFieldCommand
    {
        std::uint64_t Guid{0};
        std::string Component;
        std::string Field;
        FieldValue Value;
    };

    export struct AddComponentCommand
    {
        std::uint64_t Guid{0};
        std::string Component;
        std::string Yaml; // optional initial data; empty means the registered defaults
    };

    export struct RemoveComponentCommand
    {
        std::uint64_t Guid{0};
        std::string Component;
    };

    export struct SpawnEntityCommand
    {
        std::uint64_t Guid{0};
        std::string Yaml; // Scene::SerializeEntity output
    };

    export struct DestroyEntityCommand
    {
        std::uint64_t Guid{0};
    };

    export struct CloneEntityCommand
    {
        std::uint64_t SourceGuid{0};
        std::uint64_t NewGuid{0};
    };

    export struct SetParentCommand
    {
        std::uint64_t Guid{0};
        std::uint64_t Parent{0};
    };

    export struct SetExtensionCommand
    {
        std::string Name;
        std::string Yaml;
        bool Remove{false};
    };

    export using Command = std::variant<SetFieldCommand, AddComponentCommand, RemoveComponentCommand,
                                        SpawnEntityCommand, DestroyEntityCommand, CloneEntityCommand,
                                        SetParentCommand, SetExtensionCommand>;

    export struct CommandRecord
    {
        std::uint64_t Tick{0};
        Command Applied;
        std::optional<Command> Inverse;
    };

    // Applies through the dirty-tracked path. False when it changed nothing; `inverse` receives the undo command.
    export bool ApplyCommand(Scene& scene, const Command& command, Command* inverse = nullptr);

    export [[nodiscard]] std::string CommandToYaml(const Command& command);
    export [[nodiscard]] std::optional<Command> CommandFromYaml(const std::string& yaml);

    export struct UndoEntry
    {
        std::vector<Command> Undo;
        std::vector<Command> Redo;
        std::string Label;
        std::string CoalesceKey;
        std::chrono::steady_clock::time_point Time{};
    };

    export class CommandHistory
    {
    public:
        static constexpr std::size_t kCapacity = 256;

        // Clears redo. Merges into the top entry when both share a non-empty key and arrive within the window.
        void Record(UndoEntry entry, std::chrono::milliseconds coalesceWindow = std::chrono::milliseconds(600));
        std::optional<UndoEntry> TakeUndo();
        std::optional<UndoEntry> TakeRedo();
        void PushUndo(UndoEntry entry);
        void PushRedo(UndoEntry entry);
        void Clear();

        [[nodiscard]] bool CanUndo() const;
        [[nodiscard]] bool CanRedo() const;
        [[nodiscard]] std::string UndoLabel() const;
        [[nodiscard]] std::string RedoLabel() const;

    private:
        mutable std::mutex m_Mutex;
        std::deque<UndoEntry> m_Undo;
        std::vector<UndoEntry> m_Redo;
    };
}

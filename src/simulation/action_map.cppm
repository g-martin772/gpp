export module GPP.Simulation:ActionMap;

import std;
import glm;
import :Input;

export namespace GPP
{
    struct InputBinding
    {
        std::string Source;
        float Scale{1.0f};

        [[nodiscard]] bool operator==(const InputBinding&) const = default;
    };

    struct InputAction
    {
        std::string Name;
        std::vector<InputBinding> Bindings;

        [[nodiscard]] bool operator==(const InputAction&) const = default;
    };

    class ActionMap
    {
    public:
        [[nodiscard]] static ActionMap Defaults();
        [[nodiscard]] static std::string NormalizeSource(std::string_view source);
        [[nodiscard]] static std::expected<InputBinding, std::string> ParseBinding(std::string_view text);
        [[nodiscard]] static std::string FormatBinding(const InputBinding& binding);
        [[nodiscard]] static bool ValidName(std::string_view name);

        [[nodiscard]] static std::expected<ActionMap, std::string> FromYaml(const std::string& yaml);
        [[nodiscard]] std::string ToYaml() const;

        [[nodiscard]] const std::vector<InputAction>& Actions() const noexcept { return m_Actions; }
        [[nodiscard]] const InputAction* Find(std::string_view name) const;
        void Set(std::string name, std::vector<InputBinding> bindings);
        bool Remove(std::string_view name);
        void Overlay(const ActionMap& other);
        [[nodiscard]] bool Empty() const noexcept { return m_Actions.empty(); }

        [[nodiscard]] bool operator==(const ActionMap&) const = default;

    private:
        std::vector<InputAction> m_Actions;
    };

    class ActionRouter
    {
    public:
        void SetTarget(std::shared_ptr<SimulationInput> target);
        void SetMap(ActionMap map);
        [[nodiscard]] ActionMap Map() const;
        void SetSource(std::string_view source, float value);
        void AddMouse(glm::vec2 delta);
        void AddScroll(float delta);
        void SetCaptured(bool captured);
        void Release();

    private:
        void Push(const InputAction& action);
        void PushBindings();

        mutable std::mutex m_Mutex;
        ActionMap m_Map;
        std::map<std::string, float, std::less<>> m_Sources;
        std::shared_ptr<SimulationInput> m_Target;
    };
}

export module GPP.Simulation:Input;

import std;
import glm;

export namespace GPP
{
    constexpr float kInputPressThreshold = 0.5f;

    struct InputFrame
    {
        std::map<std::string, float, std::less<>> Values;
        std::set<std::string, std::less<>> Pressed;
        std::set<std::string, std::less<>> Released;
        glm::vec2 Mouse{0.0f};
        float Scroll{0.0f};

        [[nodiscard]] bool HasEvents() const { return !Pressed.empty() || !Released.empty() || Mouse != glm::vec2(0.0f) || Scroll != 0.0f; }
        [[nodiscard]] bool operator==(const InputFrame&) const = default;
    };

    class SimulationInput
    {
    public:
        void Set(std::string_view action, float value)
        {
            if (!std::isfinite(value)) value = 0.0f;
            std::scoped_lock lock(m_Mutex);
            const auto it = m_Pending.Values.find(action);
            const float before = it == m_Pending.Values.end() ? 0.0f : it->second;
            const bool wasDown = std::abs(before) >= kInputPressThreshold;
            const bool isDown = std::abs(value) >= kInputPressThreshold;
            if (!wasDown && isDown) m_Pending.Pressed.emplace(action);
            if (wasDown && !isDown) m_Pending.Released.emplace(action);
            if (value == 0.0f)
            {
                if (it != m_Pending.Values.end()) m_Pending.Values.erase(it);
            }
            else if (it != m_Pending.Values.end()) it->second = value;
            else m_Pending.Values.emplace(std::string(action), value);
        }

        void AddMouse(const glm::vec2 delta)
        {
            std::scoped_lock lock(m_Mutex);
            m_Pending.Mouse += delta;
        }

        void AddScroll(const float delta)
        {
            if (!std::isfinite(delta)) return;
            std::scoped_lock lock(m_Mutex);
            m_Pending.Scroll += delta;
        }

        void Clear()
        {
            std::scoped_lock lock(m_Mutex);
            for (const auto& [action, value] : m_Pending.Values)
            {
                if (std::abs(value) >= kInputPressThreshold) m_Pending.Released.insert(action);
            }
            m_Pending.Values.clear();
        }

        void Reset()
        {
            std::scoped_lock lock(m_Mutex);
            m_Pending = {};
        }

        [[nodiscard]] InputFrame Sample()
        {
            std::scoped_lock lock(m_Mutex);
            InputFrame frame = m_Pending;
            m_Pending.Pressed.clear();
            m_Pending.Released.clear();
            m_Pending.Mouse = glm::vec2(0.0f);
            m_Pending.Scroll = 0.0f;
            return frame;
        }

        void Begin(InputFrame frame) { m_Current = std::move(frame); }
        [[nodiscard]] const InputFrame& Current() const noexcept { return m_Current; }

        [[nodiscard]] float Axis(const std::string_view action) const
        {
            m_Used.store(true, std::memory_order_relaxed);
            const auto it = m_Current.Values.find(action);
            return it == m_Current.Values.end() ? 0.0f : it->second;
        }
        [[nodiscard]] bool Held(const std::string_view action) const { return std::abs(Axis(action)) >= kInputPressThreshold; }
        [[nodiscard]] bool Pressed(const std::string_view action) const
        {
            m_Used.store(true, std::memory_order_relaxed);
            return m_Current.Pressed.contains(action);
        }
        [[nodiscard]] bool Released(const std::string_view action) const
        {
            m_Used.store(true, std::memory_order_relaxed);
            return m_Current.Released.contains(action);
        }
        [[nodiscard]] glm::vec2 MouseDelta() const
        {
            m_Used.store(true, std::memory_order_relaxed);
            return m_Current.Mouse;
        }

        [[nodiscard]] float Scroll() const
        {
            m_Used.store(true, std::memory_order_relaxed);
            return m_Current.Scroll;
        }

        [[nodiscard]] bool Used() const noexcept { return m_Used.load(std::memory_order_relaxed); }
        void ResetUsed() noexcept { m_Used.store(false, std::memory_order_relaxed); }

        void SetCaptured(const bool captured) noexcept { m_Captured.store(captured ? 1 : 0, std::memory_order_relaxed); }
        [[nodiscard]] std::optional<bool> Captured() const noexcept
        {
            const auto state = m_Captured.load(std::memory_order_relaxed);
            return state < 0 ? std::nullopt : std::optional<bool>(state != 0);
        }

        void SetBindings(std::map<std::string, std::vector<std::string>, std::less<>> bindings)
        {
            std::scoped_lock lock(m_Mutex);
            m_Bindings = std::move(bindings);
        }

        [[nodiscard]] std::vector<std::string> Bindings(const std::string_view action)
        {
            std::scoped_lock lock(m_Mutex);
            const auto it = m_Bindings.find(action);
            return it == m_Bindings.end() ? std::vector<std::string>{} : it->second;
        }

    private:
        std::mutex m_Mutex;
        InputFrame m_Pending;
        InputFrame m_Current;
        std::map<std::string, std::vector<std::string>, std::less<>> m_Bindings;
        mutable std::atomic<bool> m_Used{false};
        std::atomic<signed char> m_Captured{-1};
    };
}

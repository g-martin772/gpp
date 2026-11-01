export module GPP.Scripting:EventBus;

import std;
import :Luau;

export namespace GPP
{
    struct ScriptEvent
    {
        std::string Name;
        std::vector<LuauValue> Args;
    };

    // sim thread only
    class ScriptEventBus
    {
    public:
        using Handler = std::function<void(const ScriptEvent&)>;
        using Token = std::uint64_t;
        static constexpr std::size_t kDefaultDispatchLimit = 4096;

        void Emit(std::string name, std::vector<LuauValue> args = {});
        [[nodiscard]] Token Subscribe(std::string name, Handler handler);
        void Unsubscribe(Token token);

        std::size_t Dispatch(std::size_t limit = kDefaultDispatchLimit);
        [[nodiscard]] bool TakeOverflow() { return std::exchange(m_Overflowed, false); }
        [[nodiscard]] std::size_t Pending() const { return m_Queue.size(); }
        void Clear() { m_Queue.clear(); }

    private:
        struct Subscription
        {
            Token Id{0};
            std::string Name;
            Handler Callback;
        };

        std::deque<ScriptEvent> m_Queue;
        std::vector<Subscription> m_Subscriptions;
        Token m_NextToken{1};
        bool m_Dispatching{false};
        bool m_Overflowed{false};
    };
}

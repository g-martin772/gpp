module GPP.Scripting;

import std;
import :Luau;
import :EventBus;

namespace GPP
{
    void ScriptEventBus::Emit(std::string name, std::vector<LuauValue> args)
    {
        m_Queue.push_back(ScriptEvent{std::move(name), std::move(args)});
    }

    ScriptEventBus::Token ScriptEventBus::Subscribe(std::string name, Handler handler)
    {
        const auto id = m_NextToken++;
        m_Subscriptions.push_back(Subscription{id, std::move(name), std::move(handler)});
        return id;
    }

    void ScriptEventBus::Unsubscribe(const Token token)
    {
        const auto it = std::ranges::find(m_Subscriptions, token, &Subscription::Id);
        if (it == m_Subscriptions.end()) return;
        if (m_Dispatching) it->Callback = nullptr;
        else m_Subscriptions.erase(it);
    }

    std::size_t ScriptEventBus::Dispatch(const std::size_t limit)
    {
        if (m_Dispatching) return 0;
        m_Dispatching = true;
        std::size_t delivered = 0;
        try
        {
            while (!m_Queue.empty())
            {
                if (delivered >= limit)
                {
                    m_Queue.clear();
                    m_Overflowed = true;
                    break;
                }
                const auto event = std::move(m_Queue.front());
                m_Queue.pop_front();
                ++delivered;
                const auto count = m_Subscriptions.size();
                for (std::size_t i = 0; i < count; ++i)
                {
                    if (m_Subscriptions[i].Callback && (m_Subscriptions[i].Name.empty() || m_Subscriptions[i].Name == event.Name))
                    {
                        const auto callback = m_Subscriptions[i].Callback;
                        callback(event);
                    }
                }
            }
        }
        catch (...)
        {
            m_Dispatching = false;
            std::erase_if(m_Subscriptions, [](const Subscription& s) { return !s.Callback; });
            throw;
        }
        m_Dispatching = false;
        std::erase_if(m_Subscriptions, [](const Subscription& s) { return !s.Callback; });
        return delivered;
    }
}

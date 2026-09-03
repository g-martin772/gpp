module;
#include <cstdint>

export module GPP.Core:Events;

import std;
import :DI.Service;
import :Application;
import :Threading.ThreadPool;

namespace GPP
{
    export enum class EventDelivery : $u8 { Immediate, Deferred, Async };

    export enum class EventTarget : $u8 { Main, Render, ThreadPool };

    export class EventSubscription
    {
    public:
        EventSubscription() = default;

        EventSubscription(std::shared_ptr<std::function<void()>> cancel, std::size_t id)
            : m_Cancel(std::move(cancel)), m_Id(id)
        {
        }

        EventSubscription(const EventSubscription&) = delete;
        EventSubscription& operator=(const EventSubscription&) = delete;

        EventSubscription(EventSubscription&& other) noexcept
            : m_Cancel(std::exchange(other.m_Cancel, nullptr)), m_Id(other.m_Id)
        {
        }

        EventSubscription& operator=(EventSubscription&& other) noexcept
        {
            if (this != &other)
            {
                Reset();
                m_Cancel = std::exchange(other.m_Cancel, nullptr);
                m_Id = other.m_Id;
            }
            return *this;
        }

        ~EventSubscription() { Reset(); }

        void Reset() noexcept
        {
            if (m_Cancel)
            {
                (*m_Cancel)();
                m_Cancel.reset();
            }
        }

    private:
        std::shared_ptr<std::function<void()>> m_Cancel{};
        std::size_t m_Id{};
    };

    export class EventDispatcher : public IService
    {
    public:
        using RenderExecutor = std::function<void(std::move_only_function<void()>)>;

        EventDispatcher() = default;
        ~EventDispatcher() override;

        void SetRenderExecutor(RenderExecutor executor);

        template <typename T>
        EventSubscription Subscribe(std::function<void(const T&)> callback,
                                    EventDelivery delivery = EventDelivery::Immediate,
                                    EventTarget target = EventTarget::Main)
        {
            const auto key = std::type_index(typeid(T));
            const auto id = m_NextId++;
            auto cancel = std::make_shared<std::function<void()>>([this, key, id]
            {
                std::scoped_lock lock(m_Mutex);
                auto it = m_Subscribers.find(key);
                if (it == m_Subscribers.end()) return;
                std::erase_if(it->second, [id](const Subscriber& subscriber) { return subscriber.Id == id; });
            });
            {
                std::scoped_lock lock(m_Mutex);
                m_Subscribers[key].push_back(Subscriber{
                    .Id = id,
                    .Delivery = delivery,
                    .Target = target,
                    .Callback = [callback = std::move(callback)](const void* event)
                    {
                        callback(*static_cast<const T*>(event));
                    }
                });
            }
            return EventSubscription(std::move(cancel), id);
        }

        template <typename T>
        void Publish(const T& event)
        {
            std::vector<Subscriber> subscribers;
            {
                std::scoped_lock lock(m_Mutex);
                if (auto it = m_Subscribers.find(std::type_index(typeid(T))); it != m_Subscribers.end())
                    subscribers = it->second;
            }
            for (const auto& subscriber : subscribers)
            {
                if (subscriber.Delivery == EventDelivery::Immediate)
                {
                    subscriber.Callback(&event);
                }
                else
                {
                    Enqueue(subscriber.Target, [callback = subscriber.Callback, event] { callback(&event); },
                            subscriber.Delivery == EventDelivery::Async);
                }
            }
        }

        void Pump(EventTarget target);

    private:
        struct Subscriber
        {
            std::size_t Id{};
            EventDelivery Delivery{};
            EventTarget Target{};
            std::function<void(const void*)> Callback;
        };

        struct Queue
        {
            std::queue<std::move_only_function<void()>> Items;
            bool Scheduled{false};
        };

        void Enqueue(EventTarget target, std::move_only_function<void()> task, bool schedule);
        void Schedule(EventTarget target);
        void Drain(EventTarget target);
        void CompleteScheduledDrain();

        std::mutex m_Mutex{};
        std::condition_variable m_DrainCondition{};
        std::size_t m_ScheduledDrains{0};
        bool m_Destroying{false};
        std::unordered_map<std::type_index, std::vector<Subscriber>> m_Subscribers{};
        std::array<Queue, 3> m_Queues{};
        RenderExecutor m_RenderExecutor{};
        std::atomic<std::size_t> m_NextId{1};
    };

    namespace
    {
        constexpr std::size_t Index(EventTarget target) { return static_cast<std::size_t>(target); }
    }

    EventDispatcher::~EventDispatcher()
    {
        std::unique_lock lock(m_Mutex);
        m_Destroying = true;
        m_DrainCondition.wait(lock, [this] { return m_ScheduledDrains == 0; });
    }

    void EventDispatcher::SetRenderExecutor(RenderExecutor executor)
    {
        std::scoped_lock lock(m_Mutex);
        m_RenderExecutor = std::move(executor);
    }

    void EventDispatcher::Enqueue(EventTarget target, std::move_only_function<void()> task, bool schedule)
    {
        bool shouldSchedule = false;
        {
            std::scoped_lock lock(m_Mutex);
            auto& queue = m_Queues[Index(target)];
            queue.Items.push(std::move(task));
            shouldSchedule = schedule && !queue.Scheduled;
            if (shouldSchedule) queue.Scheduled = true;
        }
        if (shouldSchedule) Schedule(target);
    }

    void EventDispatcher::Schedule(EventTarget target)
    {
        {
            std::scoped_lock lock(m_Mutex);
            if (m_Destroying)
            {
                m_Queues[Index(target)].Scheduled = false;
                return;
            }
            ++m_ScheduledDrains;
        }

        auto drain = [this, target]
        {
            Drain(target);
            CompleteScheduledDrain();
        };

        switch (target)
        {
        case EventTarget::Main:
            Application::Instance().ScheduleOnMainThread(std::move(drain));
            break;
        case EventTarget::Render:
            {
                RenderExecutor executor;
                {
                    std::scoped_lock lock(m_Mutex);
                    executor = m_RenderExecutor;
                }
                if (executor) executor(std::move(drain));
                else ThreadPool::Instance().Submit(std::move(drain));
                break;
            }
        case EventTarget::ThreadPool:
            ThreadPool::Instance().Submit(std::move(drain));
            break;
        }
    }

    void EventDispatcher::CompleteScheduledDrain()
    {
        std::scoped_lock lock(m_Mutex);
        --m_ScheduledDrains;
        m_DrainCondition.notify_all();
    }

    void EventDispatcher::Drain(EventTarget target)
    {
        while (true)
        {
            std::move_only_function<void()> task;
            {
                std::scoped_lock lock(m_Mutex);
                auto& queue = m_Queues[Index(target)];
                if (queue.Items.empty())
                {
                    queue.Scheduled = false;
                    return;
                }
                task = std::move(queue.Items.front());
                queue.Items.pop();
            }
            task();
        }
    }

    void EventDispatcher::Pump(EventTarget target)
    {
        Drain(target);
    }
}

export module GPP.Core:Threading.Timer;

import std;
import :Threading.ThreadPool;
import :Threading.Task;

namespace GPP
{
    struct TimerEntry
    {
        std::chrono::steady_clock::time_point deadline;
        std::move_only_function<void()> callback;
        std::stop_token stopToken;

        bool operator>(const TimerEntry& other) const noexcept
        {
            return deadline > other.deadline;
        }
    };

    export class TimerSystem
    {
    public:
        static TimerSystem& Instance()
        {
            static TimerSystem instance;
            return instance;
        }

        ~TimerSystem()
        {
            m_StopSource.request_stop();
            m_CV.notify_all();
            if (m_Worker.joinable())
            {
                m_Worker.join();
            }
        }

        void Schedule(std::chrono::milliseconds delay, std::stop_token stopToken,
                      std::move_only_function<void()> callback)
        {
            auto deadline = std::chrono::steady_clock::now() + delay;
            {
                std::scoped_lock lock(m_Mutex);
                m_Timers.emplace(deadline, std::move(callback), stopToken);
            }
            m_CV.notify_all();
        }

    private:
        TimerSystem()
        {
            m_Worker = std::jthread([this] { Run(m_StopSource.get_token()); });
        }

        void Run(std::stop_token stopToken)
        {
            std::unique_lock lock(m_Mutex);
            while (!stopToken.stop_requested())
            {
                if (m_Timers.empty())
                {
                    m_CV.wait(lock, stopToken, [this] { return !m_Timers.empty(); });
                }
                else
                {
                    auto nextDeadline = m_Timers.top().deadline;
                    m_CV.wait_until(lock, stopToken, nextDeadline, [this, nextDeadline]
                    {
                        return m_Timers.top().deadline < nextDeadline;
                    });
                }

                if (stopToken.stop_requested())
                {
                    break;
                }

                auto now = std::chrono::steady_clock::now();
                while (!m_Timers.empty() && m_Timers.top().deadline <= now)
                {
                    auto entry = std::move(const_cast<TimerEntry&>(m_Timers.top()));
                    m_Timers.pop();

                    if (!entry.stopToken.stop_requested())
                    {
                        ThreadPool::Instance().Submit(std::move(entry.callback));
                    }
                }
            }
        }

        std::priority_queue<TimerEntry, std::vector<TimerEntry>, std::greater<TimerEntry>> m_Timers{};
        std::mutex m_Mutex{};
        std::condition_variable_any m_CV{};
        std::stop_source m_StopSource{};
        std::jthread m_Worker{};
    };

    export template <typename Rep, typename Period>
    Task<void> DelayAsync(std::chrono::duration<Rep, Period> duration, std::stop_token stopToken = {})
    {
        struct DelayState
        {
            std::atomic<bool> resumed{false};
            std::exception_ptr exception{nullptr};
            std::optional<std::stop_callback<std::function<void()>>> stopCb{};
        };

        struct DelayAwaiter
        {
            std::chrono::milliseconds ms;
            std::stop_token stopToken;
            std::shared_ptr<DelayState> state = std::make_shared<DelayState>();

            bool await_ready() const noexcept
            {
                return ms.count() <= 0 || stopToken.stop_requested();
            }

            void await_suspend(std::coroutine_handle<> h)
            {
                if (stopToken.stop_requested())
                {
                    state->exception = std::make_exception_ptr(std::runtime_error("Operation cancelled"));
                    h.resume();
                    return;
                }

                auto resumeAction = [h, state = this->state](bool cancelled)
                {
                    if (!state->resumed.exchange(true))
                    {
                        if (cancelled)
                        {
                            state->exception = std::make_exception_ptr(std::runtime_error("Operation cancelled"));
                        }
                        h.resume();
                    }
                };

                if (stopToken.stop_possible())
                {
                    state->stopCb.emplace(stopToken, [resumeAction]
                    {
                        ThreadPool::Instance().Submit([resumeAction] { resumeAction(true); });
                    });
                }

                TimerSystem::Instance().Schedule(ms, stopToken, [resumeAction]
                {
                    resumeAction(false);
                });
            }

            void await_resume()
            {
                if (stopToken.stop_requested() && !state->exception)
                {
                    throw std::runtime_error("Operation cancelled");
                }
                if (state->exception)
                {
                    std::rethrow_exception(state->exception);
                }
            }
        };

        co_await DelayAwaiter{
            .ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration),
            .stopToken = stopToken
        };
    }

    template <typename T>
    struct WaitState
    {
        std::mutex mutex{};
        std::coroutine_handle<> awaitingCoro{nullptr};
        std::optional<T> result{std::nullopt};
        std::exception_ptr exception{nullptr};
        bool completed{false};
    };

    template <>
    struct WaitState<void>
    {
        std::mutex mutex{};
        std::coroutine_handle<> awaitingCoro{nullptr};
        std::exception_ptr exception{nullptr};
        bool completed{false};
    };

    template <typename T>
    struct RaceAwaiter
    {
        std::shared_ptr<WaitState<T>> state;

        bool await_ready() const noexcept
        {
            std::scoped_lock lock(state->mutex);
            return state->completed;
        }

        void await_suspend(std::coroutine_handle<> h) noexcept
        {
            std::scoped_lock lock(state->mutex);
            state->awaitingCoro = h;
        }

        T await_resume()
        {
            std::scoped_lock lock(state->mutex);
            if (state->exception)
            {
                std::rethrow_exception(state->exception);
            }
            return std::move(*(state->result));
        }
    };

    template <>
    struct RaceAwaiter<void>
    {
        std::shared_ptr<WaitState<void>> state;

        bool await_ready() const noexcept
        {
            std::scoped_lock lock(state->mutex);
            return state->completed;
        }

        void await_suspend(std::coroutine_handle<> h) noexcept
        {
            std::scoped_lock lock(state->mutex);
            state->awaitingCoro = h;
        }

        void await_resume()
        {
            std::scoped_lock lock(state->mutex);
            if (state->exception)
            {
                std::rethrow_exception(state->exception);
            }
        }
    };

    template <typename T>
    FireAndForget RunTimerBranch(std::chrono::milliseconds delay, std::shared_ptr<WaitState<T>> s,
                                 std::stop_token stopToken, std::stop_source targetCancel = {})
    {
        try
        {
            co_await DelayAsync(delay, stopToken);

            if (stopToken.stop_requested())
            {
                co_return; // Timer cancelled cooperatively because the target task finished first
            }

            std::coroutine_handle<> toResume{nullptr};
            {
                std::scoped_lock lock(s->mutex);
                if (!s->completed)
                {
                    s->completed = true;
                    s->exception = std::make_exception_ptr(std::runtime_error("Asynchronous operation timed out."));
                    toResume = s->awaitingCoro;
                    if (targetCancel.stop_possible())
                    {
                        targetCancel.request_stop();
                    }
                }
            }
            if (toResume)
            {
                ThreadPool::Instance().Submit([toResume] { toResume.resume(); });
            }
        }
        catch (...)
        {
            // Ignore timer internal errors / cancellation
        }
    }

    template <typename T>
    FireAndForget RunTargetBranch(Task<T> t, std::shared_ptr<WaitState<T>> s, std::stop_source timerCancel)
    {
        try
        {
            if constexpr (std::is_void_v<T>)
            {
                co_await t;

                std::coroutine_handle<> toResume{nullptr};
                {
                    std::scoped_lock lock(s->mutex);
                    if (!s->completed)
                    {
                        s->completed = true;
                        toResume = s->awaitingCoro;
                        timerCancel.request_stop(); // Stop the timer branch
                    }
                }
                if (toResume)
                {
                    ThreadPool::Instance().Submit([toResume] { toResume.resume(); });
                }
            }
            else
            {
                T value = co_await t;

                std::coroutine_handle<> toResume{nullptr};
                {
                    std::scoped_lock lock(s->mutex);
                    if (!s->completed)
                    {
                        s->completed = true;
                        s->result = std::move(value);
                        toResume = s->awaitingCoro;
                        timerCancel.request_stop(); // Stop the timer branch
                    }
                }
                if (toResume)
                {
                    ThreadPool::Instance().Submit([toResume] { toResume.resume(); });
                }
            }
        }
        catch (...)
        {
            std::coroutine_handle<> toResume{nullptr};
            {
                std::scoped_lock lock(s->mutex);
                if (!s->completed)
                {
                    s->completed = true;
                    s->exception = std::current_exception();
                    toResume = s->awaitingCoro;
                    timerCancel.request_stop();
                }
            }
            if (toResume)
            {
                ThreadPool::Instance().Submit([toResume] { toResume.resume(); });
            }
        }
    }

    export template <typename T>
    Task<T> WaitAsync(Task<T> targetTask, std::chrono::milliseconds timeout, std::stop_source targetCancel = {})
    {
        auto state = std::make_shared<WaitState<T>>();
        std::stop_source timerCancellation{};

        RunTimerBranch(timeout, state, timerCancellation.get_token(), targetCancel);
        RunTargetBranch(std::move(targetTask), state, timerCancellation);

        if constexpr (std::is_void_v<T>)
        {
            co_await RaceAwaiter<T>{state};
            co_return;
        }
        else
        {
            co_return co_await RaceAwaiter<T>{state};
        }
    }

    export template <typename Fn>
        requires std::invocable<Fn, std::stop_token>
    auto WaitAsync(Fn&& fn, std::chrono::milliseconds timeout)
    {
        using TaskType = std::invoke_result_t<Fn, std::stop_token>;
        using ValueType = typename TaskType::value_type;

        std::stop_source targetCancellation{};
        auto task = fn(targetCancellation.get_token());

        return WaitAsync<ValueType>(std::move(task), timeout, targetCancellation);
    }
}

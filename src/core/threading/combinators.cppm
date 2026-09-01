export module GPP.Core:Threading.Combinators;
import :Threading.Task;
import std;

namespace GPP
{
    // Spawn (aka Task.Run)

    export template <typename T>
    FireAndForget Spawn(Task<T> task)
    {
        try
        {
            co_await task;
        }
        catch (const std::exception& e)
        {
            Logger::LogError("Exception caught in spawned background task: {}", e.what());
        }
    }

    // WhenAll

    template <typename T>
    struct WhenAllState
    {
        std::mutex mutex{};
        std::coroutine_handle<> awaitingCoro{nullptr};
        std::vector<T> results{};
        std::atomic<std::size_t> remaining{0};
        std::exception_ptr exception{nullptr};

        explicit WhenAllState(std::size_t count) : remaining(count)
        {
            results.resize(count);
        }
    };

    template <typename T>
    struct WhenAllAwaiter
    {
        std::shared_ptr<WhenAllState<T>> state;

        bool await_ready() const noexcept
        {
            return state->remaining.load(std::memory_order_acquire) == 0;
        }

        bool await_suspend(std::coroutine_handle<> h) noexcept
        {
            std::scoped_lock lock(state->mutex);
            if (state->remaining.load(std::memory_order_acquire) == 0)
            {
                return false;
            }
            state->awaitingCoro = h;
            return true;
        }

        std::vector<T> await_resume()
        {
            if (state->exception)
            {
                std::rethrow_exception(state->exception);
            }
            return std::move(state->results);
        }
    };

    export template <typename T>
    Task<std::vector<T>> WhenAll(std::vector<Task<T>> tasks)
    {
        if (tasks.empty())
        {
            co_return std::vector<T>{};
        }

        auto state = std::make_shared<WhenAllState<T>>(tasks.size());

        for (std::size_t i = 0; i < tasks.size(); ++i)
        {
            [](std::size_t index, Task<T> t, std::shared_ptr<WhenAllState<T>> s) -> FireAndForget
            {
                try
                {
                    T value = co_await t;
                    {
                        std::scoped_lock lock(s->mutex);
                        s->results[index] = std::move(value);
                    }
                }
                catch (...)
                {
                    std::scoped_lock lock(s->mutex);
                    if (!s->exception)
                    {
                        s->exception = std::current_exception();
                    }
                }

                if (s->remaining.fetch_sub(1, std::memory_order_acq_rel) == 1)
                {
                    std::coroutine_handle<> toResume{nullptr};
                    {
                        std::scoped_lock lock(s->mutex);
                        toResume = s->awaitingCoro;
                    }
                    if (toResume)
                    {
                        ThreadPool::Instance().Submit([toResume] { toResume.resume(); });
                    }
                }
            }(i, std::move(tasks[i]), state);
        }

        co_return co_await WhenAllAwaiter<T>{state};
    }

    struct WhenAllVoidState
    {
        std::mutex mutex{};
        std::coroutine_handle<> awaitingCoro{nullptr};
        std::atomic<std::size_t> remaining{0};
        std::exception_ptr exception{nullptr};

        explicit WhenAllVoidState(std::size_t count) : remaining(count)
        {
        }
    };

    struct WhenAllVoidAwaiter
    {
        std::shared_ptr<WhenAllVoidState> state;

        bool await_ready() const noexcept
        {
            return state->remaining.load(std::memory_order_acquire) == 0;
        }

        bool await_suspend(std::coroutine_handle<> h) noexcept
        {
            std::scoped_lock lock(state->mutex);
            if (state->remaining.load(std::memory_order_acquire) == 0)
            {
                return false;
            }
            state->awaitingCoro = h;
            return true;
        }

        void await_resume()
        {
            if (state->exception)
            {
                std::rethrow_exception(state->exception);
            }
        }
    };

    export Task<void> WhenAll(std::vector<Task<void>> tasks)
    {
        if (tasks.empty())
        {
            co_return;
        }

        auto state = std::make_shared<WhenAllVoidState>(tasks.size());

        for (std::size_t i = 0; i < tasks.size(); ++i)
        {
            [](Task<void> t, std::shared_ptr<WhenAllVoidState> s) -> FireAndForget
            {
                try
                {
                    co_await t;
                }
                catch (...)
                {
                    std::scoped_lock lock(s->mutex);
                    if (!s->exception)
                    {
                        s->exception = std::current_exception();
                    }
                }

                if (s->remaining.fetch_sub(1, std::memory_order_acq_rel) == 1)
                {
                    std::coroutine_handle<> toResume{nullptr};
                    {
                        std::scoped_lock lock(s->mutex);
                        toResume = s->awaitingCoro;
                    }
                    if (toResume)
                    {
                        ThreadPool::Instance().Submit([toResume] { toResume.resume(); });
                    }
                }
            }(std::move(tasks[i]), state);
        }

        co_await WhenAllVoidAwaiter{state};
    }

    // WhenAny

    export template <typename T>
    struct WhenAnyResult
    {
        std::size_t index;
        T value;
    };

    template <typename T>
    struct WhenAnyState
    {
        std::mutex mutex{};
        std::coroutine_handle<> awaitingCoro{nullptr};
        std::optional<WhenAnyResult<T>> result{std::nullopt};
        std::atomic<bool> completed{false};
        std::exception_ptr exception{nullptr};
    };

    template <typename T>
    struct WhenAnyAwaiter
    {
        std::shared_ptr<WhenAnyState<T>> state;

        bool await_ready() const noexcept
        {
            return state->completed.load(std::memory_order_acquire);
        }

        bool await_suspend(std::coroutine_handle<> h) noexcept
        {
            std::scoped_lock lock(state->mutex);
            if (state->completed.load(std::memory_order_acquire))
            {
                return false;
            }
            state->awaitingCoro = h;
            return true;
        }

        WhenAnyResult<T> await_resume()
        {
            if (state->exception)
            {
                std::rethrow_exception(state->exception);
            }
            return std::move(*(state->result));
        }
    };

    export template <typename T>
    Task<WhenAnyResult<T>> WhenAny(std::vector<Task<T>> tasks)
    {
        if (tasks.empty())
        {
            throw std::invalid_argument("WhenAny requires at least one task.");
        }

        auto state = std::make_shared<WhenAnyState<T>>();

        for (std::size_t i = 0; i < tasks.size(); ++i)
        {
            [](std::size_t index, Task<T> t, std::shared_ptr<WhenAnyState<T>> s) -> FireAndForget
            {
                try
                {
                    T value = co_await t;
                    bool expected = false;
                    if (s->completed.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
                    {
                        std::coroutine_handle<> toResume{nullptr};
                        {
                            std::scoped_lock lock(s->mutex);
                            s->result = WhenAnyResult<T>{.index = index, .value = std::move(value)};
                            toResume = s->awaitingCoro;
                        }
                        if (toResume)
                        {
                            ThreadPool::Instance().Submit([toResume] { toResume.resume(); });
                        }
                    }
                }
                catch (...)
                {
                    bool expected = false;
                    if (s->completed.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
                    {
                        std::coroutine_handle<> toResume{nullptr};
                        {
                            std::scoped_lock lock(s->mutex);
                            s->exception = std::current_exception();
                            toResume = s->awaitingCoro;
                        }
                        if (toResume)
                        {
                            ThreadPool::Instance().Submit([toResume] { toResume.resume(); });
                        }
                    }
                }
            }(i, std::move(tasks[i]), state);
        }

        co_return co_await WhenAnyAwaiter<T>{state};
    }

    // make this al work with std::futures
    export template <typename T>
    struct FutureAwaiter
    {
        std::future<T> m_Future;

        bool await_ready() const noexcept
        {
            return m_Future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
        }

        void await_suspend(std::coroutine_handle<> handle)
        {
            ThreadPool::Instance().Submit([this, handle]() mutable
            {
                m_Future.wait();
                handle.resume();
            });
        }

        T await_resume()
        {
            return m_Future.get();
        }
    };

    export template <typename T>
    auto operator co_await(std::future<T>&& future)
    {
        return FutureAwaiter<T>{std::move(future)};
    }

    template<typename T>
    struct SharedFutureAwaiter {
        std::shared_future<T> m_Future;

        bool await_ready() const noexcept {
            return m_Future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
        }

        void await_suspend(std::coroutine_handle<> handle) {
            ThreadPool::Instance().Submit([fut = m_Future, handle]() mutable {
                fut.wait();
                handle.resume();
            });
        }

        T await_resume() {
            return m_Future.get();
        }
    };

    export template<typename T>
    auto operator co_await(std::shared_future<T> future) {
        return SharedFutureAwaiter<T>{ future };
    }
}

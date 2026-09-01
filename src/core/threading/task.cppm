export module GPP.Core:Threading.Task;

import :Threading.ThreadPool;
import std;
import :Logger;

export namespace GPP
{
    template <typename T>
    class [[nodiscard]] Task;

    namespace Detail
    {
        struct ThreadPoolDispatchAwaiter
        {
            [[nodiscard]] bool await_ready() const noexcept { return false; }

            void await_suspend(std::coroutine_handle<> h) noexcept
            {
                ThreadPool::Instance().Submit([h] { h.resume(); });
            }

            void await_resume() const noexcept
            {
            }
        };

        struct PromiseBase
        {
            std::coroutine_handle<> continuation{nullptr};
            std::mutex mutex{};
            std::condition_variable_any cv{};
            std::exception_ptr exception{nullptr};
            bool completed{false};

            struct FinalAwaiter
            {
                [[nodiscard]] bool await_ready() const noexcept { return false; }

                template <typename PromiseType>
                [[nodiscard]] std::coroutine_handle<> await_suspend(std::coroutine_handle<PromiseType> h) noexcept
                {
                    auto& promise = h.promise();

                    {
                        std::scoped_lock lock(promise.mutex);
                        promise.completed = true;
                    }
                    promise.cv.notify_all();

                    if (promise.continuation)
                    {
                        return promise.continuation;
                    }
                    return std::noop_coroutine();
                }

                void await_resume() const noexcept
                {
                }
            };

            //std::suspend_always initial_suspend() noexcept { return {}; }
            ThreadPoolDispatchAwaiter initial_suspend() noexcept { return {}; }
            FinalAwaiter final_suspend() noexcept { return {}; }
            void unhandled_exception() noexcept { exception = std::current_exception(); }
        };
    }

    template <typename>
    struct is_task : std::false_type
    {
    };

    template <typename U>
    struct is_task<Task<U>> : std::true_type
    {
    };

    template <typename R>
    struct unwrap_task { using type = R; };
    template <typename U>
    struct unwrap_task<Task<U>> { using type = U; };

    struct FireAndForget
    {
        struct promise_type
        {
            FireAndForget get_return_object() noexcept { return {}; }
            std::suspend_never initial_suspend() noexcept { return {}; } // Starts immediately
            std::suspend_never final_suspend() noexcept { return {}; } // Auto-cleans up frame
            void return_void() noexcept
            {
            }

            void unhandled_exception() noexcept
            {
                try
                {
                    std::rethrow_exception(std::current_exception());
                }
                catch (const std::exception& e)
                {
                    Logger::LogError("FireAndForget background exception: {}", e.what());
                }
                catch (...)
                {
                    Logger::LogError("FireAndForget background unknown exception");
                }
            }
        };
    };

    template <typename T>
    class [[nodiscard]] Task
    {
    public:
        using value_type = T;

        struct promise_type : public Detail::PromiseBase
        {
            std::optional<T> value{std::nullopt};

            Task<T> get_return_object()
            {
                return Task<T>{std::coroutine_handle<promise_type>::from_promise(*this)};
            }

            template <typename ValueType>
            void return_value(ValueType&& val) noexcept(std::is_nothrow_constructible_v<T, ValueType>)
            {
                value.emplace(std::forward<ValueType>(val));
            }
        };

        using handle_type = std::coroutine_handle<promise_type>;

        explicit Task(handle_type h) : m_Handle(h)
        {
        }

        ~Task()
        {
            if (m_Handle)
            {
                m_Handle.destroy();
            }
        }

        // Enforce Move-Only Semantics (for std::unique_ptr)
        Task(const Task&) = delete;
        Task& operator=(const Task&) = delete;

        Task(Task&& other) noexcept : m_Handle(std::exchange(other.m_Handle, nullptr))
        {
        }

        Task& operator=(Task&& other) noexcept
        {
            if (this != &other)
            {
                if (m_Handle) m_Handle.destroy();
                m_Handle = std::exchange(other.m_Handle, nullptr);
            }
            return *this;
        }

        T get()
        {
            if (!m_Handle) throw std::runtime_error("Task is empty or has been moved.");

            auto& promise = m_Handle.promise();
            if (!promise.completed)
            {
                // block caller
                std::unique_lock lock(promise.mutex);
                // wait until completed
                promise.cv.wait(lock, [&] { return promise.completed; });
            }

            if (promise.exception)
            {
                std::rethrow_exception(promise.exception);
            }
            return std::move(*promise.value);
        }

        // co_await Integration
        struct Awaiter
        {
            handle_type handle;

            [[nodiscard]] bool await_ready() const noexcept
            {
                return handle.done();
            }

            [[nodiscard]] std::coroutine_handle<> await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept
            {
                auto& promise = handle.promise();
                promise.continuation = awaiting_coroutine;
                return std::noop_coroutine();
            }

            T await_resume()
            {
                auto& promise = handle.promise();
                if (promise.exception)
                {
                    std::rethrow_exception(promise.exception);
                }
                return std::move(*promise.value);
            }
        };

        [[nodiscard]] auto operator co_await() const noexcept
        {
            return Awaiter{m_Handle};
        }

        template <typename F>
        auto then(F&& f) const -> Task<typename unwrap_task<std::invoke_result_t<F, T>>::type>
        {
            using Ret = std::invoke_result_t<F, T>;
            using ResultValue = typename unwrap_task<Ret>::type;

            if constexpr (std::is_void_v<ResultValue>)
            {
                T value = co_await *this;
                if constexpr (is_task<Ret>::value)
                {
                    co_await std::forward<F>(f)(std::move(value));
                }
                else
                {
                    std::forward<F>(f)(std::move(value));
                }
                co_return;
            }
            else
            {
                T value = co_await *this;
                if constexpr (is_task<Ret>::value)
                {
                    co_return co_await std::forward<F>(f)(std::move(value));
                }
                else
                {
                    co_return std::forward<F>(f)(std::move(value));
                }
            }
        }

    private:
        handle_type m_Handle{nullptr};
    };

    template <>
    class [[nodiscard]] Task<void>
    {
    public:
        using value_type = void;

        struct promise_type : public Detail::PromiseBase
        {
            Task<void> get_return_object()
            {
                return Task<void>{std::coroutine_handle<promise_type>::from_promise(*this)};
            }

            void return_void() noexcept
            {
            }
        };

        using handle_type = std::coroutine_handle<promise_type>;

        explicit Task(handle_type h) : m_Handle(h)
        {
        }

        ~Task()
        {
            if (m_Handle) m_Handle.destroy();
        }

        Task(const Task&) = delete;
        Task& operator=(const Task&) = delete;

        Task(Task&& other) noexcept : m_Handle(std::exchange(other.m_Handle, nullptr))
        {
        }

        Task& operator=(Task&& other) noexcept
        {
            if (this != &other)
            {
                if (m_Handle) m_Handle.destroy();
                m_Handle = std::exchange(other.m_Handle, nullptr);
            }
            return *this;
        }

        void get()
        {
            if (!m_Handle) throw std::runtime_error("Task is empty or has been moved.");

            auto& promise = m_Handle.promise();
            if (!promise.completed)
            {
                //m_Handle.resume();
                std::unique_lock lock(promise.mutex);
                promise.cv.wait(lock, [&] { return promise.completed; });
            }

            if (promise.exception)
            {
                std::rethrow_exception(promise.exception);
            }
        }

        struct Awaiter
        {
            handle_type handle;

            [[nodiscard]] bool await_ready() const noexcept
            {
                return handle.done();
            }

            [[nodiscard]] std::coroutine_handle<> await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept
            {
                auto& promise = handle.promise();
                promise.continuation = awaiting_coroutine;
                return std::noop_coroutine();
            }

            void await_resume()
            {
                auto& promise = handle.promise();
                if (promise.exception)
                {
                    std::rethrow_exception(promise.exception);
                }
            }
        };

        [[nodiscard]] auto operator co_await() const noexcept
        {
            return Awaiter{m_Handle};
        }

        template <typename F>
        auto then(F&& f) const -> Task<typename unwrap_task<std::invoke_result_t<F>>::type>
        {
            using Ret = std::invoke_result_t<F>;
            using ResultValue = typename unwrap_task<Ret>::type;

            co_await *this;

            if constexpr (std::is_void_v<ResultValue>)
            {
                if constexpr (is_task<Ret>::value)
                {
                    co_await std::forward<F>(f)();
                }
                else
                {
                    std::forward<F>(f)();
                }
                co_return;
            }
            else
            {
                if constexpr (is_task<Ret>::value)
                {
                    co_return co_await std::forward<F>(f)();
                }
                else
                {
                    co_return std::forward<F>(f)();
                }
            }
        }

    private:
        handle_type m_Handle{nullptr};
    };
}

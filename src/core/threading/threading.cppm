export module GPP.Core:Threading;

import std;
import :Types;

namespace GPP {
    export class ThreadPool {
    public:
        explicit ThreadPool(std::size_t workerCount = std::thread::hardware_concurrency());
        ~ThreadPool();

        static ThreadPool* Instance();

        ThreadPool(const ThreadPool&) = delete;
        ThreadPool& operator=(const ThreadPool&) = delete;
        ThreadPool(ThreadPool&&) = delete;
        ThreadPool& operator=(ThreadPool&&) = delete;

        void Submit(std::move_only_function<void()> task);

        std::size_t WorkerCount() const;

    private:
        void WorkerLoop(std::stop_token stopToken);

        std::vector<std::jthread> m_Workers{};
        std::queue<std::move_only_function<void()>> m_Tasks{};
        mutable std::mutex m_QueueMutex{};
        std::condition_variable_any m_QueueCondition{};
        bool m_Stopping{false};
    };

    struct ResumeOnAwaiter {
        ThreadPool* pool{nullptr};
        bool await_ready() const noexcept;
        void await_suspend(std::coroutine_handle<> handle) const;
        void await_resume() const noexcept;
    };

    export auto ResumeOn(ThreadPool& pool);
}

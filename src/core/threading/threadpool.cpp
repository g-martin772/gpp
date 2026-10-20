module GPP.Core;
import :Threading.ThreadPool;

import std;

namespace GPP {
    ThreadPool::ThreadPool(std::size_t workerCount) {
        if (workerCount == 0) {
            workerCount = 1;
        }

        m_Workers.reserve(workerCount);
        for (std::size_t i = 0; i < workerCount; ++i) {
            m_Workers.emplace_back([this](std::stop_token stopToken) { WorkerLoop(stopToken); });
        }
    }

    ThreadPool::~ThreadPool() {
        {
            std::scoped_lock lock(m_QueueMutex);
            m_Stopping = true;
        }
        m_QueueCondition.notify_all();
    }

    ThreadPool& ThreadPool::Instance()
    {
        static ThreadPool instance;
        return instance;
    }

    void ThreadPool::Submit(std::move_only_function<void()> task) {
        {
            std::scoped_lock lock(m_QueueMutex);
            if (m_Stopping) {
                throw std::runtime_error("ThreadPool is stopping");
            }
            m_Tasks.push(std::move(task));
        }
        m_QueueCondition.notify_one();
    }

    std::size_t ThreadPool::WorkerCount() const {
        return m_Workers.size();
    }

    void ThreadPool::WorkerLoop(std::stop_token stopToken) {
        while (true) {
            std::move_only_function<void()> task;
            {
                std::unique_lock lock(m_QueueMutex);
                m_QueueCondition.wait(lock, stopToken, [this, stopToken] {
                    return m_Stopping || !m_Tasks.empty();
                });

                if ((m_Stopping || stopToken.stop_requested()) && m_Tasks.empty()) {
                    break;
                }

                if (!m_Tasks.empty()) {
                    task = std::move(m_Tasks.front());
                    m_Tasks.pop();
                }
            }

            if (task) {
                try {
                    task();
                } catch (const std::exception& e) {
                    Logger::LogError("ThreadPool Worker caught exception: {}", e.what());
                } catch (...) {
                    Logger::LogError("ThreadPool Worker caught unknown exception.");
                }
            }
        }
    }

    bool ResumeOnAwaiter::await_ready() const noexcept
    {
        return false;
    }

    void ResumeOnAwaiter::await_suspend(std::coroutine_handle<> handle) const
    {
        pool->Submit([handle] { handle.resume(); });
    }

    void ResumeOnAwaiter::await_resume() const noexcept
    {
    }

    ResumeOnAwaiter ResumeOn(ThreadPool& pool) {
        return ResumeOnAwaiter{.pool = &pool};
    }

    void ParallelFor(const std::size_t count, const std::size_t maxWorkers, const std::function<void(std::size_t)>& body) {
        if (count == 0) return;
        struct Shared {
            std::atomic<std::size_t> Next{0};
            std::mutex Mutex;
            std::condition_variable Done;
            std::size_t Finished = 0;
            std::exception_ptr Error;
        };
        auto shared = std::make_shared<Shared>();

        const auto work = [shared, count, &body] {
            while (true) {
                const auto index = shared->Next.fetch_add(1);
                if (index >= count) return;
                std::exception_ptr error;
                try {
                    body(index);
                } catch (...) {
                    error = std::current_exception();
                }
                std::scoped_lock lock(shared->Mutex);
                if (error && !shared->Error) shared->Error = error;
                if (++shared->Finished == count) shared->Done.notify_all();
            }
        };

        const auto helpers = std::min({std::max<std::size_t>(maxWorkers, 1), count, ThreadPool::Instance().WorkerCount() + 1}) - 1;
        for (std::size_t i = 0; i < helpers; ++i) {
            try {
                ThreadPool::Instance().Submit(work);
            } catch (const std::exception&) {
                break;
            }
        }
        work();
        std::unique_lock lock(shared->Mutex);
        shared->Done.wait(lock, [&] { return shared->Finished == count; });
        if (shared->Error) std::rethrow_exception(shared->Error);
    }
}

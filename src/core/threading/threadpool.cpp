module GPP.Core;
import :Threading;

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
}

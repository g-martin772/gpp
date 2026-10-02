module GPP.Core;

import :HotReload.FileWatcher;
import std;

namespace GPP
{
    struct FileWatcher::State
    {
        struct WatchEntry
        {
            std::filesystem::path path;
            std::filesystem::file_time_type lastWriteTime{};
            bool existed = false;
        };

        std::shared_ptr<IFileSystem> fileSystem;
        std::shared_ptr<EventDispatcher> dispatcher;
        std::shared_ptr<Logger> logger;
        std::mutex mutex;
        std::unordered_map<std::size_t, WatchEntry> entries;
        std::size_t nextId = 1;
        std::chrono::milliseconds interval{250};
        std::stop_source stopSource;
        std::optional<std::stop_callback<std::function<void()>>> externalStopCallback;
        bool running = false;
    };

    namespace
    {
        void PollStateUnconditional(const std::shared_ptr<FileWatcher::State>& state)
        {
            std::vector<FileChangedEvent> changes;
            {
                std::scoped_lock lock(state->mutex);
                for (auto& [id, entry] : state->entries)
                {
                    std::error_code error;
                    const bool exists = std::filesystem::exists(entry.path, error);
                    const auto writeTime = exists
                                                ? std::filesystem::last_write_time(entry.path, error)
                                                : std::filesystem::file_time_type{};
                    if (!error && (exists != entry.existed ||
                                   (exists && writeTime != entry.lastWriteTime)))
                    {
                        entry.existed = exists;
                        entry.lastWriteTime = writeTime;
                        if (exists)
                        {
                            changes.push_back({entry.path, writeTime});
                        }
                    }
                }
            }
            for (const auto& change : changes)
            {
                state->dispatcher->Publish(change);
            }
        }

        void PollState(const std::shared_ptr<FileWatcher::State>& state)
        {
            {
                std::scoped_lock lock(state->mutex);
                if (!state->running)
                {
                    return;
                }
            }
            PollStateUnconditional(state);
        }

        void SchedulePoll(const std::shared_ptr<FileWatcher::State>& state)
        {
            std::chrono::milliseconds interval;
            std::stop_token token;
            {
                std::scoped_lock lock(state->mutex);
                if (!state->running)
                {
                    return;
                }
                interval = state->interval;
                token = state->stopSource.get_token();
            }
            TimerSystem::Instance().Schedule(interval, token, [state]
            {
                PollState(state);
                SchedulePoll(state);
            });
        }
    }

    FileWatcher::FileWatcher(std::shared_ptr<IFileSystem> fileSystem,
                             std::shared_ptr<EventDispatcher> dispatcher,
                             std::shared_ptr<Logger> logger)
        : m_State(std::make_shared<State>())
    {
        m_State->fileSystem = std::move(fileSystem);
        m_State->dispatcher = std::move(dispatcher);
        m_State->logger = std::move(logger);
        if (!m_State->fileSystem || !m_State->dispatcher)
        {
            throw std::invalid_argument("FileWatcher requires a file system and dispatcher.");
        }
    }

    FileWatcher::~FileWatcher()
    {
        if (!m_State)
        {
            return;
        }
        m_State->stopSource.request_stop();
        std::scoped_lock lock(m_State->mutex);
        m_State->running = false;
    }

    std::size_t FileWatcher::Watch(const std::filesystem::path& path) const
    {
        const auto resolved = m_State->fileSystem->ResolvePath(path);
        std::error_code error;
        const bool exists = std::filesystem::exists(resolved, error);
        const auto writeTime = exists
                                   ? std::filesystem::last_write_time(resolved, error)
                                   : std::filesystem::file_time_type{};
        std::scoped_lock lock(m_State->mutex);
        const auto id = m_State->nextId++;
        m_State->entries.emplace(id, State::WatchEntry{
                                      .path = resolved,
                                      .lastWriteTime = writeTime,
                                      .existed = exists && !error
                                  });
        return id;
    }

    void FileWatcher::Unwatch(const std::size_t watchId) const
    {
        std::scoped_lock lock(m_State->mutex);
        m_State->entries.erase(watchId);
    }

    void FileWatcher::PollNow() const
    {
        PollStateUnconditional(m_State);
    }

    Task<void> FileWatcher::StartAsync(std::chrono::milliseconds interval, std::stop_token stopToken)
    {
        {
            std::scoped_lock lock(m_State->mutex);
            m_State->interval = std::max(interval, std::chrono::milliseconds(1));
            m_State->stopSource = std::stop_source{};
            m_State->externalStopCallback.reset();
            if (stopToken.stop_possible())
            {
                if (stopToken.stop_requested())
                {
                    co_return;
                }
                m_State->externalStopCallback.emplace(stopToken, [state = m_State]
                {
                    std::scoped_lock lock(state->mutex);
                    state->running = false;
                    state->stopSource.request_stop();
                });
            }
            m_State->running = true;
        }
        SchedulePoll(m_State);
        co_return;
    }

    Task<void> FileWatcher::StopAsync() const
    {
        {
            std::scoped_lock lock(m_State->mutex);
            m_State->running = false;
            m_State->stopSource.request_stop();
            m_State->externalStopCallback.reset();
        }
        co_return;
    }

    bool FileWatcher::IsRunning() const noexcept
    {
        std::scoped_lock lock(m_State->mutex);
        return m_State->running;
    }
}

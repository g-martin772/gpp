module;

#include <poll.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <unistd.h>
#include <pthread.h>

module GPP.Graphics;

import std;
import GPP.Core;
import :Shader;

namespace GPP
{
    struct ShaderFileWatcher::State
    {
        struct WatchEntry
        {
            std::filesystem::path path;
            std::filesystem::path normalized;
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
        bool attached = false;
    };

    namespace
    {
        using State = ShaderFileWatcher::State;
        using Change = std::pair<ShaderFileChangedEvent, std::shared_ptr<EventDispatcher>>;

        constexpr auto kDebounce = std::chrono::milliseconds(100);
        constexpr auto kMaxDebounce = std::chrono::milliseconds(500);

        void Scan(State& state, const std::set<std::filesystem::path>* filter, std::vector<Change>& changes)
        {
            std::scoped_lock lock(state.mutex);
            if (!state.running)
            {
                return;
            }
            for (auto& [id, entry] : state.entries)
            {
                if (filter && !filter->contains(entry.normalized))
                {
                    continue;
                }
                std::error_code error;
                const bool exists = std::filesystem::exists(entry.path, error);
                const auto writeTime = exists
                                            ? std::filesystem::last_write_time(entry.path, error)
                                            : std::filesystem::file_time_type{};
                if (!error && (exists != entry.existed || (exists && writeTime != entry.lastWriteTime)))
                {
                    entry.existed = exists;
                    entry.lastWriteTime = writeTime;
                    if (exists)
                    {
                        changes.push_back({ShaderFileChangedEvent{entry.path, writeTime}, state.dispatcher});
                    }
                }
            }
        }

        void Publish(const std::vector<Change>& changes)
        {
            std::set<std::pair<const EventDispatcher*, std::filesystem::path>> sent;
            for (const auto& [event, dispatcher] : changes)
            {
                if (sent.emplace(dispatcher.get(), event.path.lexically_normal()).second)
                {
                    dispatcher->Publish(event);
                }
            }
        }

        class WatchHub
        {
        public:
            static WatchHub& Instance()
            {
                static WatchHub hub;
                return hub;
            }

            void Attach(const std::shared_ptr<State>& state)
            {
                {
                    std::scoped_lock lock(m_Mutex);
                    if (!state->attached)
                    {
                        state->attached = true;
                        m_States.push_back(state);
                    }
                    if (!m_Thread.joinable())
                    {
                        m_Thread = std::jthread([this](std::stop_token token) { Run(token); });
                    }
                }
                Wake();
            }

            void WatchDirectory(const std::filesystem::path& directory)
            {
                const auto key = directory.lexically_normal();
                std::scoped_lock lock(m_Mutex);
                if (m_Directories.contains(key))
                {
                    return;
                }
                int wd = -1;
                if (m_Inotify >= 0)
                {
                    wd = inotify_add_watch(m_Inotify, key.c_str(),
                                           IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE | IN_MODIFY | IN_ATTRIB);
                }
                m_Directories.emplace(key, wd);
                if (wd >= 0)
                {
                    m_WatchDirectories[wd] = key;
                }
                else
                {
                    m_NeedsPolling = true;
                }
            }

            void Wake()
            {
                if (m_Wake >= 0)
                {
                    const std::uint64_t one = 1;
                    (void)!write(m_Wake, &one, sizeof(one));
                }
            }

        private:
            WatchHub()
            {
                m_Inotify = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
                m_Wake = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
                m_NeedsPolling = m_Inotify < 0;
            }

            ~WatchHub()
            {
                {
                    std::scoped_lock lock(m_Mutex);
                    if (m_Thread.joinable())
                    {
                        m_Thread.request_stop();
                    }
                }
                Wake();
                if (m_Thread.joinable())
                {
                    m_Thread.join();
                }
                if (m_Inotify >= 0) close(m_Inotify);
                if (m_Wake >= 0) close(m_Wake);
            }

            std::chrono::milliseconds FallbackInterval()
            {
                std::chrono::milliseconds interval{1000};
                for (const auto& weak : m_States)
                {
                    if (const auto state = weak.lock())
                    {
                        std::scoped_lock lock(state->mutex);
                        interval = std::min(interval, state->interval);
                    }
                }
                return std::max(interval, std::chrono::milliseconds(50));
            }

            void ReadEvents(bool& overflow)
            {
                alignas(inotify_event) char buffer[8192];
                while (true)
                {
                    const auto count = read(m_Inotify, buffer, sizeof(buffer));
                    if (count <= 0)
                    {
                        return;
                    }
                    std::scoped_lock lock(m_Mutex);
                    for (std::ptrdiff_t offset = 0; offset < count;)
                    {
                        const auto* event = reinterpret_cast<const inotify_event*>(buffer + offset);
                        offset += static_cast<std::ptrdiff_t>(sizeof(inotify_event) + event->len);
                        if (event->mask & IN_Q_OVERFLOW)
                        {
                            overflow = true;
                            continue;
                        }
                        const auto directory = m_WatchDirectories.find(event->wd);
                        if (directory == m_WatchDirectories.end() || event->len == 0)
                        {
                            continue;
                        }
                        m_Pending.insert((directory->second / event->name).lexically_normal());
                    }
                }
            }

            void Flush(std::set<std::filesystem::path> pending, bool all)
            {
                std::vector<std::shared_ptr<State>> states;
                {
                    std::scoped_lock lock(m_Mutex);
                    std::erase_if(m_States, [&](const std::weak_ptr<State>& weak)
                    {
                        if (const auto state = weak.lock())
                        {
                            states.push_back(state);
                            return false;
                        }
                        return true;
                    });
                }
                std::vector<Change> changes;
                for (const auto& state : states)
                {
                    Scan(*state, all ? nullptr : &pending, changes);
                }
                Publish(changes);
            }

            void Run(const std::stop_token token)
            {
                pthread_setname_np(pthread_self(), "gpp-shaderwatch");
                using Clock = std::chrono::steady_clock;
                std::optional<Clock::time_point> first;
                std::optional<Clock::time_point> last;
                bool overflow = false;
                while (!token.stop_requested())
                {
                    int timeout = -1;
                    bool polling;
                    {
                        std::scoped_lock lock(m_Mutex);
                        polling = m_NeedsPolling;
                        if (!m_Pending.empty() || overflow)
                        {
                            const auto deadline = std::min(*last + kDebounce, *first + kMaxDebounce);
                            timeout = static_cast<int>(std::max<std::int64_t>(
                                0, std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count()));
                        }
                        else if (polling)
                        {
                            timeout = static_cast<int>(FallbackInterval().count());
                        }
                    }
                    pollfd fds[2] = {{m_Inotify, POLLIN, 0}, {m_Wake, POLLIN, 0}};
                    const int ready = poll(fds, 2, timeout);
                    if (token.stop_requested())
                    {
                        break;
                    }
                    if (ready > 0 && (fds[1].revents & POLLIN))
                    {
                        std::uint64_t value;
                        (void)!read(m_Wake, &value, sizeof(value));
                    }
                    if (ready > 0 && (fds[0].revents & POLLIN))
                    {
                        ReadEvents(overflow);
                        const auto now = Clock::now();
                        if (!first) first = now;
                        last = now;
                    }

                    std::set<std::filesystem::path> pending;
                    bool flush = false;
                    {
                        std::scoped_lock lock(m_Mutex);
                        if (!m_Pending.empty() || overflow)
                        {
                            if (!first) { first = Clock::now(); last = first; }
                            const auto deadline = std::min(*last + kDebounce, *first + kMaxDebounce);
                            if (Clock::now() >= deadline)
                            {
                                pending.swap(m_Pending);
                                flush = true;
                            }
                        }
                    }
                    if (flush)
                    {
                        Flush(std::move(pending), overflow);
                        overflow = false;
                        first.reset();
                        last.reset();
                    }
                    else if (polling && ready == 0)
                    {
                        Flush({}, true);
                    }
                }
            }

            std::mutex m_Mutex;
            int m_Inotify = -1;
            int m_Wake = -1;
            bool m_NeedsPolling = false;
            std::map<std::filesystem::path, int> m_Directories;
            std::unordered_map<int, std::filesystem::path> m_WatchDirectories;
            std::vector<std::weak_ptr<State>> m_States;
            std::set<std::filesystem::path> m_Pending;
            std::jthread m_Thread;
        };
    }

    ShaderFileWatcher::ShaderFileWatcher(std::shared_ptr<IFileSystem> fileSystem,
                                         std::shared_ptr<EventDispatcher> dispatcher,
                                         std::shared_ptr<Logger> logger)
        : m_State(std::make_shared<State>())
    {
        m_State->fileSystem = std::move(fileSystem);
        m_State->dispatcher = std::move(dispatcher);
        m_State->logger = std::move(logger);
        if (!m_State->fileSystem || !m_State->dispatcher)
        {
            throw std::invalid_argument("ShaderFileWatcher requires a file system and dispatcher.");
        }
    }

    ShaderFileWatcher::~ShaderFileWatcher()
    {
        m_State->stopSource.request_stop();
        std::scoped_lock lock(m_State->mutex);
        m_State->running = false;
    }

    std::size_t ShaderFileWatcher::Watch(const std::filesystem::path& path)
    {
        const auto resolved = m_State->fileSystem->ResolvePath(path);
        std::error_code error;
        const bool exists = std::filesystem::exists(resolved, error);
        const auto writeTime = exists
                                   ? std::filesystem::last_write_time(resolved, error)
                                   : std::filesystem::file_time_type{};
        std::size_t id;
        {
            std::scoped_lock lock(m_State->mutex);
            id = m_State->nextId++;
            m_State->entries.emplace(id, State::WatchEntry{
                                          .path = resolved,
                                          .normalized = resolved.lexically_normal(),
                                          .lastWriteTime = writeTime,
                                          .existed = exists && !error
                                      });
        }
        WatchHub::Instance().WatchDirectory(resolved.parent_path());
        return id;
    }

    void ShaderFileWatcher::Unwatch(const std::size_t watchId)
    {
        std::scoped_lock lock(m_State->mutex);
        m_State->entries.erase(watchId);
    }

    void ShaderFileWatcher::PollNow()
    {
        std::vector<Change> changes;
        Scan(*m_State, nullptr, changes);
        Publish(changes);
    }

    Task<void> ShaderFileWatcher::StartAsync(std::chrono::milliseconds interval,
                                             std::stop_token stopToken)
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
        WatchHub::Instance().Attach(m_State);
        co_return;
    }

    Task<void> ShaderFileWatcher::StopAsync()
    {
        {
            std::scoped_lock lock(m_State->mutex);
            m_State->running = false;
            m_State->stopSource.request_stop();
            m_State->externalStopCallback.reset();
        }
        co_return;
    }

    bool ShaderFileWatcher::IsRunning() const noexcept
    {
        std::scoped_lock lock(m_State->mutex);
        return m_State->running;
    }
}

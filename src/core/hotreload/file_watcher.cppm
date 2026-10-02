export module GPP.Core:HotReload.FileWatcher;

import std;
import :DI.Service;
import :IO.File;
import :Events;
import :Logger;
import :Threading;

namespace GPP
{
    export struct FileChangedEvent
    {
        std::filesystem::path path;
        std::filesystem::file_time_type lastWriteTime{};
    };

    export class FileWatcher : public IService
    {
    public:
        struct State;

        using Dependencies = std::tuple<IFileSystem, EventDispatcher, Logger>;

        FileWatcher(std::shared_ptr<IFileSystem> fileSystem,
                    std::shared_ptr<EventDispatcher> dispatcher,
                    std::shared_ptr<Logger> logger);
        ~FileWatcher() override;

        FileWatcher(const FileWatcher&) = delete;
        FileWatcher& operator=(const FileWatcher&) = delete;
        FileWatcher(FileWatcher&&) noexcept = default;
        FileWatcher& operator=(FileWatcher&&) noexcept = default;

        std::size_t Watch(const std::filesystem::path& path) const;
        void Unwatch(std::size_t watchId) const;

        void PollNow() const;

        Task<void> StartAsync(std::chrono::milliseconds interval = std::chrono::milliseconds(250),
                              std::stop_token stopToken = {});
        Task<void> StopAsync() const;
        [[nodiscard]] bool IsRunning() const noexcept;

    private:
        std::shared_ptr<State> m_State;
    };
}

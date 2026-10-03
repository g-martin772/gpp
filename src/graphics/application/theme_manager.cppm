export module GPP.Graphics:Application.ThemeManager;

import std;
import GPP.Core;
import :Application.Theme;
import :RenderConfig;

namespace GPP
{
    export struct ThemeDescription
    {
        std::optional<std::type_index> ClassType;
        std::optional<std::filesystem::path> LibraryPath;

        std::optional<bool> EnableHotReload;
        std::optional<std::chrono::milliseconds> PollingInterval;

        std::string CreateSymbol{"GPP_CreateHotReloadThemeModule"};
        std::string DestroySymbol{"GPP_DestroyHotReloadThemeModule"};
        std::string AbiVersionSymbol{"GPP_HotReloadThemeModuleAbiVersion"};
    };

    export struct ThemeDescriptionHolder : public IService
    {
        std::optional<ThemeDescription> Description;
    };

    export struct ThemeStagedEvent
    {
    };

    export class ThemeManager final : public IHostedService
    {
    public:
        using Dependencies = std::tuple<ThemeDescriptionHolder, ThemeProxy, EventDispatcher,
                                         IFileSystem, Logger, ServiceProvider>;

        ThemeManager(std::shared_ptr<ThemeDescriptionHolder> description,
                     std::shared_ptr<ThemeProxy> proxy,
                     std::shared_ptr<EventDispatcher> dispatcher,
                     std::shared_ptr<IFileSystem> fileSystem,
                     std::shared_ptr<Logger> logger,
                     std::shared_ptr<ServiceProvider> provider);
        ~ThemeManager() override;

        ThemeManager(const ThemeManager&) = delete;
        ThemeManager& operator=(const ThemeManager&) = delete;

        Task<void> StartAsync(std::stop_token stopToken) override;
        Task<void> StopAsync() override;

        [[nodiscard]] HotReloadState GetState() const;
        [[nodiscard]] std::string LastError() const;

    private:
        void LoadInitial();
        void QueueReload();
        void InstallStaged();

        std::shared_ptr<ThemeDescriptionHolder> m_DescriptionHolder;
        std::shared_ptr<ThemeProxy> m_Proxy;
        std::shared_ptr<EventDispatcher> m_Dispatcher;
        std::shared_ptr<IFileSystem> m_FileSystem;
        std::shared_ptr<Logger> m_Logger;
        std::shared_ptr<ServiceProvider> m_Provider;

        std::shared_ptr<Theme> m_ClassTheme;
        std::unique_ptr<HotReloadAsset> m_Asset;
        FileWatcher m_Watcher;
        EventSubscription m_FileSubscription;
        EventSubscription m_StagedSubscription;
        std::atomic<bool> m_ReloadInFlight{false};
    };
}

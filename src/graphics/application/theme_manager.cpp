module GPP.Graphics;

import :Application.ThemeManager;
import std;

namespace GPP
{
    ThemeManager::ThemeManager(
        std::shared_ptr<ThemeDescriptionHolder> description,
        std::shared_ptr<ThemeProxy> proxy,
        std::shared_ptr<EventDispatcher> dispatcher,
        std::shared_ptr<IFileSystem> fileSystem,
        std::shared_ptr<Logger> logger,
        std::shared_ptr<ServiceProvider> provider)
        : m_DescriptionHolder(std::move(description)),
          m_Proxy(std::move(proxy)),
          m_Dispatcher(std::move(dispatcher)),
          m_FileSystem(std::move(fileSystem)),
          m_Logger(std::move(logger)),
          m_Provider(std::move(provider)),
          m_Watcher(m_FileSystem, m_Dispatcher, m_Logger)
    {
        if (!m_DescriptionHolder || !m_Proxy || !m_Dispatcher || !m_FileSystem || !m_Logger || !m_Provider)
        {
            throw std::invalid_argument("ThemeManager requires all of its dependencies.");
        }
    }

    ThemeManager::~ThemeManager() = default;

    void ThemeManager::LoadInitial()
    {
        const auto& description = *m_DescriptionHolder->Description;
        if (description.ClassType)
        {
            m_ClassTheme = m_Provider->GetServiceByTypeIndex<Theme>(*description.ClassType);
            if (!m_ClassTheme)
            {
                m_Logger->Error("Theme class was not registered with the service container.");
                return;
            }
            m_Proxy->SwapActive(m_ClassTheme.get());
            m_Logger->Info("Theme loaded (in-process class).");
            m_Dispatcher->Publish(ThemeStagedEvent{});
            return;
        }

        if (m_Asset->Load(*m_Provider))
        {
            auto* theme = static_cast<Theme*>(m_Asset->GetInstance().instance);
            m_Proxy->SwapActive(theme);
            m_Logger->Info("Theme loaded from {}", description.LibraryPath->string());
            m_Dispatcher->Publish(ThemeStagedEvent{});
        }
        else
        {
            m_Logger->Error("Theme failed to load: {}", m_Asset->LastError());
        }
    }

    void ThemeManager::QueueReload()
    {
        bool expected = false;
        if (!m_ReloadInFlight.compare_exchange_strong(expected, true))
        {
            return;
        }

        const auto staged = m_Asset->Stage(*m_Provider);
        if (!staged)
        {
            m_Logger->Error("Theme failed to reload: {}", m_Asset->LastError());
            m_ReloadInFlight.store(false);
            return;
        }

        m_Logger->Info("Theme staged generation {}; installing on the render thread.", staged.generation);
        m_Dispatcher->Publish(ThemeStagedEvent{});
    }

    void ThemeManager::InstallStaged()
    {
        try
        {
            auto retired = m_Asset->Commit();
            auto* theme = static_cast<Theme*>(m_Asset->GetInstance().instance);
            m_Proxy->SwapActive(theme);
            m_Logger->Info("Theme reloaded (generation {}).", m_Asset->GetInstance().generation);
            m_Dispatcher->Publish(ThemeChangedEvent{});
        }
        catch (const std::exception& exception)
        {
            m_Logger->Error("Theme failed to install a staged reload: {}", exception.what());
        }
        m_ReloadInFlight.store(false);
    }

    Task<void> ThemeManager::StartAsync(std::stop_token stopToken)
    {
        if (!m_DescriptionHolder->Description)
        {
            co_return;
        }
        co_await ResumeOn(ThreadPool::Instance());
        const auto& description = *m_DescriptionHolder->Description;

        if (description.LibraryPath)
        {
            HotReloadAssetDescription assetDescription{
                .libraryPath = *description.LibraryPath,
                .createSymbol = description.CreateSymbol,
                .destroySymbol = description.DestroySymbol,
                .abiVersionSymbol = description.AbiVersionSymbol,
            };
            m_Asset = std::make_unique<HotReloadAsset>(
                "theme", assetDescription, m_FileSystem, m_Dispatcher, m_Logger);
        }

        LoadInitial();

        m_StagedSubscription = m_Dispatcher->Subscribe<ThemeStagedEvent>(
            [this](const ThemeStagedEvent&)
            {
                if (m_Asset)
                {
                    InstallStaged();
                }
            },
            EventDelivery::Async, EventTarget::Render);

        if (description.LibraryPath && description.EnableHotReload.value_or(false))
        {
            const auto watchedPath = *description.LibraryPath;
            m_FileSubscription = m_Dispatcher->Subscribe<FileChangedEvent>(
                [this, watchedPath](const FileChangedEvent& event)
                {
                    std::error_code error;
                    const auto changed = std::filesystem::weakly_canonical(event.path, error);
                    const auto watched = std::filesystem::weakly_canonical(
                        m_FileSystem->ResolvePath(watchedPath), error);
                    if (changed == watched)
                    {
                        QueueReload();
                    }
                },
                EventDelivery::Async, EventTarget::ThreadPool);

            const auto interval = description.PollingInterval.value_or(std::chrono::milliseconds(300));
            m_Watcher.Watch(watchedPath);
            co_await m_Watcher.StartAsync(interval, stopToken);
            m_Logger->Info("Theme watching {} for changes (every {} ms).",
                           watchedPath.string(), interval.count());
        }
        co_return;
    }

    Task<void> ThemeManager::StopAsync()
    {
        m_FileSubscription.Reset();
        m_StagedSubscription.Reset();
        co_await m_Watcher.StopAsync();
        co_return;
    }

    HotReloadState ThemeManager::GetState() const
    {
        return m_Asset ? m_Asset->GetState() : HotReloadState::Unloaded;
    }

    std::string ThemeManager::LastError() const
    {
        return m_Asset ? m_Asset->LastError() : std::string{};
    }
}

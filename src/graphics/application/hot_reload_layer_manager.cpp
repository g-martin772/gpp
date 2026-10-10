module GPP.Graphics;

import :Application.HotReloadLayerManager;
import std;

namespace GPP
{
    struct HotReloadLayerManager::Entry
    {
        HotReloadLayerDescription Description;
        std::shared_ptr<HotReloadLayerProxy> Proxy;
        std::unique_ptr<HotReloadAsset> Asset;
        FileWatcher Watcher;
        EventSubscription FileSubscription;
        EventSubscription StagedSubscription;
        std::atomic<bool> ReloadInFlight{false};

        Entry(HotReloadLayerDescription description, std::shared_ptr<HotReloadLayerProxy> proxy,
              std::unique_ptr<HotReloadAsset> asset, const std::shared_ptr<IFileSystem>& fileSystem,
              const std::shared_ptr<EventDispatcher>& dispatcher, const std::shared_ptr<Logger>& logger)
            : Description(std::move(description)), Proxy(std::move(proxy)), Asset(std::move(asset)),
              Watcher(fileSystem, dispatcher, logger)
        {
        }
    };

    struct HotReloadLayerManager::StagedEvent
    {
        Entry* entry = nullptr;
    };

    HotReloadLayerManager::HotReloadLayerManager(
        std::shared_ptr<HotReloadLayerRegistrations> registrations,
        std::shared_ptr<LayerHotReloadOptions> options,
        std::shared_ptr<EventDispatcher> dispatcher,
        std::shared_ptr<IFileSystem> fileSystem,
        std::shared_ptr<Logger> logger,
        std::shared_ptr<ServiceProvider> provider)
        : m_Registrations(std::move(registrations)),
          m_Options(std::move(options)),
          m_Dispatcher(std::move(dispatcher)),
          m_FileSystem(std::move(fileSystem)),
          m_Logger(std::move(logger)),
          m_Provider(std::move(provider))
    {
        if (!m_Registrations || !m_Options || !m_Dispatcher || !m_FileSystem || !m_Logger || !m_Provider)
        {
            throw std::invalid_argument("HotReloadLayerManager requires all of its dependencies.");
        }
    }

    HotReloadLayerManager::~HotReloadLayerManager() = default;

    void HotReloadLayerManager::LoadInitial(Entry& entry) const
    {
        if (entry.Asset->Load(*m_Provider))
        {
            auto* layer = static_cast<HotReloadableLayer*>(entry.Asset->GetInstance().instance);
            entry.Proxy->SwapActive(layer);
            m_Logger->Info("Hot-reload layer '{}' loaded from {}",
                           entry.Description.Id, entry.Description.LibraryPath.string());
        }
        else
        {
            m_Logger->Error("Hot-reload layer '{}' failed to load: {}",
                            entry.Description.Id, entry.Asset->LastError());
        }
    }

    void HotReloadLayerManager::QueueReload(Entry& entry) const
    {
        bool expected = false;
        if (!entry.ReloadInFlight.compare_exchange_strong(expected, true))
        {
            return;
        }

        constexpr int kMaxAttempts = 6;
        HotReloadInstanceHandle staged;
        for (int attempt = 1; attempt <= kMaxAttempts; ++attempt)
        {
            staged = entry.Asset->Stage(*m_Provider);
            if (staged)
            {
                break;
            }
            m_Logger->Error("Hot-reload layer '{}' failed to reload (attempt {}/{}): {}",
                            entry.Description.Id, attempt, kMaxAttempts, entry.Asset->LastError());
            if (attempt < kMaxAttempts)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(250 * attempt));
            }
        }
        if (!staged)
        {
            m_Logger->Error("Hot-reload layer '{}' keeps the previous version until the library changes again.",
                            entry.Description.Id);
            entry.ReloadInFlight.store(false);
            return;
        }

        m_Logger->Info("Hot-reload layer '{}' staged generation {}; installing on the render thread.",
                       entry.Description.Id, staged.generation);
        m_Dispatcher->Publish(StagedEvent{&entry});
    }

    void HotReloadLayerManager::InstallStaged(Entry& entry) const
    {
        try
        {
            auto retired = entry.Asset->Commit();
            auto* newLayer = static_cast<HotReloadableLayer*>(entry.Asset->GetInstance().instance);
            entry.Proxy->QueueSwap(newLayer, std::move(retired));
            m_Logger->Info("Hot-reload layer '{}' reloaded (generation {}).",
                           entry.Description.Id, entry.Asset->GetInstance().generation);
        }
        catch (const std::exception& exception)
        {
            m_Logger->Error("Hot-reload layer '{}' failed to install a staged reload: {}",
                            entry.Description.Id, exception.what());
        }
        entry.ReloadInFlight.store(false);
    }

    Task<void> HotReloadLayerManager::StartAsync(std::stop_token stopToken)
    {
        co_await ResumeOn(ThreadPool::Instance());
        for (auto& registration : m_Registrations->Items)
        {
            if (stopToken.stop_requested())
            {
                co_return;
            }

            HotReloadAssetDescription assetDescription{
                .libraryPath = registration.Description.LibraryPath,
                .shadowDirectory = m_Options->ShadowDirectory,
                .shadowCopy = m_Options->ShadowCopy,
                .createSymbol = registration.Description.CreateSymbol,
                .destroySymbol = registration.Description.DestroySymbol,
                .abiVersionSymbol = registration.Description.AbiVersionSymbol,
            };

            auto asset = std::make_unique<HotReloadAsset>(
                registration.Description.Id, assetDescription, m_FileSystem, m_Dispatcher, m_Logger);

            auto entry = std::make_unique<Entry>(
                registration.Description, registration.Proxy, std::move(asset),
                m_FileSystem, m_Dispatcher, m_Logger);

            LoadInitial(*entry);

            const auto enableHotReload = entry->Description.EnableHotReload.value_or(m_Options->Enabled);
            if (enableHotReload)
            {
                auto* entryPtr = entry.get();
                entry->StagedSubscription = m_Dispatcher->Subscribe<StagedEvent>(
                    [this, entryPtr](const StagedEvent& event)
                    {
                        if (event.entry == entryPtr)
                        {
                            InstallStaged(*entryPtr);
                        }
                    },
                    EventDelivery::Async, EventTarget::Render);
                entry->FileSubscription = m_Dispatcher->Subscribe<FileChangedEvent>(
                    [this, entryPtr](const FileChangedEvent& event)
                    {
                        std::error_code error;
                        const auto changed = std::filesystem::weakly_canonical(event.path, error);
                        const auto watched = std::filesystem::weakly_canonical(
                            m_FileSystem->ResolvePath(entryPtr->Description.LibraryPath), error);
                        if (changed == watched)
                        {
                            QueueReload(*entryPtr);
                        }
                    },
                    EventDelivery::Async, EventTarget::ThreadPool);

                const auto interval = entry->Description.PollingInterval.value_or(m_Options->PollingInterval);
                entry->Watcher.Watch(entry->Description.LibraryPath);
                co_await entry->Watcher.StartAsync(interval, stopToken);

                m_Logger->Info("Hot-reload layer '{}' watching {} for changes (every {} ms).",
                               entry->Description.Id, entry->Description.LibraryPath.string(),
                               interval.count());
            }

            m_Entries.push_back(std::move(entry));
        }
        co_return;
    }

    Task<void> HotReloadLayerManager::StopAsync()
    {
        for (auto& entry : m_Entries)
        {
            entry->FileSubscription.Reset();
            entry->StagedSubscription.Reset();
            co_await entry->Watcher.StopAsync();
        }
        co_return;
    }

    HotReloadState HotReloadLayerManager::GetState(const std::string& id) const
    {
        for (const auto& entry : m_Entries)
        {
            if (entry->Description.Id == id)
            {
                return entry->Asset->GetState();
            }
        }
        return HotReloadState::Unloaded;
    }

    std::string HotReloadLayerManager::LastError(const std::string& id) const
    {
        for (const auto& entry : m_Entries)
        {
            if (entry->Description.Id == id)
            {
                return entry->Asset->LastError();
            }
        }
        return {};
    }
}

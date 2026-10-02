export module GPP.Graphics:Application.HotReloadLayerManager;

import std;
import GPP.Core;
import :Windowing;
import :Application.Layer;
import :Application.HotReloadLayer;
import :RenderConfig;

namespace GPP
{
    export struct HotReloadLayerDescription
    {
        std::string Id;
        std::filesystem::path LibraryPath;
        LayerTarget Target;

        std::optional<bool> EnableHotReload;
        std::optional<std::chrono::milliseconds> PollingInterval;

        std::string CreateSymbol{"GPP_CreateHotReloadModule"};
        std::string DestroySymbol{"GPP_DestroyHotReloadModule"};
        std::string AbiVersionSymbol{"GPP_HotReloadModuleAbiVersion"};
    };

    export struct HotReloadLayerBuilder
    {
        HotReloadLayerDescription Description;

        HotReloadLayerBuilder(std::string id, std::filesystem::path libraryPath)
        {
            Description.Id = std::move(id);
            Description.LibraryPath = std::move(libraryPath);
        }

        HotReloadLayerBuilder& SetWindowTarget(const WindowId target)
        {
            Description.Target.Type = LayerTarget::Type::eLayerTargetWindow;
            Description.Target.Id = target;
            return *this;
        }

        HotReloadLayerBuilder& SetWindowTarget(std::string name)
        {
            Description.Target.Type = LayerTarget::Type::eLayerTargetWindow;
            Description.Target.Id = -1;
            Description.Target.Name = std::move(name);
            return *this;
        }

        HotReloadLayerBuilder& SetBufferTarget(const std::uint32_t bufferId)
        {
            Description.Target.Type = LayerTarget::Type::eLayerTargetBuffer;
            Description.Target.Id = bufferId;
            return *this;
        }

        HotReloadLayerBuilder& EnableHotReload(bool enabled = true)
        {
            Description.EnableHotReload = enabled;
            return *this;
        }

        HotReloadLayerBuilder& SetPollingInterval(std::chrono::milliseconds interval)
        {
            Description.PollingInterval = interval;
            return *this;
        }

        HotReloadLayerBuilder& SetSymbolNames(std::string create, std::string destroy, std::string abiVersion)
        {
            Description.CreateSymbol = std::move(create);
            Description.DestroySymbol = std::move(destroy);
            Description.AbiVersionSymbol = std::move(abiVersion);
            return *this;
        }
    };

    export struct HotReloadLayerRegistration
    {
        HotReloadLayerDescription Description;
        std::shared_ptr<HotReloadLayerProxy> Proxy;
    };

    export struct HotReloadLayerRegistrations : public IService
    {
        std::vector<HotReloadLayerRegistration> Items;
    };

    export class HotReloadLayerManager : public IHostedService
    {
    public:
        using Dependencies = std::tuple<HotReloadLayerRegistrations, LayerHotReloadOptions, EventDispatcher,
                                         IFileSystem, Logger, ServiceProvider>;

        HotReloadLayerManager(std::shared_ptr<HotReloadLayerRegistrations> registrations,
                             std::shared_ptr<LayerHotReloadOptions> options,
                             std::shared_ptr<EventDispatcher> dispatcher,
                             std::shared_ptr<IFileSystem> fileSystem,
                             std::shared_ptr<Logger> logger,
                             std::shared_ptr<ServiceProvider> provider);
        ~HotReloadLayerManager() override;

        HotReloadLayerManager(const HotReloadLayerManager&) = delete;
        HotReloadLayerManager& operator=(const HotReloadLayerManager&) = delete;

        Task<void> StartAsync(std::stop_token stopToken) override;
        Task<void> StopAsync() override;

        [[nodiscard]] HotReloadState GetState(const std::string& id) const;
        [[nodiscard]] std::string LastError(const std::string& id) const;

    private:
        struct Entry;
        struct StagedEvent;

        void LoadInitial(Entry& entry) const;
        void QueueReload(Entry& entry) const;
        void InstallStaged(Entry& entry) const;

        std::shared_ptr<HotReloadLayerRegistrations> m_Registrations;
        std::shared_ptr<LayerHotReloadOptions> m_Options;
        std::shared_ptr<EventDispatcher> m_Dispatcher;
        std::shared_ptr<IFileSystem> m_FileSystem;
        std::shared_ptr<Logger> m_Logger;
        std::shared_ptr<ServiceProvider> m_Provider;
        std::vector<std::unique_ptr<Entry>> m_Entries;
    };
}

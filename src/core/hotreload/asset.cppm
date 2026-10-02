export module GPP.Core:HotReload.Asset;

import std;
import :Types;
import :DI.Service;
import :DI.Provider;
import :IO.File;
import :Events;
import :Logger;
import :HotReload.DynamicLibrary;

namespace GPP
{
    // Needs to be bumped whenever the Create/Destroy/AbiVersion function-pointer contract itself changes!
    export constexpr std::uint32_t kHotReloadAbiVersion = 1;

    export enum class HotReloadState : $u8
    {
        Unloaded,
        Loading,
        Loaded,
        Failed
    };

    export struct HotReloadAssetDescription
    {
        std::filesystem::path libraryPath;
        std::filesystem::path shadowDirectory{".gpp/hot-reload-cache"};
        bool shadowCopy = true;
        int copyRetryCount = 5;
        std::chrono::milliseconds copyRetryDelay{40};

        std::string createSymbol{"GPP_CreateHotReloadModule"};
        std::string destroySymbol{"GPP_DestroyHotReloadModule"};
        std::string abiVersionSymbol{"GPP_HotReloadModuleAbiVersion"};
        std::uint32_t expectedAbiVersion = kHotReloadAbiVersion;
    };

    export struct HotReloadInstanceHandle
    {
        void* instance = nullptr;
        std::uint64_t generation = 0;

        [[nodiscard]] explicit operator bool() const noexcept { return instance != nullptr; }
    };

    export struct HotReloadAssetStagedEvent
    {
        std::string assetId;
        std::uint64_t generation = 0;
    };

    export struct HotReloadAssetFailedEvent
    {
        std::string assetId;
        std::string error;
    };

    export class RetiredHotReloadInstance
    {
    public:
        using DestroyFn = void (*)(void*);

        RetiredHotReloadInstance() = default;
        RetiredHotReloadInstance(void* instance, DestroyFn destroy, DynamicLibrary library) noexcept
            : m_Instance(instance), m_Destroy(destroy), m_Library(std::move(library))
        {
        }

        RetiredHotReloadInstance(const RetiredHotReloadInstance&) = delete;
        RetiredHotReloadInstance& operator=(const RetiredHotReloadInstance&) = delete;

        RetiredHotReloadInstance(RetiredHotReloadInstance&& other) noexcept
            : m_Instance(std::exchange(other.m_Instance, nullptr)),
              m_Destroy(std::exchange(other.m_Destroy, nullptr)),
              m_Library(std::move(other.m_Library))
        {
        }

        RetiredHotReloadInstance& operator=(RetiredHotReloadInstance&& other) noexcept
        {
            if (this != &other)
            {
                DestroyNow();
                m_Instance = std::exchange(other.m_Instance, nullptr);
                m_Destroy = std::exchange(other.m_Destroy, nullptr);
                m_Library = std::move(other.m_Library);
            }
            return *this;
        }

        ~RetiredHotReloadInstance() { DestroyNow(); }

        [[nodiscard]] void* GetInstance() const noexcept { return m_Instance; }
        [[nodiscard]] bool HasInstance() const noexcept { return m_Instance != nullptr; }

    private:
        void DestroyNow() noexcept
        {
            if (m_Instance && m_Destroy)
            {
                m_Destroy(m_Instance);
            }
            m_Instance = nullptr;
            m_Destroy = nullptr;
            m_Library.Unload();
        }

        void* m_Instance = nullptr;
        DestroyFn m_Destroy = nullptr;
        DynamicLibrary m_Library;
    };

    export class HotReloadAsset : public IService
    {
    public:
        using Dependencies = std::tuple<IFileSystem, EventDispatcher, Logger>;
        using CreateFn = void* (*)(ServiceProvider*);
        using DestroyFn = RetiredHotReloadInstance::DestroyFn;
        using AbiVersionFn = std::uint32_t (*)();

        HotReloadAsset(std::string assetId,
                      HotReloadAssetDescription description,
                      std::shared_ptr<IFileSystem> fileSystem,
                      std::shared_ptr<EventDispatcher> dispatcher,
                      std::shared_ptr<Logger> logger);
        ~HotReloadAsset() override;

        HotReloadAsset(const HotReloadAsset&) = delete;
        HotReloadAsset& operator=(const HotReloadAsset&) = delete;

        HotReloadInstanceHandle Stage(ServiceProvider& provider) noexcept;
        [[nodiscard]] RetiredHotReloadInstance Commit();
        bool Load(ServiceProvider& provider) noexcept;

        [[nodiscard]] HotReloadInstanceHandle GetInstance() const noexcept;
        [[nodiscard]] HotReloadState GetState() const noexcept;
        [[nodiscard]] std::string LastError() const;
        [[nodiscard]] const std::string& GetAssetId() const noexcept { return m_AssetId; }
        [[nodiscard]] const HotReloadAssetDescription& GetDescription() const noexcept { return m_Description; }

    private:
        struct StagedInstance
        {
            DynamicLibrary library;
            void* instance = nullptr;
            DestroyFn destroy = nullptr;
            std::uint64_t generation = 0;
        };

        [[nodiscard]] std::filesystem::path PrepareLibraryFile();
        void Fail(const std::string& error);

        std::string m_AssetId;
        HotReloadAssetDescription m_Description;
        std::shared_ptr<IFileSystem> m_FileSystem;
        std::shared_ptr<EventDispatcher> m_Dispatcher;
        std::shared_ptr<Logger> m_Logger;

        std::atomic<bool> m_Staging{false};
        mutable std::mutex m_Mutex;
        DynamicLibrary m_CurrentLibrary;
        void* m_CurrentInstance = nullptr;
        DestroyFn m_CurrentDestroy = nullptr;
        std::optional<StagedInstance> m_Staged;
        HotReloadState m_State = HotReloadState::Unloaded;
        std::string m_LastError;
        std::uint64_t m_Generation = 0;
    };
}

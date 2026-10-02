module GPP.Core;

import :HotReload.Asset;
import std;

namespace GPP
{
    HotReloadAsset::HotReloadAsset(std::string assetId,
                                   HotReloadAssetDescription description,
                                   std::shared_ptr<IFileSystem> fileSystem,
                                   std::shared_ptr<EventDispatcher> dispatcher,
                                   std::shared_ptr<Logger> logger)
        : m_AssetId(std::move(assetId)),
          m_Description(std::move(description)),
          m_FileSystem(std::move(fileSystem)),
          m_Dispatcher(std::move(dispatcher)),
          m_Logger(std::move(logger))
    {
        if (!m_FileSystem || !m_Dispatcher || !m_Logger)
        {
            throw std::invalid_argument("HotReloadAsset requires a file system, dispatcher, and logger.");
        }
        if (m_Description.libraryPath.empty())
        {
            throw std::invalid_argument("HotReloadAsset requires a non-empty library path.");
        }
    }

    HotReloadAsset::~HotReloadAsset()
    {
        if (m_Staged && m_Staged->instance && m_Staged->destroy)
        {
            try
            {
                m_Staged->destroy(m_Staged->instance);
            }
            catch (...)
            {
            }
        }
        if (m_CurrentInstance && m_CurrentDestroy)
        {
            try
            {
                m_CurrentDestroy(m_CurrentInstance);
            }
            catch (const std::exception& exception)
            {
                if (m_Logger)
                {
                    m_Logger->Error("Hot-reload asset '{}': destroy() threw during shutdown: {}",
                                   m_AssetId, exception.what());
                }
            }
            catch (...)
            {
            }
        }
    }

    void HotReloadAsset::Fail(const std::string& error)
    {
        {
            std::scoped_lock lock(m_Mutex);
            m_LastError = error;
            m_State = HotReloadState::Failed;
        }
        if (m_Logger)
        {
            m_Logger->Error("Hot-reload asset '{}' failed: {}", m_AssetId, error);
        }
        if (m_Dispatcher)
        {
            m_Dispatcher->Publish(HotReloadAssetFailedEvent{m_AssetId, error});
        }
    }

    std::filesystem::path HotReloadAsset::PrepareLibraryFile()
    {
        const auto resolved = m_FileSystem->ResolvePath(m_Description.libraryPath);
        if (!std::filesystem::exists(resolved))
        {
            throw std::runtime_error(std::format(
                "Hot-reload library does not exist: {}", resolved.string()));
        }
        if (!m_Description.shadowCopy)
        {
            return resolved;
        }

        const auto shadowDir = m_FileSystem->ResolvePath(m_Description.shadowDirectory);
        std::error_code createError;
        std::filesystem::create_directories(shadowDir, createError);

        std::uint64_t generationHint;
        {
            std::scoped_lock lock(m_Mutex);
            generationHint = m_Generation + 1;
        }
        const auto destination = shadowDir /
            std::format("{}_{}{}", m_AssetId, generationHint, resolved.extension().string());

        std::string lastError;
        const auto attempts = std::max(1, m_Description.copyRetryCount);
        for (int attempt = 0; attempt < attempts; ++attempt)
        {
            std::error_code copyError;
            std::filesystem::copy_file(
                resolved, destination, std::filesystem::copy_options::overwrite_existing, copyError);
            if (!copyError)
            {
                return destination;
            }
            lastError = copyError.message();
            if (attempt + 1 < attempts)
            {
                std::this_thread::sleep_for(m_Description.copyRetryDelay);
            }
        }
        throw std::runtime_error(std::format(
            "Failed to stage a shadow copy of '{}': {}", resolved.string(), lastError));
    }

    HotReloadInstanceHandle HotReloadAsset::Stage(ServiceProvider& provider) noexcept
    {
        bool expected = false;
        if (!m_Staging.compare_exchange_strong(expected, true))
        {
            if (m_Logger)
            {
                m_Logger->Warn(
                    "Hot-reload asset '{}' is already staging a reload; ignoring concurrent request.",
                    m_AssetId);
            }
            return {};
        }
        struct StagingGuard
        {
            std::atomic<bool>& flag;
            ~StagingGuard() { flag.store(false); }
        } guard{m_Staging};

        {
            std::scoped_lock lock(m_Mutex);
            m_State = HotReloadState::Loading;
        }

        try
        {
            const auto libraryFile = PrepareLibraryFile();
            DynamicLibrary library(libraryFile);

            const auto abiVersionFn = library.GetSymbol<AbiVersionFn>(m_Description.abiVersionSymbol);
            if (!abiVersionFn)
            {
                throw std::runtime_error(std::format(
                    "Hot-reload library is missing required export '{}'.",
                    m_Description.abiVersionSymbol));
            }
            const auto abiVersion = abiVersionFn();
            if (abiVersion != m_Description.expectedAbiVersion)
            {
                throw std::runtime_error(std::format(
                    "Hot-reload library ABI version mismatch for '{}' (expected {}, got {}). "
                    "Rebuild the plugin against the current GPP.Core headers.",
                    m_AssetId, m_Description.expectedAbiVersion, abiVersion));
            }

            const auto createFn = library.GetSymbol<CreateFn>(m_Description.createSymbol);
            const auto destroyFn = library.GetSymbol<DestroyFn>(m_Description.destroySymbol);
            if (!createFn || !destroyFn)
            {
                throw std::runtime_error(std::format(
                    "Hot-reload library is missing required export '{}' or '{}'.",
                    m_Description.createSymbol, m_Description.destroySymbol));
            }

            void* instance = createFn(&provider);
            if (!instance)
            {
                throw std::runtime_error("Hot-reload library factory returned a null instance.");
            }

            std::uint64_t generation;
            {
                std::scoped_lock lock(m_Mutex);
                generation = ++m_Generation;
                if (m_Staged && m_Staged->instance && m_Staged->destroy)
                {
                    // a earlier Stage() succeeded but never commitd aka leak
                    m_Staged->destroy(m_Staged->instance);
                }
                m_Staged = StagedInstance{std::move(library), instance, destroyFn, generation};
                m_State = HotReloadState::Loaded;
                m_LastError.clear();
            }
            if (m_Logger)
            {
                m_Logger->Info("Hot-reload asset '{}' staged generation {} from {}",
                               m_AssetId, generation, libraryFile.string());
            }
            if (m_Dispatcher)
            {
                m_Dispatcher->Publish(HotReloadAssetStagedEvent{m_AssetId, generation});
            }
            return HotReloadInstanceHandle{instance, generation};
        }
        catch (const std::exception& exception)
        {
            Fail(exception.what());
            return {};
        }
        catch (...)
        {
            Fail("Unknown error while staging hot-reload asset.");
            return {};
        }
    }

    RetiredHotReloadInstance HotReloadAsset::Commit()
    {
        std::scoped_lock lock(m_Mutex);
        if (!m_Staged)
        {
            throw std::logic_error(std::format(
                "HotReloadAsset::Commit() called for '{}' without a successfully staged instance.",
                m_AssetId));
        }
        RetiredHotReloadInstance retired(m_CurrentInstance, m_CurrentDestroy, std::move(m_CurrentLibrary));
        m_CurrentLibrary = std::move(m_Staged->library);
        m_CurrentInstance = m_Staged->instance;
        m_CurrentDestroy = m_Staged->destroy;
        m_Generation = m_Staged->generation;
        m_Staged.reset();
        m_State = HotReloadState::Loaded;
        return retired;
    }

    bool HotReloadAsset::Load(ServiceProvider& provider) noexcept
    {
        if (!Stage(provider))
        {
            return false;
        }
        try
        {
            [[maybe_unused]] auto retired = Commit();
            return true;
        }
        catch (const std::exception& exception)
        {
            Fail(exception.what());
            return false;
        }
    }

    HotReloadInstanceHandle HotReloadAsset::GetInstance() const noexcept
    {
        std::scoped_lock lock(m_Mutex);
        return HotReloadInstanceHandle{m_CurrentInstance, m_Generation};
    }

    HotReloadState HotReloadAsset::GetState() const noexcept
    {
        std::scoped_lock lock(m_Mutex);
        return m_State;
    }

    std::string HotReloadAsset::LastError() const
    {
        std::scoped_lock lock(m_Mutex);
        return m_LastError;
    }
}

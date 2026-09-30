module GPP.Graphics;

import std;
import GPP.Core;
import :ShaderAssets;

namespace GPP
{
    ShaderAssetCatalog::ShaderAssetCatalog(std::shared_ptr<IFileSystem> fileSystem,
                                           std::shared_ptr<RenderOptions> options,
                                           std::shared_ptr<Logger> logger,
                                           std::shared_ptr<EventDispatcher> dispatcher)
        : m_FileSystem(std::move(fileSystem)),
          m_Options(std::move(options)),
          m_Logger(std::move(logger)),
          m_Dispatcher(std::move(dispatcher)),
          m_Watcher(m_FileSystem, m_Dispatcher, m_Logger),
          m_Compiler(m_FileSystem, m_Logger)
    {
        if (!m_FileSystem || !m_Options || !m_Dispatcher)
        {
            throw std::invalid_argument(
                "ShaderAssetCatalog requires file system, render options, and dispatcher.");
        }
    }

    std::optional<ShaderStage> ShaderAssetCatalog::GetStage(const std::filesystem::path& path)
    {
        const auto name = path.filename().string();
        if (name.ends_with(".vert") || name.ends_with(".vert.glsl")) return ShaderStage::Vertex;
        if (name.ends_with(".frag") || name.ends_with(".frag.glsl")) return ShaderStage::Fragment;
        if (name.ends_with(".comp") || name.ends_with(".comp.glsl")) return ShaderStage::Compute;
        if (name.ends_with(".geom") || name.ends_with(".geom.glsl")) return ShaderStage::Geometry;
        if (name.ends_with(".tesc") || name.ends_with(".tesc.glsl"))
            return ShaderStage::TessellationControl;
        if (name.ends_with(".tese") || name.ends_with(".tese.glsl"))
            return ShaderStage::TessellationEvaluation;
        if (name.ends_with(".spv"))
        {
            if (name.ends_with(".vert.spv") || name == "vert.spv") return ShaderStage::Vertex;
            if (name.ends_with(".frag.spv") || name == "frag.spv") return ShaderStage::Fragment;
            if (name.ends_with(".comp.spv") || name == "comp.spv") return ShaderStage::Compute;
            if (name.ends_with(".geom.spv") || name == "geom.spv") return ShaderStage::Geometry;
            if (name.ends_with(".tesc.spv") || name == "tesc.spv")
                return ShaderStage::TessellationControl;
            if (name.ends_with(".tese.spv") || name == "tese.spv")
                return ShaderStage::TessellationEvaluation;
        }
        return std::nullopt;
    }

    void ShaderAssetCatalog::CompileDirectory(const std::filesystem::path& directory,
                                              const ShaderCompileOptions& compileOptions)
    {
        std::error_code error;
        if (!std::filesystem::exists(directory, error))
        {
            if (m_Logger) m_Logger->Warn("Shader asset directory does not exist: {}", directory.string());
            return;
        }

        for (const auto& entry : std::filesystem::recursive_directory_iterator(directory, error))
        {
            if (error)
            {
                throw std::runtime_error(std::format(
                    "Failed to enumerate shader directory {}: {}", directory.string(), error.message()));
            }
            if (!entry.is_regular_file(error))
            {
                continue;
            }
            const auto stage = GetStage(entry.path());
            if (!stage)
            {
                continue;
            }

            const auto resolved = m_FileSystem->ResolvePath(entry.path());
            auto compiled = m_Compiler.Compile(
                ShaderSource{.path = resolved, .stage = *stage}, compileOptions);
            std::scoped_lock lock(m_Mutex);
            m_ShaderPaths.push_back(resolved);
            m_Shaders[resolved.lexically_normal().string()] = std::move(compiled);
        }
    }

    ShaderCompileOptions ShaderAssetCatalog::GetCompileOptions() const
    {
        ShaderCompileOptions options;
        for (const auto& directory : m_Options->ShaderAssetDirectories)
        {
            options.includeDirectories.push_back(m_FileSystem->ResolvePath(directory));
        }
        return options;
    }

    Task<void> ShaderAssetCatalog::StartAsync(std::stop_token stopToken)
    {
        co_await ResumeOn(ThreadPool::Instance());
        if (stopToken.stop_requested())
        {
            co_return;
        }
        try
        {
            const auto compileOptions = GetCompileOptions();
            for (const auto& directory : m_Options->ShaderAssetDirectories)
            {
                const auto resolved = m_FileSystem->ResolvePath(directory);
                CompileDirectory(resolved, compileOptions);
            }
            {
                std::scoped_lock lock(m_Mutex);
                m_Ready = true;
                RefreshMetadataLocked();
                ++m_Generation;
            }
            m_Condition.notify_all();
            if (m_Options->EnableShaderHotReload)
            {
                for (const auto& path : GetShaderPaths())
                {
                    m_Watcher.Watch(path);
                }
                for (const auto& metadata : GetAvailableShaders())
                {
                    if (const CompiledShader* shader = nullptr;
                        TryGetShader(metadata.path, shader))
                    {
                        for (const auto& dependency : shader->dependencies)
                        {
                            m_Watcher.Watch(dependency);
                        }
                    }
                }
                m_FileSubscription = m_Dispatcher->Subscribe<ShaderFileChangedEvent>(
                    [this](const ShaderFileChangedEvent& event)
                    {
                        ThreadPool::Instance().Submit([this, path = event.path]
                        {
                            ReloadChanged(path);
                        });
                    });
                m_Watcher.StartAsync(m_Options->ShaderHotReloadInterval,
                                    stopToken).get();
            }
            m_Dispatcher->Publish(ShaderAssetsUpdatedEvent{
                .generation = m_Generation,
                .changedPaths = GetShaderPaths()
            });
            if (m_Logger)
            {
                m_Logger->Info("Shader asset catalog ready: {} shaders compiled",
                               m_ShaderPaths.size());
            }
        }
        catch (...)
        {
            {
                std::scoped_lock lock(m_Mutex);
                m_StartError = std::current_exception();
                m_Ready = true;
            }
            m_Condition.notify_all();
            if (m_Logger) m_Logger->Error("Shader asset catalog startup failed.");
            throw;
        }
    }

    Task<void> ShaderAssetCatalog::StopAsync()
    {
        m_FileSubscription.Reset();
        m_Watcher.StopAsync().get();
        std::scoped_lock lock(m_Mutex);
        m_Stopping = true;
        m_Condition.notify_all();
        co_return;
    }

    Task<CompiledShader> ShaderAssetCatalog::AwaitShader(std::filesystem::path path) const
    {
        co_await ResumeOn(ThreadPool::Instance());
        const auto key = m_FileSystem->ResolvePath(path).lexically_normal().string();
        std::unique_lock lock(m_Mutex);
        m_Condition.wait(lock, [this] { return m_Ready || m_Stopping; });
        if (m_StartError)
        {
            std::rethrow_exception(m_StartError);
        }
        const auto it = m_Shaders.find(key);
        if (it == m_Shaders.end())
        {
            throw std::runtime_error(std::format("Shader asset was not compiled: {}", key));
        }
        co_return it->second;
    }

    bool ShaderAssetCatalog::TryGetShader(const std::filesystem::path& path,
                                          const CompiledShader*& shader) const
    {
        shader = nullptr;
        const auto key = m_FileSystem->ResolvePath(path).lexically_normal().string();
        std::scoped_lock lock(m_Mutex);
        const auto it = m_Shaders.find(key);
        if (it == m_Shaders.end())
        {
            return false;
        }
        shader = &it->second;
        return true;
    }

    std::vector<ShaderAssetMetadata> ShaderAssetCatalog::GetAvailableShaders() const
    {
        std::scoped_lock lock(m_Mutex);
        return m_Metadata;
    }

    std::vector<std::filesystem::path> ShaderAssetCatalog::GetShaderPaths() const
    {
        std::scoped_lock lock(m_Mutex);
        return m_ShaderPaths;
    }

    bool ShaderAssetCatalog::IsReady() const noexcept
    {
        std::scoped_lock lock(m_Mutex);
        return m_Ready;
    }

    void ShaderAssetCatalog::ReloadChanged(const std::filesystem::path& changedPath)
    {
        std::vector<std::filesystem::path> changed{changedPath};
        std::vector<std::pair<std::string, CompiledShader>> replacements;
        const auto normalizedChangedPath = changedPath.lexically_normal();
        {
            std::scoped_lock lock(m_Mutex);
            for (const auto& [key, shader] : m_Shaders)
            {
                const auto matches = std::ranges::find(
                    shader.dependencies, normalizedChangedPath) != shader.dependencies.end() ||
                    shader.resolvedPath.lexically_normal() == normalizedChangedPath;
                if (matches)
                {
                    replacements.emplace_back(key, m_Compiler.Compile(
                        shader.source, GetCompileOptions()));
                }
            }
        }
        if (replacements.empty())
        {
            return;
        }
        {
            std::scoped_lock lock(m_Mutex);
            for (auto& [key, shader] : replacements)
            {
                m_Shaders[key] = std::move(shader);
            }
            RefreshMetadataLocked();
            ++m_Generation;
        }
        m_Dispatcher->Publish(ShaderAssetsUpdatedEvent{
            .generation = m_Generation,
            .changedPaths = std::move(changed)
        });
    }

    void ShaderAssetCatalog::RefreshMetadataLocked()
    {
        m_Metadata.clear();
        m_Metadata.reserve(m_Shaders.size());
        for (const auto& entry : m_Shaders)
        {
            const auto& shader = entry.second;
            m_Metadata.push_back({
                .path = shader.resolvedPath,
                .stage = shader.source.stage,
                .entryPoint = shader.source.entryPoint,
                .sourceHash = shader.sourceHash,
                .reflection = shader.reflection
            });
        }
        std::ranges::sort(m_Metadata, {}, &ShaderAssetMetadata::path);
    }
}

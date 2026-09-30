module;

#include <shaderc/shaderc.hpp>
#include <spirv_cross/spirv_cross.hpp>

module GPP.Graphics;

import std;
import GPP.Core;
import :Shader;

namespace GPP
{
    namespace
    {
        std::uint64_t HashBytes(std::string_view bytes)
        {
            std::uint64_t hash = 14695981039346656037ull;
            for (const auto byte : bytes)
            {
                hash ^= static_cast<unsigned char>(byte);
                hash *= 1099511628211ull;
            }
            return hash;
        }

        std::uint64_t HashShader(std::string_view source, const ShaderSource& shader,
                                 const ShaderCompileOptions& options)
        {
            std::string key(source);
            key.push_back('\0');
            key += std::to_string(static_cast<int>(shader.stage));
            key.push_back('\0');
            key += shader.entryPoint;
            for (const auto& [name, value] : shader.defines)
            {
                key.push_back('\0');
                key += name;
                key.push_back('=');
                key += value;
            }
            key += options.generateDebugInfo ? ":debug" : ":release";
            key += options.optimize ? ":optimized" : ":unoptimized";
            for (const auto& includeDirectory : options.includeDirectories)
            {
                key.push_back('\0');
                key += includeDirectory.lexically_normal().string();
            }
            return HashBytes(key);
        }

        struct IncludeData
        {
            shaderc_include_result result{};
            std::string sourceName;
            std::string content;
            std::filesystem::path path;
        };

        class ShaderIncluder final : public shaderc::CompileOptions::IncluderInterface
        {
        public:
            ShaderIncluder(std::shared_ptr<IFileSystem> fileSystem,
                           std::vector<std::filesystem::path> includeDirectories)
                : m_FileSystem(std::move(fileSystem)),
                  m_IncludeDirectories(std::move(includeDirectories))
            {
            }

            shaderc_include_result* GetInclude(const char* requestedSource,
                                               shaderc_include_type type,
                                               const char* requestingSource,
                                               size_t) override
            {
                const auto requested = std::filesystem::path(requestedSource);
                std::vector<std::filesystem::path> candidates;
                if (type == shaderc_include_type_relative)
                {
                    candidates.emplace_back(
                        std::filesystem::path(requestingSource).parent_path() / requested);
                }
                for (const auto& root : m_IncludeDirectories)
                {
                    candidates.emplace_back(root / requested);
                }

                for (const auto& candidate : candidates)
                {
                    const auto resolved = m_FileSystem->ResolvePath(candidate);
                    std::error_code error;
                    if (!std::filesystem::is_regular_file(resolved, error) || error)
                    {
                        continue;
                    }
                    try
                    {
                        const auto normalized = resolved.lexically_normal();
                        if (std::ranges::find(m_ActiveIncludes, normalized) != m_ActiveIncludes.end())
                        {
                            return MakeError(std::format(
                                "Cyclic shader include detected at '{}'.", normalized.string()));
                        }
                        auto data = std::make_unique<IncludeData>();
                        data->sourceName = std::filesystem::weakly_canonical(resolved, error).string();
                        if (error) data->sourceName = resolved.lexically_normal().string();
                        data->content = m_FileSystem->ReadAllText(resolved);
                        data->result.source_name = data->sourceName.c_str();
                        data->result.source_name_length = data->sourceName.size();
                        data->result.content = data->content.c_str();
                        data->result.content_length = data->content.size();
                        data->result.user_data = data.get();
                        data->path = normalized;
                        m_Dependencies.push_back(normalized);
                        m_ActiveIncludes.push_back(normalized);
                        return &data.release()->result;
                    }
                    catch (const std::exception& exception)
                    {
                        return MakeError(std::format("Failed to read shader include '{}': {}",
                                                     requestedSource, exception.what()));
                    }
                }
                return MakeError(std::format(
                    "Shader include '{}' not found while compiling '{}'.",
                    requestedSource, requestingSource));
            }

            void ReleaseInclude(shaderc_include_result* result) override
            {
                const auto path = static_cast<IncludeData*>(result->user_data)->path;
                std::erase(m_ActiveIncludes, path);
                delete static_cast<IncludeData*>(result->user_data);
            }

            const std::vector<std::filesystem::path>& Dependencies() const noexcept
            {
                return m_Dependencies;
            }

        private:
            static shaderc_include_result* MakeError(std::string message)
            {
                auto data = std::make_unique<IncludeData>();
                data->content = std::move(message);
                data->result.content = data->content.c_str();
                data->result.content_length = data->content.size();
                data->result.user_data = data.get();
                return &data.release()->result;
            }

            std::shared_ptr<IFileSystem> m_FileSystem;
            std::vector<std::filesystem::path> m_IncludeDirectories;
            std::vector<std::filesystem::path> m_Dependencies;
            std::vector<std::filesystem::path> m_ActiveIncludes;
        };

        std::filesystem::path DependencyManifestPath(const std::filesystem::path& cachePath)
        {
            return cachePath.string() + ".deps";
        }

        void WriteDependencyManifest(const std::filesystem::path& path,
                                     const std::shared_ptr<IFileSystem>& fileSystem,
                                     const std::vector<std::filesystem::path>& dependencies)
        {
            std::ofstream file(path, std::ios::trunc);
            if (!file) return;
            for (const auto& dependency : dependencies)
            {
                file << dependency.lexically_normal().string() << '\n';
                file << HashBytes(fileSystem->ReadAllText(dependency)) << '\n';
            }
        }

        bool DependenciesUnchanged(const std::filesystem::path& path,
                                   const std::shared_ptr<IFileSystem>& fileSystem,
                                   std::vector<std::filesystem::path>* dependencies = nullptr)
        {
            std::ifstream file(path);
            if (!file) return false;
            std::string dependencyPath;
            std::string dependencyHash;
            while (std::getline(file, dependencyPath) && std::getline(file, dependencyHash))
            {
                try
                {
                    const auto resolved = std::filesystem::path(dependencyPath);
                    if (std::to_string(HashBytes(fileSystem->ReadAllText(resolved))) != dependencyHash)
                    {
                        return false;
                    }
                    if (dependencies) dependencies->push_back(resolved);
                }
                catch (...)
                {
                    return false;
                }
            }
            return !file.bad();
        }

        shaderc_shader_kind ShaderKind(const ShaderStage stage)
        {
            switch (stage)
            {
            case ShaderStage::Vertex: return shaderc_glsl_vertex_shader;
            case ShaderStage::Fragment: return shaderc_glsl_fragment_shader;
            case ShaderStage::Compute: return shaderc_glsl_compute_shader;
            case ShaderStage::Geometry: return shaderc_glsl_geometry_shader;
            case ShaderStage::TessellationControl: return shaderc_glsl_tess_control_shader;
            case ShaderStage::TessellationEvaluation: return shaderc_glsl_tess_evaluation_shader;
            }
            throw std::invalid_argument("Unsupported shader stage.");
        }

        ShaderStageFlags StageFlag(const ShaderStage stage)
        {
            switch (stage)
            {
            case ShaderStage::Vertex: return ShaderStageFlags::Vertex;
            case ShaderStage::Fragment: return ShaderStageFlags::Fragment;
            case ShaderStage::Compute: return ShaderStageFlags::Compute;
            case ShaderStage::Geometry: return ShaderStageFlags::Geometry;
            case ShaderStage::TessellationControl: return ShaderStageFlags::TessellationControl;
            case ShaderStage::TessellationEvaluation: return ShaderStageFlags::TessellationEvaluation;
            }
            return ShaderStageFlags::None;
        }

        ShaderScalarType ScalarType(const spirv_cross::SPIRType& type)
        {
            switch (type.basetype)
            {
            case spirv_cross::SPIRType::Boolean: return ShaderScalarType::Boolean;
            case spirv_cross::SPIRType::Int: return ShaderScalarType::SignedInteger;
            case spirv_cross::SPIRType::UInt: return ShaderScalarType::UnsignedInteger;
            case spirv_cross::SPIRType::Float: return ShaderScalarType::Float;
            case spirv_cross::SPIRType::Double: return ShaderScalarType::Double;
            default: return ShaderScalarType::Unknown;
            }
        }

        std::uint32_t ArraySize(const spirv_cross::SPIRType& type)
        {
            if (type.array.empty())
            {
                return 1;
            }

            std::uint64_t count = 1;
            for (const auto value : type.array)
            {
                if (value == 0)
                {
                    return 1;
                }
                count *= value;
            }
            return static_cast<std::uint32_t>(std::min<std::uint64_t>(count, UINT32_MAX));
        }

        ShaderDescriptorType DescriptorTypeFor(const spirv_cross::Resource& resource,
                                               const spirv_cross::SPIRType& type,
                                               const std::string& category)
        {
            if (category == "uniform_buffers") return ShaderDescriptorType::UniformBuffer;
            if (category == "storage_buffers") return ShaderDescriptorType::StorageBuffer;
            if (category == "sampled_images") return ShaderDescriptorType::CombinedImageSampler;
            if (category == "separate_images") return ShaderDescriptorType::SampledImage;
            if (category == "separate_samplers") return ShaderDescriptorType::Sampler;
            if (category == "storage_images") return ShaderDescriptorType::StorageImage;
            if (category == "subpass_inputs") return ShaderDescriptorType::InputAttachment;
            if (category == "acceleration_structures")
                return ShaderDescriptorType::AccelerationStructure;
            (void)resource;
            (void)type;
            return ShaderDescriptorType::Unknown;
        }

        void AddResources(ShaderReflection& reflection, const spirv_cross::Compiler& compiler,
                          const spirv_cross::SmallVector<spirv_cross::Resource>& resources,
                          const std::string& category, const ShaderStageFlags stages)
        {
            for (const auto& resource : resources)
            {
                const auto& type = compiler.get_type(resource.type_id);
                ShaderDescriptorBinding binding{
                    .set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet),
                    .binding = compiler.get_decoration(resource.id, spv::DecorationBinding),
                    .descriptorCount = ArraySize(type),
                    .type = DescriptorTypeFor(resource, type, category),
                    .stages = stages,
                    .name = compiler.get_name(resource.id)
                };
                reflection.descriptorBindings.push_back(std::move(binding));
            }
        }

        ShaderReflection Reflect(const std::vector<std::uint32_t>& spirv, const ShaderStage stage)
        {
            spirv_cross::Compiler compiler(spirv);
            const auto resources = compiler.get_shader_resources();
            const auto stages = StageFlag(stage);
            ShaderReflection reflection;

            AddResources(reflection, compiler, resources.uniform_buffers, "uniform_buffers", stages);
            AddResources(reflection, compiler, resources.storage_buffers, "storage_buffers", stages);
            AddResources(reflection, compiler, resources.sampled_images, "sampled_images", stages);
            AddResources(reflection, compiler, resources.separate_images, "separate_images", stages);
            AddResources(reflection, compiler, resources.separate_samplers, "separate_samplers", stages);
            AddResources(reflection, compiler, resources.storage_images, "storage_images", stages);
            AddResources(reflection, compiler, resources.subpass_inputs, "subpass_inputs", stages);
            AddResources(reflection, compiler, resources.acceleration_structures,
                         "acceleration_structures", stages);

            for (const auto& resource : resources.push_constant_buffers)
            {
                const auto& type = compiler.get_type(resource.base_type_id);
                reflection.pushConstants.push_back({
                    .offset = 0,
                    .size = static_cast<std::uint32_t>(compiler.get_declared_struct_size(type)),
                    .stages = stages
                });
            }

            if (stage == ShaderStage::Vertex)
            {
                for (const auto& resource : resources.stage_inputs)
                {
                    if (!compiler.has_decoration(resource.id, spv::DecorationLocation))
                    {
                        continue;
                    }
                    const auto& type = compiler.get_type(resource.type_id);
                    reflection.vertexInputs.push_back({
                        .location = compiler.get_decoration(resource.id, spv::DecorationLocation),
                        .components = type.vecsize,
                        .bitWidth = type.width,
                        .scalarType = ScalarType(type),
                        .name = compiler.get_name(resource.id)
                    });
                }
            }
            return reflection;
        }

        std::filesystem::path CacheDirectory(const std::shared_ptr<IFileSystem>& fileSystem,
                                             const ShaderCompileOptions& options)
        {
            if (!options.cacheDirectory.empty())
            {
                return options.cacheDirectory.is_absolute()
                           ? options.cacheDirectory
                           : fileSystem->GetWorkingDirectory() / options.cacheDirectory;
            }
            return fileSystem->GetWorkingDirectory() / ".gpp" / "shader-cache";
        }

        std::vector<std::uint32_t> ReadCache(const std::filesystem::path& path)
        {
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            if (!file)
            {
                return {};
            }
            const auto size = file.tellg();
            if (size <= 0 || size % static_cast<std::streamoff>(sizeof(std::uint32_t)) != 0)
            {
                return {};
            }
            file.seekg(0);
            std::vector<std::uint32_t> words(static_cast<std::size_t>(size) / sizeof(std::uint32_t));
            file.read(reinterpret_cast<char*>(words.data()), size);
            if (!file || words.empty() || words.front() != 0x07230203u)
            {
                return {};
            }
            return words;
        }

        std::uint64_t HashBinary(std::span<const std::byte> bytes, const ShaderSource& shader)
        {
            std::string key(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            key.push_back('\0');
            key += std::to_string(static_cast<int>(shader.stage));
            key.push_back('\0');
            key += shader.entryPoint;
            return HashBytes(key);
        }

        void WriteCache(const std::filesystem::path& path, std::span<const std::uint32_t> words)
        {
            std::error_code error;
            std::filesystem::create_directories(path.parent_path(), error);
            if (error)
            {
                return;
            }

            const auto temporary = path.string() + ".tmp";
            std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
            if (!file)
            {
                return;
            }
            file.write(reinterpret_cast<const char*>(words.data()),
                       static_cast<std::streamsize>(words.size_bytes()));
            file.close();
            if (file)
            {
                std::filesystem::rename(temporary, path, error);
                if (error)
                {
                    std::filesystem::remove(temporary, error);
                }
            }
        }
    }

    ShaderReflection MergeShaderReflections(const ShaderReflection& first,
                                            const ShaderReflection& second)
    {
        ShaderReflection merged = first;
        for (const auto& input : second.vertexInputs)
        {
            auto it = std::find_if(merged.vertexInputs.begin(), merged.vertexInputs.end(),
                                   [&input](const auto& value) { return value.location == input.location; });
            if (it == merged.vertexInputs.end())
            {
                merged.vertexInputs.push_back(input);
            }
        }
        for (const auto& range : second.pushConstants)
        {
            auto it = std::find_if(merged.pushConstants.begin(), merged.pushConstants.end(),
                                   [&range](const auto& value)
                                   {
                                       return value.offset == range.offset && value.size == range.size;
                                   });
            if (it == merged.pushConstants.end())
            {
                merged.pushConstants.push_back(range);
            }
            else
            {
                it->stages |= range.stages;
            }
        }
        for (const auto& binding : second.descriptorBindings)
        {
            auto it = std::find_if(merged.descriptorBindings.begin(), merged.descriptorBindings.end(),
                                   [&binding](const auto& value)
                                   {
                                       return value.set == binding.set && value.binding == binding.binding;
                                   });
            if (it == merged.descriptorBindings.end())
            {
                merged.descriptorBindings.push_back(binding);
            }
            else
            {
                if (it->type != binding.type || it->descriptorCount != binding.descriptorCount)
                {
                    throw std::runtime_error("Shader descriptor bindings disagree between stages.");
                }
                it->stages |= binding.stages;
                if (it->name.empty()) it->name = binding.name;
            }
        }
        return merged;
    }

    ShaderCompiler::ShaderCompiler(std::shared_ptr<IFileSystem> fileSystem,
                                   std::shared_ptr<Logger> logger)
        : m_FileSystem(std::move(fileSystem)), m_Logger(std::move(logger))
    {
        if (!m_FileSystem)
        {
            throw std::invalid_argument("ShaderCompiler requires a file system.");
        }
    }

    CompiledShader ShaderCompiler::Compile(const ShaderSource& source,
                                           const ShaderCompileOptions& options) const
    {
        if (source.path.empty())
        {
            throw std::invalid_argument("Shader source path cannot be empty.");
        }
        const auto resolvedPath = m_FileSystem->ResolvePath(source.path);
        if (resolvedPath.extension() == ".spv")
        {
            const auto bytes = m_FileSystem->ReadAllBytes(resolvedPath);
            if (bytes.empty() || bytes.size() % sizeof(std::uint32_t) != 0)
            {
                throw std::runtime_error(
                    std::format("Precompiled shader is not valid SPIR-V: {}", resolvedPath.string()));
            }
            std::vector<std::uint32_t> spirv(bytes.size() / sizeof(std::uint32_t));
            std::memcpy(spirv.data(), bytes.data(), bytes.size());
            if (spirv.front() != 0x07230203u)
            {
                throw std::runtime_error(
                    std::format("Precompiled shader has an invalid SPIR-V header: {}",
                                resolvedPath.string()));
            }
            try
            {
                auto reflection = Reflect(spirv, source.stage);
                if (m_Logger)
                {
                    m_Logger->Info("Loaded precompiled shader: {} ({} SPIR-V words)",
                                   resolvedPath.string(), spirv.size());
                }
                return CompiledShader{
                    .source = source,
                    .resolvedPath = resolvedPath,
                    .cachePath = {},
                    .spirv = std::move(spirv),
                    .reflection = std::move(reflection),
                    .sourceHash = HashBinary(bytes, source),
                    .dependencies = {}
                };
            }
            catch (const std::exception& exception)
            {
                throw std::runtime_error(std::format("Shader reflection failed for {}: {}",
                                                     resolvedPath.string(), exception.what()));
            }
        }
        const auto sourceText = m_FileSystem->ReadAllText(resolvedPath);
        return CompileText(source, options, resolvedPath, sourceText);
    }

    CompiledShader ShaderCompiler::CompileText(const ShaderSource& source,
                                               const ShaderCompileOptions& options,
                                               const std::filesystem::path& resolvedPath,
                                               const std::string& sourceText) const
    {
        const auto hash = HashShader(sourceText, source, options);
        const auto cachePath = CacheDirectory(m_FileSystem, options) /
                               (std::format("{:016x}.spv", hash));

        std::vector<std::uint32_t> spirv;
        std::vector<std::filesystem::path> dependencies;
        if (options.enableCache &&
            DependenciesUnchanged(DependencyManifestPath(cachePath), m_FileSystem, &dependencies))
        {
            spirv = ReadCache(cachePath);
            if (!spirv.empty() && m_Logger)
            {
                m_Logger->Debug("Shader cache hit: {} ({:016x})", resolvedPath.string(), hash);
            }
        }
        if (spirv.empty())
        {
            if (m_Logger)
            {
                m_Logger->Debug("Compiling shader: {} ({:016x})", resolvedPath.string(), hash);
            }
            shaderc::Compiler compiler;
            shaderc::CompileOptions compileOptions;
            auto includer = std::make_unique<ShaderIncluder>(
                m_FileSystem, options.includeDirectories);
            auto* includerPtr = includer.get();
            compileOptions.SetIncluder(std::move(includer));
            compileOptions.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);
            if (options.generateDebugInfo)
            {
                compileOptions.SetGenerateDebugInfo();
            }
            compileOptions.SetOptimizationLevel(options.optimize
                                                    ? shaderc_optimization_level_performance
                                                    : shaderc_optimization_level_zero);
            for (const auto& [name, value] : source.defines)
            {
                compileOptions.AddMacroDefinition(name, value);
            }

            const auto result = compiler.CompileGlslToSpv(
                sourceText, ShaderKind(source.stage), resolvedPath.string().c_str(),
                source.entryPoint.c_str(), compileOptions);
            if (result.GetCompilationStatus() != shaderc_compilation_status_success)
            {
                throw std::runtime_error(std::format("Shader compilation failed for {}:\n{}",
                                                     resolvedPath.string(), result.GetErrorMessage()));
            }
            spirv.assign(result.cbegin(), result.cend());
            dependencies = includerPtr->Dependencies();
            if (options.enableCache)
            {
                WriteCache(cachePath, spirv);
                WriteDependencyManifest(DependencyManifestPath(cachePath), m_FileSystem,
                                        dependencies);
            }
            if (m_Logger)
            {
                m_Logger->Debug("Shader compiled: {} ({} SPIR-V words)",
                                resolvedPath.string(), spirv.size());
            }
        }

        try
        {
            auto reflection = Reflect(spirv, source.stage);
            if (m_Logger)
            {
                m_Logger->Debug("Compiled shader {}: {} SPIR-V words, {} descriptors, "
                                "{} push constants, {} vertex inputs{}",
                                resolvedPath.string(), spirv.size(),
                                reflection.descriptorBindings.size(), reflection.pushConstants.size(),
                                reflection.vertexInputs.size(),
                                options.enableCache ? " (cache enabled)" : "");
            }
            return CompiledShader{
                .source = source,
                .resolvedPath = resolvedPath,
                .cachePath = cachePath,
                .spirv = spirv,
                .reflection = std::move(reflection),
                .sourceHash = hash,
                .dependencies = std::move(dependencies)
            };
        }
        catch (const std::exception& exception)
        {
            throw std::runtime_error(std::format("Shader reflection failed for {}: {}",
                                                 resolvedPath.string(), exception.what()));
        }
    }

    Task<CompiledShader> ShaderCompiler::CompileAsync(ShaderSource source,
                                                       ShaderCompileOptions options) const
    {
        co_await ResumeOn(ThreadPool::Instance());
        if (m_FileSystem->ResolvePath(source.path).extension() == ".spv")
        {
            co_return Compile(source, options);
        }
        const auto resolvedPath = m_FileSystem->ResolvePath(source.path);
        const auto sourceText = co_await m_FileSystem->ReadAllTextAsync(resolvedPath);
        co_return CompileText(source, options, resolvedPath, sourceText);
    }

    void ShaderCompiler::ClearCache(const ShaderCompileOptions& options) const
    {
        std::error_code error;
        std::filesystem::remove_all(CacheDirectory(m_FileSystem, options), error);
        if (error && m_Logger)
        {
            m_Logger->Warn("Failed to clear shader cache: {}", error.message());
        }
    }

    struct ShaderFileWatcher::State
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
        void PollState(const std::shared_ptr<ShaderFileWatcher::State>& state)
        {
            std::vector<ShaderFileChangedEvent> changes;
            {
                std::scoped_lock lock(state->mutex);
                if (!state->running)
                {
                    return;
                }
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

        void SchedulePoll(const std::shared_ptr<ShaderFileWatcher::State>& state)
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
        std::scoped_lock lock(m_State->mutex);
        const auto id = m_State->nextId++;
        m_State->entries.emplace(id, State::WatchEntry{
                                      .path = resolved,
                                      .lastWriteTime = writeTime,
                                      .existed = exists && !error
                                  });
        return id;
    }

    void ShaderFileWatcher::Unwatch(const std::size_t watchId)
    {
        std::scoped_lock lock(m_State->mutex);
        m_State->entries.erase(watchId);
    }

    void ShaderFileWatcher::PollNow()
    {
        PollState(m_State);
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
        SchedulePoll(m_State);
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

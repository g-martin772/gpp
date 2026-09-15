module;

export module GPP.Graphics:Shader;

import std;
import GPP.Core;

namespace GPP
{
    export enum class ShaderStage : std::uint8_t
    {
        Vertex,
        Fragment,
        Compute,
        Geometry,
        TessellationControl,
        TessellationEvaluation
    };

    export enum class ShaderStageFlags : std::uint32_t
    {
        None = 0,
        Vertex = 1u << 0,
        Fragment = 1u << 1,
        Compute = 1u << 2,
        Geometry = 1u << 3,
        TessellationControl = 1u << 4,
        TessellationEvaluation = 1u << 5
    };

    export constexpr ShaderStageFlags operator|(ShaderStageFlags lhs, ShaderStageFlags rhs) noexcept
    {
        return static_cast<ShaderStageFlags>(
            static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs));
    }

    export constexpr ShaderStageFlags& operator|=(ShaderStageFlags& lhs, ShaderStageFlags rhs) noexcept
    {
        lhs = lhs | rhs;
        return lhs;
    }

    export constexpr bool HasShaderStage(ShaderStageFlags flags, ShaderStageFlags stage) noexcept
    {
        return (static_cast<std::uint32_t>(flags) & static_cast<std::uint32_t>(stage)) != 0;
    }

    export enum class ShaderScalarType : std::uint8_t
    {
        Unknown,
        Boolean,
        SignedInteger,
        UnsignedInteger,
        Float,
        Double
    };

    export enum class ShaderDescriptorType : std::uint8_t
    {
        Unknown,
        Sampler,
        CombinedImageSampler,
        SampledImage,
        StorageImage,
        UniformTexelBuffer,
        StorageTexelBuffer,
        UniformBuffer,
        StorageBuffer,
        InputAttachment,
        AccelerationStructure
    };

    export struct ShaderDescriptorBinding
    {
        std::uint32_t set = 0;
        std::uint32_t binding = 0;
        std::uint32_t descriptorCount = 1;
        ShaderDescriptorType type = ShaderDescriptorType::Unknown;
        ShaderStageFlags stages = ShaderStageFlags::None;
        std::string name;
    };

    export struct ShaderPushConstantRange
    {
        std::uint32_t offset = 0;
        std::uint32_t size = 0;
        ShaderStageFlags stages = ShaderStageFlags::None;
    };

    export struct ShaderVertexInput
    {
        std::uint32_t location = 0;
        std::uint32_t components = 1;
        std::uint32_t bitWidth = 32;
        ShaderScalarType scalarType = ShaderScalarType::Unknown;
        std::string name;
    };

    export struct ShaderReflection
    {
        std::vector<ShaderDescriptorBinding> descriptorBindings;
        std::vector<ShaderPushConstantRange> pushConstants;
        std::vector<ShaderVertexInput> vertexInputs;
    };

    export ShaderReflection MergeShaderReflections(const ShaderReflection& first,
                                                   const ShaderReflection& second);

    export struct ShaderSource
    {
        std::filesystem::path path;
        ShaderStage stage = ShaderStage::Vertex;
        std::string entryPoint = "main";
        std::unordered_map<std::string, std::string> defines;
    };

    export struct ShaderCompileOptions
    {
        std::filesystem::path cacheDirectory;
        bool enableCache = true;
        bool generateDebugInfo = false;
        bool optimize = true;
    };

    export struct CompiledShader
    {
        ShaderSource source;
        std::filesystem::path resolvedPath;
        std::filesystem::path cachePath;
        std::vector<std::uint32_t> spirv;
        ShaderReflection reflection;
        std::uint64_t sourceHash = 0;
    };

    export class ShaderCompiler
    {
    public:
        explicit ShaderCompiler(std::shared_ptr<IFileSystem> fileSystem,
                                std::shared_ptr<Logger> logger);

        CompiledShader Compile(const ShaderSource& source,
                               const ShaderCompileOptions& options = {}) const;
        Task<CompiledShader> CompileAsync(ShaderSource source,
                                          ShaderCompileOptions options = {}) const;

        void ClearCache(const ShaderCompileOptions& options = {}) const;

    private:
        CompiledShader CompileText(const ShaderSource& source,
                                   const ShaderCompileOptions& options,
                                   const std::filesystem::path& resolvedPath,
                                   const std::string& sourceText) const;

        std::shared_ptr<IFileSystem> m_FileSystem;
        std::shared_ptr<Logger> m_Logger;
    };

    export struct ShaderFileChangedEvent
    {
        std::filesystem::path path;
        std::filesystem::file_time_type lastWriteTime{};
    };

    export class ShaderFileWatcher : public IService
    {
    public:
        struct State;

        using Dependencies = std::tuple<IFileSystem, EventDispatcher, Logger>;

        ShaderFileWatcher(std::shared_ptr<IFileSystem> fileSystem,
                          std::shared_ptr<EventDispatcher> dispatcher,
                          std::shared_ptr<Logger> logger);
        ~ShaderFileWatcher() override;

        std::size_t Watch(const std::filesystem::path& path);
        void Unwatch(std::size_t watchId);
        void PollNow();

        Task<void> StartAsync(std::chrono::milliseconds interval = std::chrono::milliseconds(250),
                              std::stop_token stopToken = {});
        Task<void> StopAsync();
        bool IsRunning() const noexcept;

    private:
        std::shared_ptr<State> m_State;
    };
}

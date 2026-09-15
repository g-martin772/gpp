export module GPP.Graphics:HotReload;

import vulkan;
import GPP.Core;
import :Shader;
import :Vulkan.Pipeline;

namespace GPP
{
    export enum class ShaderCompilationState : std::uint8_t
    {
        Idle,
        Compiling,
        Ready,
        Failed
    };

    export struct ShaderCompilationProgress
    {
        ShaderCompilationState state = ShaderCompilationState::Idle;
        std::uint32_t completedStages = 0;
        std::uint32_t totalStages = 0;
        std::uint64_t generation = 0;
        std::string activeStage;
        std::string message;
    };

    export struct ShaderPipelineMetadata
    {
        std::uint64_t vertexSourceHash = 0;
        std::uint64_t fragmentSourceHash = 0;
        std::size_t descriptorCount = 0;
        std::size_t pushConstantCount = 0;
        std::size_t vertexInputCount = 0;
        ShaderReflection reflection;
    };

    export struct ShaderPipelineDescription
    {
        ShaderSource vertex;
        ShaderSource fragment;
        ShaderCompileOptions compileOptions;
        std::chrono::milliseconds pollingInterval{250};
        bool enableHotReload = false;
    };

    export class ShaderPipeline
    {
    public:
        ShaderPipeline(std::shared_ptr<VulkanDevice> device,
                       VulkanPipelineSpecification pipelineSpecification,
                       ShaderPipelineDescription description,
                       std::shared_ptr<IFileSystem> fileSystem,
                       std::shared_ptr<EventDispatcher> dispatcher,
                       std::shared_ptr<Logger> logger = {});
        ~ShaderPipeline();

        ShaderPipeline(const ShaderPipeline&) = delete;
        ShaderPipeline& operator=(const ShaderPipeline&) = delete;

        struct CompiledPipeline;

        // Must be called by the render thread when Vulkan objects are created.
        bool StartOnRenderThread();
        Task<void> StartAsync(std::stop_token stopToken = {});
        Task<void> StopAsync();

        bool Reload();
        Task<bool> ReloadAsync();

        [[nodiscard]] std::shared_ptr<VulkanPipeline> GetPipeline() const;
        [[nodiscard]] ShaderReflection GetReflection() const;
        [[nodiscard]] ShaderCompilationProgress GetCompilationProgress() const;
        [[nodiscard]] ShaderPipelineMetadata GetMetadata() const;
        [[nodiscard]] std::string LastError() const;
        [[nodiscard]] bool IsRunning() const noexcept;

    private:
        Task<std::shared_ptr<CompiledPipeline>> CompileAsync();
        std::shared_ptr<CompiledPipeline> Compile();
        bool Install(std::shared_ptr<CompiledPipeline> compiled);
        void QueueReload();
        void HandleFileChanged(const ShaderFileChangedEvent& event);
        void HandleCompiled(const std::shared_ptr<CompiledPipeline>& compiled);
        void SetProgress(ShaderCompilationProgress progress);

        std::shared_ptr<VulkanDevice> m_Device;
        VulkanPipelineSpecification m_PipelineSpecification;
        ShaderPipelineDescription m_Description;
        ShaderCompiler m_Compiler;
        std::shared_ptr<Logger> m_Logger;
        std::shared_ptr<EventDispatcher> m_Dispatcher;
        ShaderFileWatcher m_Watcher;
        EventSubscription m_FileSubscription;
        EventSubscription m_CompiledSubscription;
        std::filesystem::path m_VertexPath;
        std::filesystem::path m_FragmentPath;

        mutable std::mutex m_Mutex;
        std::condition_variable m_ReloadCondition;
        std::shared_ptr<VulkanPipeline> m_CurrentPipeline;
        std::vector<std::shared_ptr<VulkanPipeline>> m_RetiredPipelines;
        ShaderReflection m_Reflection;
        ShaderPipelineMetadata m_Metadata;
        ShaderCompilationProgress m_Progress;
        std::string m_LastError;
        std::stop_source m_StopSource;
        bool m_Running = false;
        bool m_Reloading = false;
        bool m_CompilationInFlight = false;
    };

}

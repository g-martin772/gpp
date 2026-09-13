module;

#include <vulkan/vulkan.hpp>

export module GPP.Graphics:HotReload;

import std;
import GPP.Core;
import :Shader;
import :Vulkan.Pipeline;

namespace GPP
{
    export struct ShaderPipelineDescription
    {
        ShaderSource vertex;
        ShaderSource fragment;
        ShaderCompileOptions compileOptions;
        std::chrono::milliseconds pollingInterval{250};
    };

    export class HotReloadablePipeline
    {
    public:
        HotReloadablePipeline(std::shared_ptr<VulkanDevice> device,
                              vk::Format colorFormat,
                              vk::Format depthFormat,
                              ShaderPipelineDescription description,
                              std::shared_ptr<IFileSystem> fileSystem,
                              std::shared_ptr<EventDispatcher> dispatcher,
                              std::shared_ptr<Logger> logger = {});
        ~HotReloadablePipeline();

        HotReloadablePipeline(const HotReloadablePipeline&) = delete;
        HotReloadablePipeline& operator=(const HotReloadablePipeline&) = delete;

        Task<void> StartAsync(std::stop_token stopToken = {});
        Task<void> StopAsync();

        bool Reload();
        Task<bool> ReloadAsync();

        [[nodiscard]] std::shared_ptr<VulkanPipeline> GetPipeline() const;
        [[nodiscard]] ShaderReflection GetReflection() const;
        [[nodiscard]] std::string LastError() const;
        [[nodiscard]] bool IsRunning() const noexcept;

    private:
        void QueueReload();
        void HandleFileChanged(const ShaderFileChangedEvent& event);

        std::shared_ptr<VulkanDevice> m_Device;
        vk::Format m_ColorFormat;
        vk::Format m_DepthFormat;
        ShaderPipelineDescription m_Description;
        ShaderCompiler m_Compiler;
        std::shared_ptr<Logger> m_Logger;
        std::shared_ptr<EventDispatcher> m_Dispatcher;
        ShaderFileWatcher m_Watcher;
        EventSubscription m_FileSubscription;
        std::filesystem::path m_VertexPath;
        std::filesystem::path m_FragmentPath;

        mutable std::mutex m_Mutex;
        std::condition_variable m_ReloadCondition;
        std::shared_ptr<VulkanPipeline> m_CurrentPipeline;
        std::vector<std::shared_ptr<VulkanPipeline>> m_RetiredPipelines;
        ShaderReflection m_Reflection;
        std::string m_LastError;
        std::stop_source m_StopSource;
        bool m_Running = false;
        bool m_Reloading = false;
    };
}

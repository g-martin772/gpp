module;
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
export module GPP.Graphics:Renderer;

import std;
import GPP.Core;
import :Vulkan.Context;
import :Vulkan.Device;
import :Windowing.WindowManager;
import :Vulkan.Swapchain;
import :Vulkan.Command;
import :Vulkan.Pipeline;

namespace GPP
{
    class VulkanContext;
    class WindowManager;

    export class Renderer : public IHostedService
    {
    public:
        using Dependencies = std::tuple<VulkanContext, WindowManager, WindowOptions, Logger, IFileSystem>;

        Renderer(const std::shared_ptr<VulkanContext>& vulkanContext,
                 const std::shared_ptr<WindowManager>& windowManager,
                 const std::shared_ptr<WindowOptions>& windowOptions,
                 const std::shared_ptr<Logger>& logger,
                 const std::shared_ptr<IFileSystem>& fileSystem);

        Task<void> StartAsync(std::stop_token stopToken) override;
        Task<void> StopAsync() override;

        Task<void> AwaitReady()
        {
            co_await m_SharedFuture;
            co_return;
        }

        const std::shared_ptr<VulkanDevice>& GetDevice() const noexcept { return m_MainWindowResources.Device; }

        const std::shared_ptr<VulkanSwapChain>& GetSwapChain() const noexcept
        {
            return m_MainWindowResources.SwapChain;
        }

    private:
        Task<void> InitializeRenderSystem();
        Task<void> StopRenderSystem();
        void RenderLoop(std::stop_token stopToken);

        std::shared_ptr<VulkanContext> m_VulkanContext;
        std::shared_ptr<WindowManager> m_WindowManager;
        std::shared_ptr<WindowOptions> m_WindowOptions;
        std::shared_ptr<IFileSystem> m_FileSystem;
        std::shared_ptr<Logger> m_Logger;

        struct WindowResources
        {
            std::shared_ptr<GPP::Window> Window;
            vk::SurfaceKHR Surface;
            std::shared_ptr<VulkanDevice> Device;
            std::shared_ptr<VulkanSwapChain> SwapChain;
            std::shared_ptr<VulkanCommandPool> CommandPool;

            ~WindowResources();
        };

        struct FrameResources
        {
            VulkanCommandBuffer CommandBuffer;
            VulkanSemaphore ImageAvailableSemaphore;
            VulkanSemaphore RenderFinishedSemaphore;
            VulkanFence InFlightFence;

            FrameResources() = default;

            explicit FrameResources(VulkanCommandBuffer commandBuffer, vk::Device device) noexcept
                : CommandBuffer(std::move(commandBuffer)),
                  ImageAvailableSemaphore(device),
                  RenderFinishedSemaphore(device),
                  InFlightFence(device, true)
            {
            }

            FrameResources(const FrameResources&) = delete;
            FrameResources& operator=(const FrameResources&) = delete;
            FrameResources(FrameResources&&) noexcept = default;
            FrameResources& operator=(FrameResources&&) noexcept = default;
            ~FrameResources();
        };

        WindowResources m_MainWindowResources{};
        std::vector<FrameResources> m_FrameResources{};
        std::vector<VulkanSemaphore> m_RenderFinishedSemaphores{};
        uint32_t m_FrameIndex = 0;

        std::shared_ptr<VulkanPipeline> m_Pipeline;

        std::thread m_RenderThread;
        std::atomic<bool> m_Running{true};
        std::promise<void> m_ReadyPromise;
        std::shared_future<void> m_SharedFuture{m_ReadyPromise.get_future().share()};
    };
}

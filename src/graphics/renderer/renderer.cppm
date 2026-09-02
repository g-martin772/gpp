module;
#include <vulkan/vulkan.hpp>
export module GPP.Graphics:Renderer;

import std;
import GPP.Core;
import :Vulkan.Context;
import :Vulkan.Device;
import :Windowing.WindowManager;

namespace GPP
{
    class VulkanContext;
    class WindowManager;

    struct WindowResources
    {
        std::shared_ptr<GPP::Window> Window;
        vk::SurfaceKHR Surface;
        std::shared_ptr<VulkanDevice> Device;

        ~WindowResources()
        {
            if (Surface)
            {
                Device->GetInstance().destroySurfaceKHR(Surface);
            }
        }
    };

    export class Renderer : public IHostedService
    {
    public:
        using Dependencies = std::tuple<VulkanContext, WindowManager, WindowOptions, Logger>;

        Renderer(const std::shared_ptr<VulkanContext>& vulkanContext,
                 const std::shared_ptr<WindowManager>& windowManager,
                 const std::shared_ptr<WindowOptions>& windowOptions,
                 const std::shared_ptr<Logger>& logger)
            : m_VulkanContext(std::move(vulkanContext)),
              m_WindowManager(std::move(windowManager)),
              m_WindowOptions(std::move(windowOptions)),
              m_Logger(std::move(logger))
        {
        }

        Task<void> StartAsync(std::stop_token stopToken) override
        {
            m_RenderThread = std::thread([this, stopToken]()
            {
                RenderLoop(stopToken);
            });
            co_return;
        }

        Task<void> StopAsync() override
        {
            m_Running = false;
            if (m_RenderThread.joinable())
            {
                m_RenderThread.join();
            }
            co_return;
        }

        Task<void> AwaitReady()
        {
            co_await m_SharedFuture;
            co_return;
        }

        const std::shared_ptr<VulkanDevice>& GetDevice() const noexcept { return m_MainWindowResources.Device; }

    private:
        Task<void> InitializeRenderSystem()
        {
            co_await m_VulkanContext->Init();
            m_MainWindowResources.Window = co_await m_WindowManager->CreateWindow(*m_WindowOptions);
            VkSurfaceKHR surface;
            if (!co_await m_MainWindowResources.Window->CreateVulkanSurface(m_VulkanContext->GetInstance(), &surface))
            {
                m_Logger->Error("Failed to create Vulkan surface for window.");
                co_return;
            }
            m_MainWindowResources.Surface = surface;
            m_MainWindowResources.Device = std::make_shared<VulkanDevice>(
                DeviceRequirements{
                    .Graphics = true,
                    .Compute = false,
                    .Transfer = true,
                    .Sparse = true,
                    .Present = true,
                    .Surface = m_MainWindowResources.Surface
                }, m_VulkanContext, m_Logger);
            co_return;
        }

        void RenderLoop(std::stop_token stopToken)
        {
            InitializeRenderSystem().get();
            m_ReadyPromise.set_value();

            while (m_Running && !stopToken.stop_requested())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(16)); // Simulate ~60 FPS
            }
        }

        std::shared_ptr<VulkanContext> m_VulkanContext;
        std::shared_ptr<WindowManager> m_WindowManager;
        std::shared_ptr<WindowOptions> m_WindowOptions;
        std::shared_ptr<Logger> m_Logger;

        WindowResources m_MainWindowResources{};

        std::thread m_RenderThread;
        std::atomic<bool> m_Running{true};
        std::promise<void> m_ReadyPromise;
        std::shared_future<void> m_SharedFuture{m_ReadyPromise.get_future().share()};
    };
}

export module GPP.Graphics:Renderer;

import std;
import GPP.Core;
import :Vulkan.Context;

namespace GPP
{
    class VulkanContext;
    class WindowManager;

    export class Renderer : public IHostedService
    {
    public:
        using Dependencies = std::tuple<VulkanContext, WindowManager, Logger>;

        Renderer(const std::shared_ptr<VulkanContext>& vulkanContext, const std::shared_ptr<WindowManager>& windowManager,
                            const std::shared_ptr<Logger>& logger)
            : m_VulkanContext(std::move(vulkanContext)),
              m_WindowManager(std::move(windowManager)),
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

    private:
        void RenderLoop(std::stop_token stopToken)
        {
            m_VulkanContext->Init().get();

            while (m_Running && !stopToken.stop_requested())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(16)); // Simulate ~60 FPS
            }
        }

        std::shared_ptr<VulkanContext> m_VulkanContext;
        std::shared_ptr<WindowManager> m_WindowManager;
        std::shared_ptr<Logger> m_Logger;
        std::thread m_RenderThread;
        std::atomic<bool> m_Running{true};
    };
}

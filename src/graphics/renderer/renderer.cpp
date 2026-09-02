module;
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
module GPP.Graphics;

import std;
import GPP.Core;
import :Renderer;

namespace GPP
{
    Renderer::WindowResources::~WindowResources()
    {
        CommandPool = nullptr;
        SwapChain = nullptr;
        if (Surface)
        {
            Device->GetInstance().destroySurfaceKHR(Surface);
        }
        Device = nullptr;
        Window = nullptr;
    }

    Renderer::FrameResources::FrameResources(VulkanCommandBuffer commandBuffer) noexcept
        : CommandBuffer(std::move(commandBuffer))
    {
    }

    Renderer::FrameResources::~FrameResources()
    {
    }

    Renderer::Renderer(const std::shared_ptr<VulkanContext>& vulkanContext,
                       const std::shared_ptr<WindowManager>& windowManager,
                       const std::shared_ptr<WindowOptions>& windowOptions,
                       const std::shared_ptr<Logger>& logger)
        : m_VulkanContext(std::move(vulkanContext)),
          m_WindowManager(std::move(windowManager)),
          m_WindowOptions(std::move(windowOptions)),
          m_Logger(std::move(logger))
    {
    }

    Task<void> Renderer::StartAsync(std::stop_token stopToken)
    {
        m_RenderThread = std::thread([this, stopToken]()
        {
            RenderLoop(stopToken);
        });
        co_return;
    }

    Task<void> Renderer::StopAsync()
    {
        m_Running = false;
        if (m_RenderThread.joinable())
        {
            m_RenderThread.join();
        }
        co_return;
    }

    Task<void> Renderer::InitializeRenderSystem()
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
        m_MainWindowResources.SwapChain = std::make_shared<VulkanSwapChain>(
            m_MainWindowResources.Device,
            m_Logger,
            //m_MainWindowResources.Window->GetSize(),
            glm::uvec2{100,100},
            m_MainWindowResources.Surface
        );

        m_MainWindowResources.CommandPool = std::make_shared<VulkanCommandPool>(
            m_MainWindowResources.Device,
            m_Logger,
            m_MainWindowResources.Device->GetQueueIndices().Graphics
        );

        m_FrameResources.reserve(2);
        for (int i = 0; i < 2; i++)
        {
            auto cb = m_MainWindowResources.CommandPool->AllocateCommandBuffer();
            m_FrameResources.emplace_back(std::move(cb));
        }

        co_return;
    }

    Task<void> Renderer::StopRenderSystem()
    {
        co_return;
    }

    void Renderer::RenderLoop(std::stop_token stopToken)
    {
        InitializeRenderSystem().get();
        m_ReadyPromise.set_value();
        int iterations = 0;
        std::chrono::high_resolution_clock::time_point start = std::chrono::high_resolution_clock::now();
        while (m_Running && !stopToken.stop_requested())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(16)); // Simulate ~60 FPS
            //m_MainWindowResources.SwapChain->Update(glm::uvec2{100, 100});
            iterations++;

            FrameResources& frame = m_FrameResources[m_FrameIndex];
            m_FrameIndex = (m_FrameIndex + 1) % m_FrameResources.size();

            frame.CommandBuffer.Begin();
            frame.CommandBuffer.End();
            //frame.CommandBuffer.Submit();
        }
        std::chrono::high_resolution_clock::time_point end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        auto averageFps = iterations / (duration.count() / 1000.0);
        m_Logger->Info("Render thread completed. Iterations: {}, Time: {} ms, Average FPS: {}",
            iterations, duration.count(), averageFps);
    }
}

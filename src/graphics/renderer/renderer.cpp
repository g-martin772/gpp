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
        if (Device)
        {
            Device->WaitIdle();
        }

        CommandPool = nullptr;
        SwapChain = nullptr;
        if (Surface)
        {
            Device->GetInstance().destroySurfaceKHR(Surface);
        }
        Device = nullptr;
        Window = nullptr;
    }

    Renderer::FrameResources::~FrameResources()
    {
    }

    Renderer::Renderer(const std::shared_ptr<VulkanContext>& vulkanContext,
                       const std::shared_ptr<WindowManager>& windowManager,
                       const std::shared_ptr<WindowOptions>& windowOptions,
                       const std::shared_ptr<Logger>& logger,
                       const std::shared_ptr<IFileSystem>& fileSystem)
        : m_VulkanContext(std::move(vulkanContext)),
          m_WindowManager(std::move(windowManager)),
          m_WindowOptions(std::move(windowOptions)),
          m_Logger(std::move(logger)),
          m_FileSystem(std::move(fileSystem))
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
            glm::uvec2{100, 100},
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
            m_FrameResources.emplace_back(std::move(FrameResources(
                m_MainWindowResources.CommandPool->AllocateCommandBuffer(),
                m_MainWindowResources.Device->GetDevice())));
        }

        m_RenderFinishedSemaphores.clear();
        const uint32_t imageCount = m_MainWindowResources.SwapChain->GetImageCount();
        m_RenderFinishedSemaphores.reserve(imageCount);
        for (uint32_t i = 0; i < imageCount; ++i)
        {
            m_RenderFinishedSemaphores.emplace_back(VulkanSemaphore(m_MainWindowResources.Device->GetDevice()));
        }

        auto readSpirvWords = [this](const std::string& path)
        {
            std::vector<std::byte> bytes = m_FileSystem->ReadAllBytes(path);
            if (bytes.size() % sizeof(std::uint32_t) != 0)
            {
                throw std::runtime_error("SPIR-V bytecode size is not aligned to uint32_t words: " + path);
            }

            std::vector<std::uint32_t> words(bytes.size() / sizeof(std::uint32_t));
            std::memcpy(words.data(), bytes.data(), bytes.size());
            return words;
        };

        auto vertSpirv = readSpirvWords("shaders/vert.spv");
        auto fragSpirv = readSpirvWords("shaders/frag.spv");

        m_Pipeline = std::make_shared<VulkanPipeline>(
            m_MainWindowResources.Device,
            m_MainWindowResources.SwapChain->GetImageFormat(),
            m_MainWindowResources.SwapChain->GetDepthImageFormat(),
            std::span<const std::uint32_t>(vertSpirv),
            std::span<const std::uint32_t>(fragSpirv)
        );

        co_return;
    }

    Task<void> Renderer::StopRenderSystem()
    {
        co_return;
    }

    inline vk::ImageAspectFlags GetImageAspectMask(vk::Format format, vk::ImageLayout newLayout)
    {
        if (newLayout == vk::ImageLayout::eDepthStencilAttachmentOptimal ||
            newLayout == vk::ImageLayout::eDepthAttachmentOptimal ||
            newLayout == vk::ImageLayout::eDepthReadOnlyOptimal)
        {
            vk::ImageAspectFlags aspectMask = vk::ImageAspectFlagBits::eDepth;

            if (format == vk::Format::eD24UnormS8Uint ||
                format == vk::Format::eD32SfloatS8Uint ||
                format == vk::Format::eD16UnormS8Uint)
            {
                aspectMask |= vk::ImageAspectFlagBits::eStencil;
            }
            return aspectMask;
        }
        return vk::ImageAspectFlagBits::eColor;
    }

    void TransitionImageLayout(
        vk::CommandBuffer commandBuffer,
        vk::Image image,
        vk::Format format,
        vk::ImageLayout oldLayout,
        vk::ImageLayout newLayout
    )
    {
        vk::ImageMemoryBarrier2 barrier{};
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = GetImageAspectMask(format, newLayout);
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        if (oldLayout == vk::ImageLayout::eUndefined &&
            newLayout == vk::ImageLayout::eColorAttachmentOptimal)
        {
            barrier.srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe;
            barrier.srcAccessMask = vk::AccessFlagBits2::eNone;
            barrier.dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
            barrier.dstAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite;
        }
        else if (oldLayout == vk::ImageLayout::eColorAttachmentOptimal &&
            newLayout == vk::ImageLayout::ePresentSrcKHR)
        {
            barrier.srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
            barrier.srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite;
            barrier.dstStageMask = vk::PipelineStageFlagBits2::eBottomOfPipe;
            barrier.dstAccessMask = vk::AccessFlagBits2::eNone;
        }
        else
        {
            return;
        }

        vk::DependencyInfo dependencyInfo{};
        dependencyInfo.imageMemoryBarrierCount = 1;
        dependencyInfo.pImageMemoryBarriers = &barrier;

        commandBuffer.pipelineBarrier2(dependencyInfo);
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

            vk::Device device = m_MainWindowResources.Device->GetDevice();
            const auto& swapchain = m_MainWindowResources.SwapChain;

            frame.InFlightFence.WaitAndReset();

            swapchain->AcquireNextImage(
                frame.ImageAvailableSemaphore.GetSemaphore(),
                VK_NULL_HANDLE
            );

            auto imageIndex = swapchain->GetCurrentImageIndex();
            // maybe cancel early when aquire fails or something

            // once we know we are taking the frame
            m_FrameIndex = (m_FrameIndex + 1) % m_FrameResources.size();

            auto& cmd = frame.CommandBuffer;
            cmd.Begin();
            {
                vk::CommandBuffer rawCmd = cmd.GetCommandBuffer();

                TransitionImageLayout(
                    rawCmd,
                    swapchain->GetImages()[imageIndex],
                    swapchain->GetImageFormat(),
                    vk::ImageLayout::eUndefined,
                    vk::ImageLayout::eColorAttachmentOptimal
                );

                // Begin dynamic rendering directly inside the command buffer
                vk::RenderingAttachmentInfo colorAttachment{};
                colorAttachment.imageView = swapchain->GetImageViews()[imageIndex];
                colorAttachment.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
                colorAttachment.loadOp = vk::AttachmentLoadOp::eClear;
                colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
                colorAttachment.clearValue = vk::ClearValue(vk::ClearColorValue(0.05f, 0.05f, 0.05f, 1.00f));
                // Charcoal gray background

                vk::RenderingInfo renderingInfo{};
                renderingInfo.renderArea = vk::Rect2D({0, 0}, swapchain->GetExtent());
                renderingInfo.layerCount = 1;
                renderingInfo.colorAttachmentCount = 1;
                renderingInfo.pColorAttachments = &colorAttachment;

                rawCmd.beginRendering(renderingInfo);
                {
                    vk::Viewport viewport{
                        0.0f, 0.0f,
                        static_cast<float>(swapchain->GetExtent().width),
                        static_cast<float>(swapchain->GetExtent().height),
                        0.0f, 1.0f
                    };
                    vk::Rect2D scissor{{0, 0}, swapchain->GetExtent()};
                    rawCmd.setViewport(0, 1, &viewport);
                    rawCmd.setScissor(0, 1, &scissor);

                    rawCmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_Pipeline->GetPipeline());
                    rawCmd.draw(3, 1, 0, 0);
                }
                rawCmd.endRendering();

                TransitionImageLayout(
                    rawCmd,
                    swapchain->GetImages()[imageIndex],
                    swapchain->GetImageFormat(),
                    vk::ImageLayout::eColorAttachmentOptimal,
                    vk::ImageLayout::ePresentSrcKHR
                );
            }
            cmd.End();

            vk::SubmitInfo submitInfo{};

            // wait for swapchain
            vk::Semaphore waitSemaphores[] = {frame.ImageAvailableSemaphore.GetSemaphore()};
            vk::PipelineStageFlags waitStages[] = {vk::PipelineStageFlagBits::eColorAttachmentOutput};
            submitInfo.waitSemaphoreCount = 1;
            submitInfo.pWaitSemaphores = waitSemaphores;
            submitInfo.pWaitDstStageMask = waitStages;

            // signal render finished using one semaphore per swapchain image so the
            // previous present operation cannot still be using the same semaphore.
            auto& renderFinishedSemaphore = m_RenderFinishedSemaphores[imageIndex];
            vk::Semaphore signalSemaphores[] = {renderFinishedSemaphore.GetSemaphore()};
            submitInfo.signalSemaphoreCount = 1;
            submitInfo.pSignalSemaphores = signalSemaphores;

            vk::CommandBuffer commandBuffers[] = {cmd.GetCommandBuffer()};
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = commandBuffers;

            auto result = m_MainWindowResources.Device->GetGraphicsQueue().submit(
                1, &submitInfo, frame.InFlightFence.GetFence());

            swapchain->Present(
                m_MainWindowResources.Device->GetPresentQueue(),
                renderFinishedSemaphore.GetSemaphore()
            );
        }
        m_MainWindowResources.Device->WaitIdle();
        std::chrono::high_resolution_clock::time_point end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        auto averageFps = iterations / (duration.count() / 1000.0);
        m_Logger->Info("Render thread completed. Iterations: {}, Time: {} ms, Average FPS: {}",
                       iterations, duration.count(), averageFps);
    }
}

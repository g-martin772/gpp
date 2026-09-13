module;
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
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
                       const std::shared_ptr<IFileSystem>& fileSystem,
                       const std::shared_ptr<InputState>& inputState,
                       const std::shared_ptr<EventDispatcher>& dispatcher)
        : m_VulkanContext(std::move(vulkanContext)),
          m_WindowManager(std::move(windowManager)),
          m_WindowOptions(std::move(windowOptions)),
          m_Logger(std::move(logger)),
          m_FileSystem(std::move(fileSystem)),
          m_InputState(std::move(inputState)),
          m_Dispatcher(std::move(dispatcher))
    {
    }

    ShaderCompilationProgress Renderer::GetShaderCompilationProgress() const
    {
        return m_ShaderPipeline
                   ? m_ShaderPipeline->GetCompilationProgress()
                   : ShaderCompilationProgress{};
    }

    ShaderPipelineMetadata Renderer::GetShaderPipelineMetadata() const
    {
        return m_ShaderPipeline
                   ? m_ShaderPipeline->GetMetadata()
                   : ShaderPipelineMetadata{};
    }

    std::string Renderer::GetShaderPipelineError() const
    {
        return m_ShaderPipeline ? m_ShaderPipeline->LastError() : std::string{};
    }

    Task<void> Renderer::StartAsync(std::stop_token stopToken)
    {
        m_Dispatcher->SetRenderExecutor([this](std::move_only_function<void()> task)
        {
            std::scoped_lock lock(m_RenderQueueMutex);
            m_RenderQueue.push(std::move(task));
        });
        m_ResizeSubscription = m_Dispatcher->Subscribe<WindowResizedEvent>(
            [this](const WindowResizedEvent& event)
            {
                std::scoped_lock lock(m_RenderQueueMutex);
                m_PendingResize[event.Window] = glm::uvec2{
                    static_cast<uint32_t>(std::max(event.Width, 1)),
                    static_cast<uint32_t>(std::max(event.Height, 1))
                };
            }, EventDelivery::Async, EventTarget::Render);
        m_RenderThread = std::thread([this, stopToken]()
        {
            RenderLoop(stopToken);
        });
        co_return;
    }

    Task<void> Renderer::StopAsync()
    {
        m_ResizeSubscription.Reset();
        m_Dispatcher->SetRenderExecutor({});
        m_Running = false;
        if (m_RenderThread.joinable())
        {
            m_RenderThread.join();
        }
        co_return;
    }

    void Renderer::InitializeRenderSystem()
    {
        m_VulkanContext->Init().get();

        m_FileSystem->RegisterAssetDirectory("shaders", m_FileSystem->GetBinaryDirectory() / ".." / "shaders");

        m_MainWindowResources.Window = m_WindowManager->CreateWindow(*m_WindowOptions).get();
        VkSurfaceKHR surface;
        if (!m_MainWindowResources.Window->CreateVulkanSurface(
            m_VulkanContext->GetInstance(), &surface).get())
        {
            m_Logger->Error("Failed to create Vulkan surface for window.");
            return;
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
            glm::uvec2{10000, 10000},
            m_MainWindowResources.Surface
        );
        const auto depthFormat = m_MainWindowResources.SwapChain->GetDepthImageFormat();
        const auto depthAspectMask = GetImageAspectMask(depthFormat);
        const bool depthHasStencil = (depthAspectMask & vk::ImageAspectFlagBits::eStencil) != vk::ImageAspectFlags{};
        m_MainWindowResources.DepthImage.Create(
            m_MainWindowResources.Device,
            VulkanImageSpecification{
                .extent = {
                    m_MainWindowResources.SwapChain->GetExtent().width,
                    m_MainWindowResources.SwapChain->GetExtent().height, 1
                },
                .format = depthFormat,
                .usage = vk::ImageUsageFlagBits::eDepthStencilAttachment,
                .aspectMask = depthAspectMask,
                .debugName = "DemoDepthBuffer"
            });
        m_MainWindowResources.DepthLayout = vk::ImageLayout::eUndefined;

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
        m_SwapchainImageLayouts.assign(imageCount, vk::ImageLayout::eUndefined);
        m_RenderFinishedSemaphores.reserve(imageCount);
        for (uint32_t i = 0; i < imageCount; ++i)
        {
            m_RenderFinishedSemaphores.emplace_back(VulkanSemaphore(m_MainWindowResources.Device->GetDevice()));
        }

        const auto vertexSource = m_FileSystem->ResolveAssetPath("shaders", "vert.vert");
        const auto fragmentSource = m_FileSystem->ResolveAssetPath("shaders", "frag.frag");
        m_ShaderPipeline = std::make_shared<ShaderPipeline>(
            m_MainWindowResources.Device,
            VulkanPipelineSpecification{
                .colorFormat = m_MainWindowResources.SwapChain->GetImageFormat(),
                .depthFormat = m_MainWindowResources.SwapChain->GetDepthImageFormat(),
                .enableBlending = false,
                .cullMode = vk::CullModeFlagBits::eBack,
                .frontFace = vk::FrontFace::eCounterClockwise
            },
            ShaderPipelineDescription{
                .vertex = ShaderSource{.path = vertexSource, .stage = ShaderStage::Vertex},
                .fragment = ShaderSource{.path = fragmentSource, .stage = ShaderStage::Fragment},
                .enableHotReload = true
            },
            m_FileSystem, m_Dispatcher, m_Logger);
        if (!m_ShaderPipeline->StartOnRenderThread())
        {
            throw std::runtime_error(m_ShaderPipeline->LastError());
        }

        struct Vertex
        {
            glm::vec3 position;
            glm::vec3 color;
        };
        constexpr std::array vertices{
            Vertex{{-1, -1, -1}, {1, 0, 0}}, Vertex{{1, -1, -1}, {0, 1, 0}},
            Vertex{{1, 1, -1}, {0, 0, 1}}, Vertex{{-1, 1, -1}, {1, 1, 0}},
            Vertex{{-1, -1, 1}, {1, 0, 1}}, Vertex{{1, -1, 1}, {0, 1, 1}},
            Vertex{{1, 1, 1}, {1, 1, 1}}, Vertex{{-1, 1, 1}, {0.2f, 0.2f, 0.2f}}
        };
        constexpr std::array<std::uint32_t, 36> indices{
            0, 1, 2, 2, 3, 0, 1, 5, 6, 6, 2, 1,
            5, 4, 7, 7, 6, 5, 4, 0, 3, 3, 7, 4,
            3, 2, 6, 6, 7, 3, 4, 5, 1, 1, 0, 4
        };
        m_VertexBuffer.Create(m_MainWindowResources.Device,
                              MakeVertexBufferSpecification(sizeof(vertices), true));
        m_VertexBuffer.Upload(vertices.data(), sizeof(vertices));
        m_IndexBuffer.Create(m_MainWindowResources.Device,
                             MakeIndexBufferSpecification(sizeof(indices), true));
        m_IndexBuffer.Upload(indices.data(), sizeof(indices));
        m_IndexCount = static_cast<std::uint32_t>(indices.size());
    }

    Task<void> Renderer::StopRenderSystem()
    {
        co_return;
    }

    void Renderer::RenderLoop(std::stop_token stopToken)
    {
        InitializeRenderSystem();
        m_ReadyPromise.set_value();
        int iterations = 0;
        std::chrono::high_resolution_clock::time_point start = std::chrono::high_resolution_clock::now();
        while (m_Running && !stopToken.stop_requested())
        {
            std::queue<std::move_only_function<void()>> pendingTasks;
            {
                std::scoped_lock lock(m_RenderQueueMutex);
                pendingTasks.swap(m_RenderQueue);
            }
            while (!pendingTasks.empty())
            {
                auto task = std::move(pendingTasks.front());
                pendingTasks.pop();
                task();
            }
            {
                std::scoped_lock lock(m_RenderQueueMutex);
                if (m_MainWindowResources.Window && m_MainWindowResources.SwapChain)
                {
                    const auto windowId = m_MainWindowResources.Window->GetID();
                    if (auto it = m_PendingResize.find(windowId); it != m_PendingResize.end())
                    {
                        m_MainWindowResources.SwapChain->Update(it->second);
                        m_MainWindowResources.DepthImage.Resize(
                            {
                                m_MainWindowResources.SwapChain->GetExtent().width,
                                m_MainWindowResources.SwapChain->GetExtent().height, 1
                            });
                        m_MainWindowResources.DepthLayout = vk::ImageLayout::eUndefined;
                        m_SwapchainImageLayouts.assign(
                            m_MainWindowResources.SwapChain->GetImageCount(),
                            vk::ImageLayout::eUndefined);
                        m_PendingResize.erase(it);
                    }
                }
            }
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
                const auto depthFormat = swapchain->GetDepthImageFormat();
                const bool depthHasStencil = (GetImageAspectMask(depthFormat) & vk::ImageAspectFlagBits::eStencil) != vk::ImageAspectFlags{};
                const auto depthTargetLayout = depthHasStencil
                    ? vk::ImageLayout::eDepthStencilAttachmentOptimal
                    : vk::ImageLayout::eDepthAttachmentOptimal;
 
                TransitionImageLayout(
                    rawCmd,
                    swapchain->GetImages()[imageIndex],
                    swapchain->GetImageFormat(),
                    m_SwapchainImageLayouts[imageIndex],
                    vk::ImageLayout::eColorAttachmentOptimal
                );
                TransitionImageLayout(
                    rawCmd,
                    m_MainWindowResources.DepthImage.GetImage(),
                    depthFormat,
                    m_MainWindowResources.DepthLayout,
                    depthTargetLayout
                );
                m_SwapchainImageLayouts[imageIndex] = vk::ImageLayout::eColorAttachmentOptimal;
                m_MainWindowResources.DepthLayout = depthTargetLayout;

                // Begin dynamic rendering directly inside the command buffer
                vk::RenderingAttachmentInfo colorAttachment{};
                colorAttachment.imageView = swapchain->GetImageViews()[imageIndex];
                colorAttachment.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
                colorAttachment.loadOp = vk::AttachmentLoadOp::eClear;
                colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
                colorAttachment.clearValue = vk::ClearValue(vk::ClearColorValue(0.05f, 0.05f, 0.05f, 1.00f));
                vk::RenderingAttachmentInfo depthAttachment{};
                depthAttachment.imageView = m_MainWindowResources.DepthImage.GetImageView();
                depthAttachment.imageLayout = vk::ImageLayout::eDepthAttachmentOptimal;
                depthAttachment.loadOp = vk::AttachmentLoadOp::eClear;
                depthAttachment.storeOp = vk::AttachmentStoreOp::eDontCare;
                depthAttachment.clearValue = vk::ClearValue(vk::ClearDepthStencilValue(1.0f, 0));

                vk::RenderingInfo renderingInfo{};
                renderingInfo.renderArea = vk::Rect2D({0, 0}, swapchain->GetExtent());
                renderingInfo.layerCount = 1;
                renderingInfo.colorAttachmentCount = 1;
                renderingInfo.pColorAttachments = &colorAttachment;
                renderingInfo.pDepthAttachment = &depthAttachment;

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

                    auto pipeline = m_ShaderPipeline ? m_ShaderPipeline->GetPipeline() : nullptr;
                    if (pipeline)
                    {
                        rawCmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline->GetPipeline());
                        BindVertexBuffer(rawCmd, m_VertexBuffer.GetBuffer());
                        BindIndexBuffer(rawCmd, m_IndexBuffer.GetBuffer(), 0, vk::IndexType::eUint32);

                        struct PushConstants
                        {
                            glm::mat4 viewProjection;
                            glm::mat4 model;
                            float time;
                        } pushConstants{};
                        const float elapsed = static_cast<float>(
                            std::chrono::duration<double>(
                                std::chrono::high_resolution_clock::now() - start).count());
                        const float aspect = static_cast<float>(swapchain->GetExtent().width) /
                            static_cast<float>(swapchain->GetExtent().height);
                        pushConstants.viewProjection = glm::perspective(
                            glm::radians(45.0f), aspect, 0.1f, 100.0f);
                        pushConstants.viewProjection[1][1] *= -1.0f;
                        pushConstants.model = glm::translate(
                            glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -4.0f));
                        pushConstants.model = glm::rotate(
                            pushConstants.model, elapsed, glm::vec3(0.5f, 1.0f, 0.0f));
                        pushConstants.time = elapsed;
                        rawCmd.pushConstants(
                            pipeline->GetLayout(), vk::ShaderStageFlagBits::eVertex,
                            0, sizeof(pushConstants), &pushConstants);
                        rawCmd.drawIndexed(m_IndexCount, 1, 0, 0, 0);
                    }
                }
                rawCmd.endRendering();

                TransitionImageLayout(
                    rawCmd,
                    swapchain->GetImages()[imageIndex],
                    swapchain->GetImageFormat(),
                    vk::ImageLayout::eColorAttachmentOptimal,
                    vk::ImageLayout::ePresentSrcKHR
                );
                m_SwapchainImageLayouts[imageIndex] = vk::ImageLayout::ePresentSrcKHR;
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

            // m_Logger->Trace("W key pressed: {}", m_InputState->IsKeyDown(ScanCode::W));
        }
        m_MainWindowResources.Device->WaitIdle();
        if (m_ShaderPipeline)
        {
            m_ShaderPipeline->StopAsync().get();
            std::queue<std::move_only_function<void()>> shutdownTasks;
            {
                std::scoped_lock lock(m_RenderQueueMutex);
                shutdownTasks.swap(m_RenderQueue);
            }
            while (!shutdownTasks.empty())
            {
                auto task = std::move(shutdownTasks.front());
                shutdownTasks.pop();
                task();
            }
            m_ShaderPipeline.reset();
        }
        m_VertexBuffer.Destroy();
        m_IndexBuffer.Destroy();
        m_RenderFinishedSemaphores.clear();
        m_FrameResources.clear();
        m_MainWindowResources.CommandPool.reset();
        m_MainWindowResources.SwapChain.reset();
        if (m_MainWindowResources.Surface)
        {
            m_MainWindowResources.Device->GetInstance().destroySurfaceKHR(m_MainWindowResources.Surface);
            m_MainWindowResources.Surface = nullptr;
        }
        m_MainWindowResources.Window.reset();
        m_MainWindowResources.Device.reset();
        std::chrono::high_resolution_clock::time_point end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        auto averageFps = iterations / (duration.count() / 1000.0);
        m_Logger->Info("Render thread completed. Iterations: {}, Time: {} ms, Average FPS: {}",
                       iterations, duration.count(), averageFps);
    }
}

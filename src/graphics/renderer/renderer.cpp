module;
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
module GPP.Graphics;

import std;
import glm;
import vulkan;
import GPP.Core;
import :Renderer;
import :UI;

namespace GPP
{
    Renderer::WindowResources::~WindowResources()
    {
        SwapChain = nullptr;
        Window = nullptr;
    }

    Renderer::FrameResources::FrameResources(VulkanCommandBuffer commandBuffer, vk::Device device) noexcept :
        CommandBuffer(std::move(commandBuffer)),
        ImageAvailableSemaphore(device),
        RenderFinishedSemaphore(device),
        InFlightFence(device, true)
    {
    }

    Renderer::FrameResources::~FrameResources()
    {
    }

    Renderer::Renderer(const std::shared_ptr<VulkanContext>& vulkanContext,
                       const std::shared_ptr<WindowManager>& windowManager,
                       const std::shared_ptr<WindowOptions>& windowOptions,
                       const std::shared_ptr<WindowDefinitions>& windowDefinitions,
                       const std::shared_ptr<RenderOptions>& renderOptions,
                       const std::shared_ptr<Logger>& logger,
                       const std::shared_ptr<IFileSystem>& fileSystem,
                       const std::shared_ptr<InputState>& inputState,
                       const std::shared_ptr<EventDispatcher>& dispatcher)
        : m_VulkanContext(std::move(vulkanContext)),
          m_WindowManager(std::move(windowManager)),
          m_WindowOptions(std::move(windowOptions)),
        m_WindowDefinitions(std::move(windowDefinitions)),
          m_RenderOptions(std::move(renderOptions)),
          m_Logger(std::move(logger)),
          m_FileSystem(std::move(fileSystem)),
          m_InputState(std::move(inputState)),
          m_Dispatcher(std::move(dispatcher))
    {
    }

    const std::shared_ptr<VulkanSwapChain>& Renderer::GetSwapChain(WindowId id) const noexcept
    {
        if (m_MainWindowResources.Window && id == m_MainWindowResources.Window->GetID())
        {
            return m_MainWindowResources.SwapChain;
        }
        static const std::shared_ptr<VulkanSwapChain> empty;
        const auto it = m_WindowResources.find(id);
        return it == m_WindowResources.end() ? empty : it->second.SwapChain;
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

    void Renderer::AttachLayerStackToWindow(GuiLayerStack& layerStack, const std::shared_ptr<Window>& window)
    {
        if (!window) return;
        std::scoped_lock lock(m_RenderQueueMutex);
        m_RenderQueue.push([this, &layerStack, window]
        {
            const auto id = window->GetID();
            if (m_MainWindowResources.Window && id == m_MainWindowResources.Window->GetID())
            {
                m_MainLayerStack = &layerStack;
                m_MainWindowResources.LayerStack = &layerStack;
            }
            else if (const auto it = m_WindowResources.find(id); it != m_WindowResources.end())
            {
                it->second.LayerStack = &layerStack;
            }
            layerStack.OnAttach();
        });
    }

    Task<std::shared_ptr<Window>> Renderer::CreateWindow(const WindowOptions& options, std::string name)
    {
        if (m_WindowManager->IsHeadless())
        {
            throw std::runtime_error("Cannot create a window while running headless.");
        }
        auto window = co_await m_WindowManager->CreateWindow(options, std::move(name));
        {
            std::scoped_lock lock(m_RenderQueueMutex);
            m_RenderQueue.push([this, window]
            {
                auto& resources = m_WindowResources[window->GetID()];
                resources.Window = window;
                InitializeWindowResources(window, resources);
                InitializeWindowSync(resources);
            });
        }
        co_return window;
    }

    void Renderer::AttachLayerStackToBuffer(GuiLayerStack& layerStack, std::uint32_t bufferId)
    {
        std::scoped_lock lock(m_RenderQueueMutex);
        m_RenderQueue.push([this, &layerStack, bufferId]
        {
            if (m_BufferTargets.contains(bufferId))
                return;

            auto& target = m_BufferTargets[bufferId];
            target.LayerStack = &layerStack;
            target.ColorImage.Create(
                m_Device,
                VulkanImageSpecification{
                    .extent = {target.Extent.x, target.Extent.y, 1},
                    .format = m_MainWindowResources.SwapChain
                                  ? m_MainWindowResources.SwapChain->GetImageFormat()
                                  : vk::Format::eB8G8R8A8Unorm,
                    .usage = vk::ImageUsageFlagBits::eColorAttachment |
                    vk::ImageUsageFlagBits::eSampled,
                    .aspectMask = vk::ImageAspectFlagBits::eColor,
                    .createSampler = true,
                    .debugName = "BufferTargetColor"
                });
            const auto depthFormat = m_MainWindowResources.SwapChain
                                         ? m_MainWindowResources.SwapChain->GetDepthImageFormat()
                                         : m_Device->GetDepthFormat();
            target.DepthImage.Create(
                m_Device,
                VulkanImageSpecification{
                    .extent = {target.Extent.x, target.Extent.y, 1},
                    .format = depthFormat,
                    .usage = vk::ImageUsageFlagBits::eDepthStencilAttachment,
                    .aspectMask = GetImageAspectMask(depthFormat),
                    .debugName = "BufferTargetDepth"
                });
            target.ImGuiTexture = reinterpret_cast<void*>(
                m_ImGuiContext
                    ? reinterpret_cast<void*>(ImGui_ImplVulkan_AddTexture(
                        target.ColorImage.GetSampler(),
                        target.ColorImage.GetImageView(),
                        static_cast<VkImageLayout>(vk::ImageLayout::eShaderReadOnlyOptimal)))
                    : nullptr);
            target.LayerStack->OnAttach();
            m_Logger->Info("Created render target {}", bufferId);
        });
    }

    std::optional<Renderer::RenderTargetInfo> Renderer::GetRenderTargetInfo(
        const std::uint32_t bufferId) const
    {
        const auto it = m_BufferTargets.find(bufferId);
        if (it == m_BufferTargets.end())
            return std::nullopt;
        return RenderTargetInfo{
            vk::Extent2D{it->second.Extent.x, it->second.Extent.y},
            it->second.ColorImage.GetImageView(),
            it->second.ColorImage.GetSampler(),
            it->second.ImGuiTexture
        };
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
                    static_cast<std::uint32_t>(std::max(event.Width, 1)),
                    static_cast<std::uint32_t>(std::max(event.Height, 1))
                };
            }, EventDelivery::Async, EventTarget::Render);
        m_WindowCloseSubscription = m_Dispatcher->Subscribe<WindowCloseRequestedEvent>(
            [this](const WindowCloseRequestedEvent& event)
            {
                std::scoped_lock lock(m_RenderQueueMutex);
                m_RenderQueue.push([this, id = event.Window]
                {
                    if (m_Device)
                    {
                        m_Device->WaitIdle();
                    }
                    if (m_MainWindowResources.Window &&
                        m_MainWindowResources.Window->GetID() == id)
                    {
                        m_MainWindowResources.SwapChain.reset();
                        if (m_MainWindowResources.Surface)
                        {
                            m_Device->GetInstance().destroySurfaceKHR(
                                m_MainWindowResources.Surface);
                            m_MainWindowResources.Surface = nullptr;
                        }
                        if (m_MainWindowResources.LayerStack)
                        {
                            m_MainWindowResources.LayerStack->OnDetach();
                        }
                        m_MainWindowResources.Window.reset();
                        return;
                    }
                    const auto it = m_WindowResources.find(id);
                    if (it != m_WindowResources.end())
                    {
                        it->second.SwapChain.reset();
                        if (it->second.Surface)
                        {
                            m_Device->GetInstance().destroySurfaceKHR(it->second.Surface);
                            it->second.Surface = nullptr;
                        }
                        if (it->second.LayerStack)
                        {
                            it->second.LayerStack->OnDetach();
                        }
                        m_WindowResources.erase(it);
                    }
                });
            }, EventDelivery::Async, EventTarget::Render);
        m_ImGuiInputSubscriptions.push_back(m_Dispatcher->Subscribe<ImGuiRawEvent>(
            [this](const ImGuiRawEvent& source)
            {
                std::scoped_lock lock(m_RenderQueueMutex);
                m_RenderQueue.push([this, source]
                {
                    if (!m_ImGuiContext)
                        return;
                    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(m_ImGuiContext));
                    if (source.Type == SDL_EVENT_WINDOW_FOCUS_GAINED)
                    {
                        m_ImGuiInputWindow = source.Window;
                        return;
                    }
                    if (source.Type == SDL_EVENT_WINDOW_FOCUS_LOST)
                    {
                        if (m_ImGuiInputWindow == source.Window)
                            m_ImGuiInputWindow.reset();
                        return;
                    }
                    if (source.Window != 0)
                    {
                        if (!m_ImGuiInputWindow)
                            m_ImGuiInputWindow = source.Window;
                        if (m_ImGuiInputWindow != source.Window)
                            return;
                    }
                    SDL_Event event{};
                    event.type = source.Type;
                    switch (source.Type)
                    {
                    case SDL_EVENT_KEY_DOWN:
                    case SDL_EVENT_KEY_UP:
                        event.key.windowID = source.Window;
                        event.key.key = static_cast<SDL_Keycode>(source.Key);
                        event.key.scancode = static_cast<SDL_Scancode>(source.Scan);
                        event.key.mod = static_cast<SDL_Keymod>(source.Modifiers);
                        event.key.repeat = source.Repeat;
                        break;
                    case SDL_EVENT_MOUSE_BUTTON_DOWN:
                    case SDL_EVENT_MOUSE_BUTTON_UP:
                        event.button.windowID = source.Window;
                        event.button.button = static_cast<Uint8>(source.Button);
                        break;
                    case SDL_EVENT_MOUSE_MOTION:
                        event.motion.windowID = source.Window;
                        event.motion.x = source.X;
                        event.motion.y = source.Y;
                        event.motion.xrel = source.DeltaX;
                        event.motion.yrel = source.DeltaY;
                        break;
                    case SDL_EVENT_MOUSE_WHEEL:
                        event.wheel.windowID = source.Window;
                        event.wheel.x = source.X;
                        event.wheel.y = source.Y;
                        break;
                    case SDL_EVENT_TEXT_INPUT:
                        event.text.windowID = source.Window;
                        event.text.text = source.Text.c_str();
                        break;
                    case SDL_EVENT_WINDOW_MOUSE_ENTER:
                    case SDL_EVENT_WINDOW_MOUSE_LEAVE:
                    case SDL_EVENT_WINDOW_FOCUS_GAINED:
                    case SDL_EVENT_WINDOW_FOCUS_LOST:
                    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                    case SDL_EVENT_WINDOW_MOVED:
                    case SDL_EVENT_WINDOW_RESIZED:
                        event.window.windowID = source.Window;
                        event.window.data1 = static_cast<int>(source.X);
                        event.window.data2 = static_cast<int>(source.Y);
                        break;
                    default:
                        break;
                    }
                    auto& io = ImGui::GetIO();
                    switch (source.Type)
                    {
                    case SDL_EVENT_KEY_DOWN:
                    case SDL_EVENT_KEY_UP:
                        {
                            const auto key = ToImGuiKey(source.Key);
                            if (key != ImGuiKey_None)
                            {
                                io.AddKeyEvent(key, source.Type == SDL_EVENT_KEY_DOWN);
                            }
                            io.AddKeyEvent(
                                ImGuiMod_Ctrl, (source.Modifiers & SDL_KMOD_CTRL) != 0);
                            io.AddKeyEvent(
                                ImGuiMod_Shift, (source.Modifiers & SDL_KMOD_SHIFT) != 0);
                            io.AddKeyEvent(
                                ImGuiMod_Alt, (source.Modifiers & SDL_KMOD_ALT) != 0);
                            io.AddKeyEvent(
                                ImGuiMod_Super, (source.Modifiers & SDL_KMOD_GUI) != 0);
                            break;
                        }
                    case SDL_EVENT_MOUSE_BUTTON_DOWN:
                    case SDL_EVENT_MOUSE_BUTTON_UP:
                        {
                            const int button =
                                source.Button == MouseButton::Left ? 0 :
                                source.Button == MouseButton::Right ? 1 :
                                source.Button == MouseButton::Middle ? 2 :
                                source.Button == MouseButton::X1 ? 3 :
                                source.Button == MouseButton::X2 ? 4 : -1;
                            if (button >= 0)
                            {
                                io.AddMouseButtonEvent(
                                    button, source.Type == SDL_EVENT_MOUSE_BUTTON_DOWN);
                            }
                            break;
                        }
                    case SDL_EVENT_MOUSE_MOTION:
                        {
                            io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
                            io.AddMousePosEvent(
                                source.X, source.Y);
                            break;
                        }
                    case SDL_EVENT_MOUSE_WHEEL:
                        io.AddMouseWheelEvent(-source.X, source.Y);
                        break;
                    case SDL_EVENT_TEXT_INPUT:
                        io.AddInputCharactersUTF8(source.Text.c_str());
                        break;
                    default:
                        break;
                    }
                });
            }));
        m_RenderThread = std::thread([this, stopToken]()
        {
            RenderLoop(stopToken);
        });
        co_return;
    }

    Task<void> Renderer::StopAsync()
    {
        m_ImGuiInputSubscriptions.clear();
        m_ResizeSubscription.Reset();
        m_WindowCloseSubscription.Reset();
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

        if (!m_WindowManager->IsHeadless())
        {
            m_MainWindowResources.Window =
                m_WindowManager->CreateWindow(
                    *m_WindowOptions, std::string(WindowManager::MainWindowName)).get();
            if (!m_MainWindowResources.Window->CreateVulkanSurface(
                m_VulkanContext->GetInstance(), &m_MainWindowResources.Surface).get())
            {
                throw std::runtime_error("Failed to create Vulkan surface for main window.");
            }
        }
        const auto surface = m_MainWindowResources.Surface;
        m_Device = std::make_shared<VulkanDevice>(
            DeviceRequirements{
                .Graphics = true,
                .Compute = false,
                .Transfer = true,
                .Sparse = true,
                .Present = !m_WindowManager->IsHeadless(),
                .Surface = surface
            }, m_VulkanContext, m_Logger);
        if (!m_Device)
        {
            throw std::runtime_error("Failed to create Vulkan device.");
        }
        m_CommandPool = std::make_shared<VulkanCommandPool>(
            m_Device, m_Logger, m_Device->GetQueueIndices().Graphics);
        if (m_MainWindowResources.Window)
        {
            InitializeWindowResources(m_MainWindowResources.Window, m_MainWindowResources);
            for (const auto& definition : m_WindowDefinitions->Items)
            {
                auto window = m_WindowManager->CreateWindow(
                    definition.Options, definition.Name).get();
                auto& resources = m_WindowResources[window->GetID()];
                InitializeWindowResources(window, resources);
            }
        }
        if (m_WindowManager->IsHeadless())
        {
            return;
        }
        m_ImGuiContext = InitializeImGui(m_Device, &m_MainWindowResources, m_Logger);

        m_FrameResources.reserve(2);
        for (int i = 0; i < 2; i++)
        {
            m_FrameResources.emplace_back(std::move(FrameResources(
                m_CommandPool->AllocateCommandBuffer(),
                m_Device->GetDevice())));
        }
        InitializeWindowSync(m_MainWindowResources);
        for (auto& [windowId, resources] : m_WindowResources)
        {
            InitializeWindowSync(resources);
        }

        const auto vertexSource = m_FileSystem->ResolveAssetPath("shaders", "vert.vert");
        const auto fragmentSource = m_FileSystem->ResolveAssetPath("shaders", "frag.frag");
        m_ShaderPipeline = std::make_shared<ShaderPipeline>(
            m_Device,
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
                .compileOptions = ShaderCompileOptions{
                    .includeDirectories = [&]
                    {
                        std::vector<std::filesystem::path> directories;
                        directories.reserve(m_RenderOptions->ShaderAssetDirectories.size());
                        for (const auto& directory : m_RenderOptions->ShaderAssetDirectories)
                        {
                            directories.push_back(m_FileSystem->ResolvePath(directory));
                        }
                        return directories;
                    }()
                },
                .pollingInterval = m_RenderOptions->ShaderHotReloadInterval,
                .enableHotReload = m_RenderOptions->EnableShaderHotReload
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
        m_VertexBuffer.Create(m_Device,
                              MakeVertexBufferSpecification(sizeof(vertices), true));
        m_VertexBuffer.Upload(vertices.data(), sizeof(vertices));
        m_IndexBuffer.Create(m_Device,
                             MakeIndexBufferSpecification(sizeof(indices), true));
        m_IndexBuffer.Upload(indices.data(), sizeof(indices));
        m_IndexCount = static_cast<std::uint32_t>(indices.size());
    }

    void Renderer::InitializeWindowResources(const std::shared_ptr<Window>& window,
                                             WindowResources& resources)
    {
        resources.Window = window;
        if (!resources.Surface && !window->CreateVulkanSurface(
                m_VulkanContext->GetInstance(), &resources.Surface).get())
        {
            throw std::runtime_error("Failed to create Vulkan surface for window.");
        }
        resources.SwapChain = std::make_shared<VulkanSwapChain>(
            m_Device, m_Logger, glm::uvec2{10000, 10000}, resources.Surface);
        const auto depthFormat = resources.SwapChain->GetDepthImageFormat();
        resources.DepthImage.Create(
            m_Device,
            VulkanImageSpecification{
                .extent = {
                    resources.SwapChain->GetExtent().width,
                    resources.SwapChain->GetExtent().height,
                    1
                },
                .format = depthFormat,
                .usage = vk::ImageUsageFlagBits::eDepthStencilAttachment,
                .aspectMask = GetImageAspectMask(depthFormat),
                .debugName = "WindowDepthBuffer"
            });
        resources.DepthLayout = vk::ImageLayout::eUndefined;
    }

    void Renderer::InitializeWindowSync(WindowResources& resources)
    {
        if (!resources.SwapChain)
        {
            return;
        }
        resources.ImageAvailableSemaphores.clear();
        resources.ImageAvailableSemaphores.reserve(m_FrameResources.size());
        for (std::size_t i = 0; i < m_FrameResources.size(); ++i)
        {
            resources.ImageAvailableSemaphores.emplace_back(
                m_Device->GetDevice());
        }
        resources.RenderFinishedSemaphores.clear();
        resources.RenderFinishedSemaphores.reserve(resources.SwapChain->GetImageCount());
        for (std::uint32_t i = 0; i < resources.SwapChain->GetImageCount(); ++i)
        {
            resources.RenderFinishedSemaphores.emplace_back(m_Device->GetDevice());
        }
        resources.ImageLayouts.assign(
            resources.SwapChain->GetImageCount(), vk::ImageLayout::eUndefined);
    }

    void Renderer::RenderWindow(WindowResources& resources,
                                vk::CommandBuffer rawCmd,
                                const float elapsed,
                                const bool renderTargets)
    {
        auto& swapchain = resources.SwapChain;
        const auto imageIndex = swapchain->GetCurrentImageIndex();
        const auto extent = swapchain->GetExtent();
        if (renderTargets)
        for (auto& [bufferId, target] : m_BufferTargets)
        {
            TransitionImageLayout(
                rawCmd, target.ColorImage.GetImage(),
                target.ColorImage.GetSpecification().format, target.ColorLayout,
                vk::ImageLayout::eColorAttachmentOptimal);
            const auto targetDepthFormat = target.DepthImage.GetSpecification().format;
            const auto targetDepthLayout =
                (GetImageAspectMask(targetDepthFormat) & vk::ImageAspectFlagBits::eStencil) !=
                        vk::ImageAspectFlags{}
                    ? vk::ImageLayout::eDepthStencilAttachmentOptimal
                    : vk::ImageLayout::eDepthAttachmentOptimal;
            TransitionImageLayout(
                rawCmd, target.DepthImage.GetImage(), targetDepthFormat,
                target.DepthLayout, targetDepthLayout);
            target.ColorLayout = vk::ImageLayout::eColorAttachmentOptimal;
            target.DepthLayout = targetDepthLayout;
            vk::RenderingAttachmentInfo targetColor{};
            targetColor.imageView = target.ColorImage.GetImageView();
            targetColor.imageLayout = target.ColorLayout;
            targetColor.loadOp = vk::AttachmentLoadOp::eClear;
            targetColor.storeOp = vk::AttachmentStoreOp::eStore;
            targetColor.clearValue =
                vk::ClearValue(vk::ClearColorValue(0.08f, 0.10f, 0.14f, 1.0f));
            vk::RenderingAttachmentInfo targetDepth{};
            targetDepth.imageView = target.DepthImage.GetImageView();
            targetDepth.imageLayout = target.DepthLayout;
            targetDepth.loadOp = vk::AttachmentLoadOp::eClear;
            targetDepth.storeOp = vk::AttachmentStoreOp::eDontCare;
            targetDepth.clearValue =
                vk::ClearValue(vk::ClearDepthStencilValue(1.0f, 0));
            vk::RenderingInfo targetRendering{};
            targetRendering.renderArea = vk::Rect2D{
                {0, 0}, {target.Extent.x, target.Extent.y}};
            targetRendering.layerCount = 1;
            targetRendering.colorAttachmentCount = 1;
            targetRendering.pColorAttachments = &targetColor;
            targetRendering.pDepthAttachment = &targetDepth;
            rawCmd.beginRendering(targetRendering);
            const vk::Viewport targetViewport{
                0.0f, 0.0f, static_cast<float>(target.Extent.x),
                static_cast<float>(target.Extent.y), 0.0f, 1.0f};
            const vk::Rect2D targetScissor{{0, 0}, {target.Extent.x, target.Extent.y}};
            rawCmd.setViewport(0, 1, &targetViewport);
            rawCmd.setScissor(0, 1, &targetScissor);
            if (auto pipeline = m_ShaderPipeline ? m_ShaderPipeline->GetPipeline() : nullptr)
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
                pushConstants.viewProjection = glm::perspective(
                    glm::radians(45.0f),
                    static_cast<float>(target.Extent.x) /
                        static_cast<float>(std::max(target.Extent.y, 1u)),
                    0.1f, 100.0f);
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
            rawCmd.endRendering();
            TransitionImageLayout(
                rawCmd, target.ColorImage.GetImage(),
                target.ColorImage.GetSpecification().format, target.ColorLayout,
                vk::ImageLayout::eShaderReadOnlyOptimal);
            target.ColorLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        }

        const auto depthFormat = swapchain->GetDepthImageFormat();
        const auto depthLayout =
            (GetImageAspectMask(depthFormat) & vk::ImageAspectFlagBits::eStencil) !=
                    vk::ImageAspectFlags{}
                ? vk::ImageLayout::eDepthStencilAttachmentOptimal
                : vk::ImageLayout::eDepthAttachmentOptimal;

        TransitionImageLayout(
            rawCmd, swapchain->GetImages()[imageIndex], swapchain->GetImageFormat(),
            resources.ImageLayouts[imageIndex], vk::ImageLayout::eColorAttachmentOptimal);
        TransitionImageLayout(
            rawCmd, resources.DepthImage.GetImage(), depthFormat, resources.DepthLayout,
            depthLayout);
        resources.ImageLayouts[imageIndex] = vk::ImageLayout::eColorAttachmentOptimal;
        resources.DepthLayout = depthLayout;

        vk::RenderingAttachmentInfo colorAttachment{};
        colorAttachment.imageView = swapchain->GetImageViews()[imageIndex];
        colorAttachment.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
        colorAttachment.loadOp = vk::AttachmentLoadOp::eClear;
        colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
        colorAttachment.clearValue =
            vk::ClearValue(vk::ClearColorValue(0.05f, 0.05f, 0.05f, 1.0f));
        vk::RenderingAttachmentInfo depthAttachment{};
        depthAttachment.imageView = resources.DepthImage.GetImageView();
        depthAttachment.imageLayout = depthLayout;
        depthAttachment.loadOp = vk::AttachmentLoadOp::eClear;
        depthAttachment.storeOp = vk::AttachmentStoreOp::eDontCare;
        depthAttachment.clearValue =
            vk::ClearValue(vk::ClearDepthStencilValue(1.0f, 0));
        vk::RenderingInfo renderingInfo{};
        renderingInfo.renderArea = vk::Rect2D({0, 0}, swapchain->GetExtent());
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachments = &colorAttachment;
        renderingInfo.pDepthAttachment = &depthAttachment;

        rawCmd.beginRendering(renderingInfo);
        vk::Viewport viewport{
            0.0f, 0.0f, static_cast<float>(extent.width),
            static_cast<float>(extent.height), 0.0f, 1.0f};
        vk::Rect2D scissor{{0, 0}, extent};
        rawCmd.setViewport(0, 1, &viewport);
        rawCmd.setScissor(0, 1, &scissor);

        if (resources.LayerStack)
            resources.LayerStack->OnRender();

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
            const float aspect = static_cast<float>(extent.width) /
                static_cast<float>(std::max(extent.height, 1u));
            pushConstants.viewProjection =
                glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);
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
        if (m_ImGuiContext)
            ImGui_ImplVulkan_RenderDrawData(
                ImGui::GetDrawData(), static_cast<VkCommandBuffer>(rawCmd));
        rawCmd.endRendering();
        TransitionImageLayout(
            rawCmd, swapchain->GetImages()[imageIndex], swapchain->GetImageFormat(),
            vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::ePresentSrcKHR);
        resources.ImageLayouts[imageIndex] = vk::ImageLayout::ePresentSrcKHR;
    }

    Task<void> Renderer::StopRenderSystem()
    {
        // TODO?
        if (m_MainLayerStack)
            m_MainLayerStack->OnDetach();
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
            if (m_WindowManager->IsHeadless())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(16));
                continue;
            }
            {
                std::scoped_lock lock(m_RenderQueueMutex);
                std::vector<WindowResources*> windows;
                if (m_MainWindowResources.Window && m_MainWindowResources.SwapChain)
                    windows.push_back(&m_MainWindowResources);
                for (auto& [windowId, resources] : m_WindowResources)
                {
                    if (resources.Window && resources.SwapChain)
                        windows.push_back(&resources);
                }
                for (auto* resources : windows)
                {
                    const auto windowId = resources->Window->GetID();
                    if (auto it = m_PendingResize.find(windowId); it != m_PendingResize.end())
                    {
                        resources->SwapChain->Update(it->second);
                        resources->DepthImage.Resize(
                            {
                                resources->SwapChain->GetExtent().width,
                                resources->SwapChain->GetExtent().height, 1
                            });
                        resources->DepthLayout = vk::ImageLayout::eUndefined;
                        resources->ImageLayouts.assign(
                            resources->SwapChain->GetImageCount(),
                            vk::ImageLayout::eUndefined);
                        m_PendingResize.erase(it);
                    }
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(16)); // Simulate ~60 FPS
            //m_MainWindowResources.SwapChain->Update(glm::uvec2{100, 100});
            iterations++;

            std::vector<WindowResources*> activeWindows;
            if (m_MainWindowResources.Window && m_MainWindowResources.SwapChain)
            {
                activeWindows.push_back(&m_MainWindowResources);
            }
            for (auto& [windowId, resources] : m_WindowResources)
            {
                if (resources.Window && resources.SwapChain)
                {
                    activeWindows.push_back(&resources);
                }
            }
            if (activeWindows.empty())
            {
                continue;
            }

            FrameResources& multiWindowFrame = m_FrameResources[m_FrameIndex];
            multiWindowFrame.InFlightFence.WaitAndReset();
            for (auto* resources : activeWindows)
            {
                resources->SwapChain->AcquireNextImage(
                    resources->ImageAvailableSemaphores[m_FrameIndex].GetSemaphore(),
                    nullptr);
            }
            m_FrameIndex = (m_FrameIndex + 1) % m_FrameResources.size();

            auto& multiWindowCommandBuffer = multiWindowFrame.CommandBuffer;
            multiWindowCommandBuffer.Begin();
            const float elapsed = static_cast<float>(
                std::chrono::duration<double>(
                    std::chrono::high_resolution_clock::now() - start).count());
            const auto rawMultiWindowCommandBuffer =
                multiWindowCommandBuffer.GetCommandBuffer();
            if (m_ImGuiContext)
            {
                ImGui::SetCurrentContext(static_cast<ImGuiContext*>(m_ImGuiContext));
                auto& io = ImGui::GetIO();
                io.DisplaySize = ImVec2(
                    static_cast<float>(activeWindows.front()->SwapChain->GetExtent().width),
                    static_cast<float>(activeWindows.front()->SwapChain->GetExtent().height));
                io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
                auto* viewport = ImGui::GetMainViewport();
                viewport->Pos = ImVec2(0.0f, 0.0f);
                viewport->Size = io.DisplaySize;
                ImGui_ImplVulkan_NewFrame();
                ImGui::NewFrame();
                if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
                    ImGui::DockSpaceOverViewport(0, viewport);
            }
            for (auto& [bufferId, target] : m_BufferTargets)
            {
                if (target.LayerStack)
                {
                    target.LayerStack->OnRender();
                    target.LayerStack->OnUiRender();
                }
            }
            for (auto* resources : activeWindows)
            {
                if (resources->LayerStack)
                    resources->LayerStack->OnUiRender();
            }
            if (m_ImGuiContext)
                ImGui::Render();
            bool renderTargets = true;
            for (auto* resources : activeWindows)
            {
                RenderWindow(
                    *resources, rawMultiWindowCommandBuffer, elapsed, renderTargets);
                renderTargets = false;
            }
            multiWindowCommandBuffer.End();

            std::vector<vk::Semaphore> multiWaitSemaphores;
            std::vector<vk::PipelineStageFlags> multiWaitStages;
            std::vector<vk::Semaphore> multiSignalSemaphores;
            multiWaitSemaphores.reserve(activeWindows.size());
            multiWaitStages.reserve(activeWindows.size());
            multiSignalSemaphores.reserve(activeWindows.size());
            for (auto* resources : activeWindows)
            {
                multiWaitSemaphores.push_back(
                    resources->ImageAvailableSemaphores[m_FrameIndex == 0
                                                            ? m_FrameResources.size() - 1
                                                            : m_FrameIndex - 1]
                        .GetSemaphore());
                multiWaitStages.push_back(vk::PipelineStageFlagBits::eColorAttachmentOutput);
                multiSignalSemaphores.push_back(
                    resources->RenderFinishedSemaphores[
                        resources->SwapChain->GetCurrentImageIndex()].GetSemaphore());
            }
            vk::SubmitInfo multiWindowSubmitInfo{};
            multiWindowSubmitInfo.waitSemaphoreCount =
                static_cast<std::uint32_t>(multiWaitSemaphores.size());
            multiWindowSubmitInfo.pWaitSemaphores = multiWaitSemaphores.data();
            multiWindowSubmitInfo.pWaitDstStageMask = multiWaitStages.data();
            multiWindowSubmitInfo.signalSemaphoreCount =
                static_cast<std::uint32_t>(multiSignalSemaphores.size());
            multiWindowSubmitInfo.pSignalSemaphores = multiSignalSemaphores.data();
            const auto commandBuffer = multiWindowCommandBuffer.GetCommandBuffer();
            multiWindowSubmitInfo.commandBufferCount = 1;
            multiWindowSubmitInfo.pCommandBuffers = &commandBuffer;
            const auto submitResult = m_Device->GetGraphicsQueue().submit(
                1, &multiWindowSubmitInfo, multiWindowFrame.InFlightFence.GetFence());
            if (submitResult != vk::Result::eSuccess)
            {
                m_Logger->Error("Failed to submit multi-window frame: {}",
                                vk::to_string(submitResult));
                continue;
            }

            for (std::size_t index = 0; index < activeWindows.size(); ++index)
            {
                auto* resources = activeWindows[index];
                resources->SwapChain->Present(
                    m_Device->GetPresentQueue(), multiSignalSemaphores[index]);
            }
            if (m_ImGuiContext &&
                (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable))
            {
                ImGui::UpdatePlatformWindows();
                ImGui::RenderPlatformWindowsDefault();
            }
            continue;

            FrameResources& frame = m_FrameResources[m_FrameIndex];

            vk::Device device = m_Device->GetDevice();
            const auto& swapchain = m_MainWindowResources.SwapChain;

            frame.InFlightFence.WaitAndReset();

            swapchain->AcquireNextImage(
                frame.ImageAvailableSemaphore.GetSemaphore(),
                nullptr
            );

            auto imageIndex = swapchain->GetCurrentImageIndex();
            // maybe cancel early when aquire fails or something

            // once we know we are taking the frame
            m_FrameIndex = (m_FrameIndex + 1) % m_FrameResources.size();

            auto& cmd = frame.CommandBuffer;
            cmd.Begin();
            if (m_ImGuiContext)
            {
                ImGui::SetCurrentContext(static_cast<ImGuiContext*>(m_ImGuiContext));
                ImGui_ImplVulkan_NewFrame();
                ImGui_ImplSDL3_NewFrame();
                ImGui::NewFrame();
                if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable)
                    ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
            }

            {
                vk::CommandBuffer rawCmd = cmd.GetCommandBuffer();
                for (auto& [bufferId, target] : m_BufferTargets)
                {
                    TransitionImageLayout(
                        rawCmd,
                        target.ColorImage.GetImage(),
                        target.ColorImage.GetSpecification().format,
                        target.ColorLayout,
                        vk::ImageLayout::eColorAttachmentOptimal);
                    const auto targetDepthFormat = target.DepthImage.GetSpecification().format;
                    const auto targetDepthAspect = GetImageAspectMask(targetDepthFormat);
                    const auto targetDepthLayout =
                        (targetDepthAspect & vk::ImageAspectFlagBits::eStencil) !=
                        vk::ImageAspectFlags{}
                            ? vk::ImageLayout::eDepthStencilAttachmentOptimal
                            : vk::ImageLayout::eDepthAttachmentOptimal;
                    TransitionImageLayout(
                        rawCmd,
                        target.DepthImage.GetImage(),
                        targetDepthFormat,
                        target.DepthLayout,
                        targetDepthLayout);
                    target.ColorLayout = vk::ImageLayout::eColorAttachmentOptimal;
                    target.DepthLayout = targetDepthLayout;

                    vk::RenderingAttachmentInfo targetColor{};
                    targetColor.imageView = target.ColorImage.GetImageView();
                    targetColor.imageLayout = target.ColorLayout;
                    targetColor.loadOp = vk::AttachmentLoadOp::eClear;
                    targetColor.storeOp = vk::AttachmentStoreOp::eStore;
                    targetColor.clearValue = vk::ClearValue(
                        vk::ClearColorValue(0.08f, 0.10f, 0.14f, 1.0f));
                    vk::RenderingAttachmentInfo targetDepth{};
                    targetDepth.imageView = target.DepthImage.GetImageView();
                    targetDepth.imageLayout = target.DepthLayout;
                    targetDepth.loadOp = vk::AttachmentLoadOp::eClear;
                    targetDepth.storeOp = vk::AttachmentStoreOp::eDontCare;
                    targetDepth.clearValue = vk::ClearValue(
                        vk::ClearDepthStencilValue(1.0f, 0));
                    vk::RenderingInfo targetRendering{};
                    targetRendering.renderArea = vk::Rect2D{
                        vk::Offset2D{0, 0},
                        vk::Extent2D{target.Extent.x, target.Extent.y}
                    };
                    targetRendering.layerCount = 1;
                    targetRendering.colorAttachmentCount = 1;
                    targetRendering.pColorAttachments = &targetColor;
                    targetRendering.pDepthAttachment = &targetDepth;
                    rawCmd.beginRendering(targetRendering);
                    vk::Viewport targetViewport{
                        0.0f, 0.0f,
                        static_cast<float>(target.Extent.x),
                        static_cast<float>(target.Extent.y),
                        0.0f, 1.0f
                    };
                    vk::Rect2D targetScissor{{0, 0}, {target.Extent.x, target.Extent.y}};
                    rawCmd.setViewport(0, 1, &targetViewport);
                    rawCmd.setScissor(0, 1, &targetScissor);
                    if (target.LayerStack)
                    {
                        target.LayerStack->OnRender();
                        target.LayerStack->OnUiRender();
                    }
                    auto targetPipeline = m_ShaderPipeline ? m_ShaderPipeline->GetPipeline() : nullptr;
                    if (targetPipeline)
                    {
                        rawCmd.bindPipeline(
                            vk::PipelineBindPoint::eGraphics,
                            targetPipeline->GetPipeline());
                        BindVertexBuffer(rawCmd, m_VertexBuffer.GetBuffer());
                        BindIndexBuffer(
                            rawCmd, m_IndexBuffer.GetBuffer(), 0, vk::IndexType::eUint32);
                        struct PushConstants
                        {
                            glm::mat4 viewProjection;
                            glm::mat4 model;
                            float time;
                        } pushConstants{};
                        const float elapsed = static_cast<float>(
                            std::chrono::duration<double>(
                                std::chrono::high_resolution_clock::now() - start).count());
                        pushConstants.viewProjection = glm::perspective(
                            glm::radians(45.0f),
                            static_cast<float>(target.Extent.x) /
                            static_cast<float>(target.Extent.y),
                            0.1f, 100.0f);
                        pushConstants.viewProjection[1][1] *= -1.0f;
                        pushConstants.model = glm::translate(
                            glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -4.0f));
                        pushConstants.model = glm::rotate(
                            pushConstants.model, elapsed, glm::vec3(0.5f, 1.0f, 0.0f));
                        pushConstants.time = elapsed;
                        rawCmd.pushConstants(
                            targetPipeline->GetLayout(),
                            vk::ShaderStageFlagBits::eVertex,
                            0, sizeof(pushConstants), &pushConstants);
                        rawCmd.drawIndexed(m_IndexCount, 1, 0, 0, 0);
                    }
                    rawCmd.endRendering();
                    TransitionImageLayout(
                        rawCmd,
                        target.ColorImage.GetImage(),
                        target.ColorImage.GetSpecification().format,
                        target.ColorLayout,
                        vk::ImageLayout::eShaderReadOnlyOptimal);
                    target.ColorLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
                }
                const auto depthFormat = swapchain->GetDepthImageFormat();
                const bool depthHasStencil = (GetImageAspectMask(depthFormat) & vk::ImageAspectFlagBits::eStencil) !=
                    vk::ImageAspectFlags{};
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
                depthAttachment.imageLayout = depthTargetLayout;
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

                    // TODO
                    if (m_MainLayerStack)
                        m_MainLayerStack->OnRender();
                    if (m_MainLayerStack)
                        m_MainLayerStack->OnUiRender();

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
                    if (m_ImGuiContext)
                    {
                        ImGui::Render();
                        ImGui_ImplVulkan_RenderDrawData(
                            ImGui::GetDrawData(), static_cast<VkCommandBuffer>(rawCmd));
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

            auto result = m_Device->GetGraphicsQueue().submit(
                1, &submitInfo, frame.InFlightFence.GetFence());

            swapchain->Present(
                m_Device->GetPresentQueue(),
                renderFinishedSemaphore.GetSemaphore()
            );
            if (m_ImGuiContext &&
                (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable))
            {
                ImGui::UpdatePlatformWindows();
                ImGui::RenderPlatformWindowsDefault();
            }

            // m_Logger->Trace("W key pressed: {}", m_InputState->IsKeyDown(ScanCode::W));
        }
        if (m_Device)
            m_Device->WaitIdle();
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
        if (m_ImGuiContext)
        {
            ImGui::SetCurrentContext(static_cast<ImGuiContext*>(m_ImGuiContext));
            ImGui_ImplVulkan_Shutdown();
            ImGui_ImplSDL3_Shutdown();
            ImGui::DestroyContext(static_cast<ImGuiContext*>(m_ImGuiContext));
            m_ImGuiContext = nullptr;
        }
        for (auto& [bufferId, target] : m_BufferTargets)
        {
            if (target.LayerStack)
                target.LayerStack->OnDetach();
        }
        if (m_MainWindowResources.LayerStack)
            m_MainWindowResources.LayerStack->OnDetach();
        for (auto& [windowId, resources] : m_WindowResources)
        {
            if (resources.LayerStack)
                resources.LayerStack->OnDetach();
        }
        m_BufferTargets.clear();
        m_VertexBuffer.Destroy();
        m_IndexBuffer.Destroy();
        m_RenderFinishedSemaphores.clear();
        m_FrameResources.clear();
        m_CommandPool.reset();
        m_MainWindowResources.SwapChain.reset();
        for (auto& [windowId, resources] : m_WindowResources)
        {
            resources.SwapChain.reset();
        }
        if (m_MainWindowResources.Surface)
        {
            m_Device->GetInstance().destroySurfaceKHR(m_MainWindowResources.Surface);
            m_MainWindowResources.Surface = nullptr;
        }
        for (auto& [windowId, resources] : m_WindowResources)
        {
            if (resources.Surface)
            {
                m_Device->GetInstance().destroySurfaceKHR(resources.Surface);
                resources.Surface = nullptr;
            }
        }
        m_MainWindowResources.Window.reset();
        m_WindowResources.clear();
        m_Device.reset();
        std::chrono::high_resolution_clock::time_point end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        auto averageFps = iterations / (duration.count() / 1000.0);
        m_Logger->Info("Render thread completed. Iterations: {}, Time: {} ms, Average FPS: {}",
                       iterations, duration.count(), averageFps);
    }
}

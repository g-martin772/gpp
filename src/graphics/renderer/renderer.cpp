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
                       const std::shared_ptr<EventDispatcher>& dispatcher,
                       const std::shared_ptr<ImGuiOptions>& imguiOptions,
                       const std::shared_ptr<ThemeProxy>& themeProxy,
                       const std::shared_ptr<UiPreferences>& uiPreferences,
                       const std::shared_ptr<FontAssetCatalog>& fontAssets)
        : m_VulkanContext(std::move(vulkanContext)),
          m_WindowManager(std::move(windowManager)),
          m_WindowOptions(std::move(windowOptions)),
          m_WindowDefinitions(std::move(windowDefinitions)),
          m_RenderOptions(std::move(renderOptions)),
          m_Logger(std::move(logger)),
          m_FileSystem(std::move(fileSystem)),
          m_InputState(std::move(inputState)),
          m_Dispatcher(std::move(dispatcher)),
          m_ImGuiOptions(std::move(imguiOptions)),
          m_ThemeProxy(std::move(themeProxy)),
          m_UiPreferences(std::move(uiPreferences)),
          m_FontAssets(std::move(fontAssets))
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
            m_RenderQueue.push([this, window, options]
            {
                auto& resources = m_WindowResources[window->GetID()];
                resources.Window = window;
                InitializeWindowResources(window, resources);
                InitializeWindowSync(resources);
                if (m_ImGuiEnabled)
                {
                    InitializeImGuiForWindow(resources, options);
                }
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
            if (m_MainWindowResources.ImGuiContext)
            {
                ImGui::SetCurrentContext(
                    static_cast<ImGuiContext*>(m_MainWindowResources.ImGuiContext));
                target.ImGuiTexture = reinterpret_cast<void*>(ImGui_ImplVulkan_AddTexture(
                    target.ColorImage.GetSampler(),
                    target.ColorImage.GetImageView(),
                    static_cast<VkImageLayout>(vk::ImageLayout::eShaderReadOnlyOptimal)));
            }
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

    void Renderer::ResizeBufferTarget(const std::uint32_t bufferId, const glm::uvec2 extent)
    {
        const auto it = m_BufferTargets.find(bufferId);
        if (it == m_BufferTargets.end()) return;
        auto& target = it->second;
        if (extent.x == 0 || extent.y == 0 || target.Extent == extent) return;

        m_Device->GetDevice().waitIdle();

        target.Extent = extent;
        target.ColorImage.Resize({extent.x, extent.y, 1});
        target.DepthImage.Resize({extent.x, extent.y, 1});

        target.ColorLayout = vk::ImageLayout::eUndefined;
        target.DepthLayout = vk::ImageLayout::eUndefined;

        if (m_MainWindowResources.ImGuiContext)
        {
            ImGui::SetCurrentContext(static_cast<ImGuiContext*>(m_MainWindowResources.ImGuiContext));
            if (target.ImGuiTexture)
            {
                ImGui_ImplVulkan_RemoveTexture(static_cast<VkDescriptorSet>(target.ImGuiTexture));
            }
            target.ImGuiTexture = reinterpret_cast<void*>(ImGui_ImplVulkan_AddTexture(
                target.ColorImage.GetSampler(),
                target.ColorImage.GetImageView(),
                static_cast<VkImageLayout>(vk::ImageLayout::eShaderReadOnlyOptimal)));
        }
    }

    Task<void> Renderer::StartAsync(std::stop_token stopToken)
    {
        m_Dispatcher->SetRenderExecutor([this](std::move_only_function<void()> task)
        {
            std::scoped_lock lock(m_RenderQueueMutex);
            m_RenderQueue.push(std::move(task));
        });
        m_ThemeChangedSubscription = m_Dispatcher->Subscribe<ThemeChangedEvent>(
            [this](const ThemeChangedEvent&)
            {
                std::scoped_lock lock(m_RenderQueueMutex);
                m_RenderQueue.push([this] { ApplyThemeToAllContexts(); });
            }, EventDelivery::Async, EventTarget::Render);
        m_UiPreferencesSubscription = m_Dispatcher->Subscribe<UiPreferencesChangedEvent>(
            [this](const UiPreferencesChangedEvent&)
            {
                std::scoped_lock lock(m_RenderQueueMutex);
                m_RenderQueue.push([this] { ApplyFontPreferencesToAllContexts(); });
            }, EventDelivery::Async, EventTarget::Render);
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
                        ShutdownImGuiForWindow(m_MainWindowResources);
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

                        if (m_MainWindowResources.Window)
                        {
                            m_MainWindowResources.Window->DestroyWindow().get();
                        }
                        m_MainWindowResources.Window.reset();
                        return;
                    }
                    const auto it = m_WindowResources.find(id);
                    if (it != m_WindowResources.end())
                    {
                        ShutdownImGuiForWindow(it->second);
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

                        if (it->second.Window)
                        {
                            it->second.Window->DestroyWindow().get();
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
                    WindowResources* resources = nullptr;
                    if (m_MainWindowResources.Window &&
                        m_MainWindowResources.Window->GetID() == source.Window)
                    {
                        resources = &m_MainWindowResources;
                    }
                    else if (const auto it = m_WindowResources.find(source.Window);
                        it != m_WindowResources.end())
                    {
                        resources = &it->second;
                    }
                    if (!resources || !resources->ImGuiContext)
                        return;
                    // Each window owns its own ImGuiContext, so input is naturally
                    // scoped to whichever window the originating SDL event targeted.
                    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(resources->ImGuiContext));
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
        m_ThemeChangedSubscription.Reset();
        m_UiPreferencesSubscription.Reset();
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
        m_ImGuiEnabled = true;
        InitializeImGuiForWindow(m_MainWindowResources, *m_WindowOptions);
        for (auto& [windowId, resources] : m_WindowResources)
        {
            const WindowOptions* options = m_WindowOptions.get();
            for (const auto& definition : m_WindowDefinitions->Items)
            {
                if (resources.Window && m_WindowManager->GetWindowName(windowId) == definition.Name)
                {
                    options = &definition.Options;
                    break;
                }
            }
            InitializeImGuiForWindow(resources, *options);
        }

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

    void Renderer::InitializeImGuiForWindow(WindowResources& resources, const WindowOptions& windowOptions)
    {
        if (!resources.Window || !resources.SwapChain || resources.ImGuiContext)
            return;
        const auto name = m_WindowManager->GetWindowName(resources.Window->GetID());
        resources.ImGuiIniPath = std::format(
            ".gpp/imgui_{}.ini", name.empty() ? std::to_string(resources.Window->GetID()) : name);
        resources.ImGuiContext = InitializeImGui(m_Device, &resources, m_Logger);

        ImGui::SetCurrentContext(static_cast<ImGuiContext*>(resources.ImGuiContext));
        auto& io = ImGui::GetIO();
        const auto& configFlags = windowOptions.ImGuiConfigFlags.value_or(m_ImGuiOptions->ConfigFlags);
        io.ConfigFlags |= ParseImGuiConfigFlags(configFlags);
        resources.EnableDockSpace = windowOptions.ImGuiDockSpace.value_or(m_ImGuiOptions->EnableDockSpace);
        resources.ImGuiEffectiveConfigFlags = static_cast<unsigned int>(io.ConfigFlags);

        ApplyThemeToContext(resources);
        ApplyFontPreferencesToContext(resources);
    }

    void Renderer::ShutdownImGuiForWindow(WindowResources& resources)
    {
        if (!resources.ImGuiContext)
            return;
        ImGui::SetCurrentContext(static_cast<ImGuiContext*>(resources.ImGuiContext));
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext(static_cast<ImGuiContext*>(resources.ImGuiContext));
        resources.ImGuiContext = nullptr;
        resources.FontCache.clear();
    }

    void Renderer::ApplyThemeToContext(WindowResources& resources) const
    {
        if (!resources.ImGuiContext)
            return;
        auto* theme = m_ThemeProxy->GetActive();
        if (!theme)
            return;
        ImGui::SetCurrentContext(static_cast<ImGuiContext*>(resources.ImGuiContext));
        theme->Apply(ImGui::GetStyle(), ImGui::GetIO());
    }

    void Renderer::ApplyFontPreferencesToContext(WindowResources& resources) const
    {
        if (!resources.ImGuiContext)
            return;
        ImGui::SetCurrentContext(static_cast<ImGuiContext*>(resources.ImGuiContext));
        auto& io = ImGui::GetIO();

        io.FontGlobalScale = std::max(m_UiPreferences->GetUiScale(), 0.1f);

        const auto fontName = m_UiPreferences->GetFontName();
        const auto fontSize = m_UiPreferences->GetFontSize();
        if (fontName.empty() || fontSize <= 0.0f)
        {
            return;
        }

        const auto cacheKey = std::format("{}@{}", fontName, fontSize);
        if (const auto it = resources.FontCache.find(cacheKey); it != resources.FontCache.end())
        {
            io.FontDefault = static_cast<ImFont*>(it->second);
            return;
        }

        const auto path = m_FontAssets->ResolveFont(fontName);
        if (!path)
        {
            m_Logger->Warn("UiPreferences requested unknown font '{}'; keeping the current font.", fontName);
            return;
        }

        auto* font = io.Fonts->AddFontFromFileTTF(path->string().c_str(), fontSize);
        if (!font)
        {
            m_Logger->Error("Failed to load font '{}' from {}", fontName, path->string());
            return;
        }
        resources.FontCache[cacheKey] = font;
        io.FontDefault = font;
    }

    void Renderer::ApplyThemeToAllContexts()
    {
        ApplyThemeToContext(m_MainWindowResources);
        for (auto& [windowId, resources] : m_WindowResources)
        {
            ApplyThemeToContext(resources);
        }
    }

    void Renderer::ApplyFontPreferencesToAllContexts()
    {
        ApplyFontPreferencesToContext(m_MainWindowResources);
        for (auto& [windowId, resources] : m_WindowResources)
        {
            ApplyFontPreferencesToContext(resources);
        }
    }

    void Renderer::RenderWindow(WindowResources& resources,
                                vk::CommandBuffer rawCmd,
                                const float elapsed,
                                const bool renderTargets,
                                const std::uint32_t frameIndex)
    {
        (void)elapsed;
        auto& swapchain = resources.SwapChain;
        const auto imageIndex = swapchain->GetCurrentImageIndex();
        const auto extent = swapchain->GetExtent();

        if (renderTargets)
            for (auto& [bufferId, target] : m_BufferTargets)
            {
                if (!target.Graph)
                {
                    target.Graph = std::make_unique<RenderGraph>(m_Device, m_Logger);
                }
                target.Graph->Begin(frameIndex);
                const auto colorHandle = target.Graph->ImportImage(
                    "Target.Color", target.ColorImage, target.ColorLayout);
                const auto depthHandle = target.Graph->ImportImage(
                    "Target.Depth", target.DepthImage, target.DepthLayout);
                target.Graph->SetPrimaryColorTarget(colorHandle);
                target.Graph->SetPrimaryDepthTarget(depthHandle);
                if (target.LayerStack)
                {
                    target.LayerStack->OnRenderGraph(*target.Graph);
                }
                target.Graph->Compile();
                target.Graph->Execute(rawCmd);
                target.ColorLayout = target.Graph->GetCurrentLayout(colorHandle);
                target.DepthLayout = target.Graph->GetCurrentLayout(depthHandle);

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

        if (!resources.Graph)
        {
            resources.Graph = std::make_unique<RenderGraph>(m_Device, m_Logger);
        }
        resources.Graph->Begin(frameIndex);
        const auto windowColorHandle = resources.Graph->ImportImage(
            "Window.Color",
            RenderGraphExternalImage{
                swapchain->GetImages()[imageIndex], swapchain->GetImageViews()[imageIndex],
                swapchain->GetImageFormat(), vk::Extent3D{extent.width, extent.height, 1}
            },
            resources.ImageLayouts[imageIndex]);
        const auto windowDepthHandle = resources.Graph->ImportImage(
            "Window.Depth", resources.DepthImage, resources.DepthLayout);
        resources.Graph->SetPrimaryColorTarget(windowColorHandle);
        resources.Graph->SetPrimaryDepthTarget(windowDepthHandle);
        if (resources.LayerStack)
        {
            resources.LayerStack->OnRenderGraph(*resources.Graph);
        }
        resources.Graph->Compile();
        resources.Graph->Execute(rawCmd);
        resources.ImageLayouts[imageIndex] = resources.Graph->GetCurrentLayout(windowColorHandle);
        resources.DepthLayout = resources.Graph->GetCurrentLayout(windowDepthHandle);

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
        colorAttachment.loadOp = vk::AttachmentLoadOp::eLoad;
        colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
        vk::RenderingAttachmentInfo depthAttachment{};
        depthAttachment.imageView = resources.DepthImage.GetImageView();
        depthAttachment.imageLayout = depthLayout;
        depthAttachment.loadOp = vk::AttachmentLoadOp::eLoad;
        depthAttachment.storeOp = vk::AttachmentStoreOp::eDontCare;
        vk::RenderingInfo renderingInfo{};
        renderingInfo.renderArea = vk::Rect2D({0, 0}, swapchain->GetExtent());
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachments = &colorAttachment;
        renderingInfo.pDepthAttachment = &depthAttachment;

        rawCmd.beginRendering(renderingInfo);
        vk::Viewport viewport{
            0.0f, 0.0f, static_cast<float>(extent.width),
            static_cast<float>(extent.height), 0.0f, 1.0f
        };
        vk::Rect2D scissor{{0, 0}, extent};
        rawCmd.setViewport(0, 1, &viewport);
        rawCmd.setScissor(0, 1, &scissor);
        if (resources.ImGuiContext)
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

            const std::uint32_t frameSlot = m_FrameIndex;
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
            const float deltaTime = std::max(0.0f, elapsed - m_LastFrameElapsed);
            m_LastFrameElapsed = elapsed;
            const auto rawMultiWindowCommandBuffer =
                multiWindowCommandBuffer.GetCommandBuffer();
            // Buffer-target UI (e.g. ImGui::Image widgets showing offscreen render targets)
            // is hosted inside the main window's ImGui frame.
            if (m_MainWindowResources.ImGuiContext)
            {
                ImGui::SetCurrentContext(
                    static_cast<ImGuiContext*>(m_MainWindowResources.ImGuiContext));
                ImGui_ImplVulkan_NewFrame();
                ImGui_ImplSDL3_NewFrame();
                ImGui::NewFrame();
                if (m_MainWindowResources.EnableDockSpace &&
                    (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable))
                    m_MainDockspaceId = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
                for (auto& [bufferId, target] : m_BufferTargets)
                {
                    if (target.LayerStack)
                    {
                        target.LayerStack->OnUpdate(deltaTime);
                        target.LayerStack->OnRender();
                        target.LayerStack->OnUiRender();
                    }
                }
                if (m_MainWindowResources.LayerStack)
                {
                    m_MainWindowResources.LayerStack->OnUpdate(deltaTime);
                    m_MainWindowResources.LayerStack->OnUiRender();
                }
                ImGui::Render();
            }
            // Every other window owns its own independent ImGuiContext, so each one gets
            // its own NewFrame/UiRender/Render cycle and draw data.
            for (auto* resources : activeWindows)
            {
                if (resources == &m_MainWindowResources || !resources->ImGuiContext)
                    continue;
                ImGui::SetCurrentContext(static_cast<ImGuiContext*>(resources->ImGuiContext));
                ImGui_ImplVulkan_NewFrame();
                ImGui_ImplSDL3_NewFrame();
                ImGui::NewFrame();
                if (resources->EnableDockSpace &&
                    (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable))
                    ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
                if (resources->LayerStack)
                {
                    resources->LayerStack->OnUpdate(deltaTime);
                    resources->LayerStack->OnUiRender();
                }
                ImGui::Render();
            }
            bool renderTargets = true;
            for (auto* resources : activeWindows)
            {
                if (resources->ImGuiContext)
                    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(resources->ImGuiContext));
                RenderWindow(
                    *resources, rawMultiWindowCommandBuffer, elapsed, renderTargets, frameSlot);
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
            continue;
        }
        if (m_Device)
            m_Device->WaitIdle();
        {
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
        }
        ShutdownImGuiForWindow(m_MainWindowResources);
        for (auto& [windowId, resources] : m_WindowResources)
        {
            ShutdownImGuiForWindow(resources);
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

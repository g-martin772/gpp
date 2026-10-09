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

    Renderer::~Renderer()
    {
        m_Running = false;
        if (m_RenderThread.joinable())
        {
            m_RenderThread.join();
        }
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

    std::shared_ptr<BufferTargetResources> Renderer::FindBufferTarget(const std::uint32_t bufferId) const
    {
        std::shared_lock lock(m_TargetsMutex);
        const auto it = m_BufferTargets.find(bufferId);
        return it == m_BufferTargets.end() ? nullptr : it->second;
    }

    std::vector<std::shared_ptr<BufferTargetResources>> Renderer::SnapshotBufferTargets() const
    {
        std::shared_lock lock(m_TargetsMutex);
        std::vector<std::shared_ptr<BufferTargetResources>> targets;
        targets.reserve(m_BufferTargets.size());
        for (const auto& [id, target] : m_BufferTargets)
        {
            targets.push_back(target);
        }
        std::ranges::sort(targets, {}, &BufferTargetResources::Id);
        return targets;
    }

    void Renderer::PostToBufferThread(std::move_only_function<void()> task)
    {
        if (m_ViewportRunning.load(std::memory_order_acquire))
        {
            {
                std::scoped_lock lock(m_ViewportMutex);
                m_ViewportQueue.push(std::move(task));
            }
            WakeViewport();
            return;
        }

        std::scoped_lock lock(m_RenderQueueMutex);
        m_RenderQueue.push(std::move(task));
    }

    void Renderer::WakeViewport()
    {
        {
            std::scoped_lock lock(m_ViewportMutex);
            ++m_ViewportSignal;
        }
        m_ViewportWake.notify_all();
    }

    void Renderer::AttachLayerStackToBuffer(GuiLayerStack& layerStack, const std::uint32_t bufferId)
    {
        PostToBufferThread([this, &layerStack, bufferId]
        {
            std::shared_ptr<BufferTargetResources> target;
            {
                std::unique_lock lock(m_TargetsMutex);
                auto& slot = m_BufferTargets[bufferId];
                if (!slot)
                    slot = std::make_shared<BufferTargetResources>(bufferId);
                target = slot;
            }
            if (target->LayerStack)
                return;

            target->LayerStack = &layerStack;
            const auto extent = BufferTargetResources::UnpackExtent(target->DesiredExtent.load());
            target->Extent = {std::max(extent.x, 1u), std::max(extent.y, 1u)};
            target->DepthImage.Create(
                m_Device,
                VulkanImageSpecification{
                    .extent = {target->Extent.x, target->Extent.y, 1},
                    .format = m_TargetDepthFormat,
                    .usage = vk::ImageUsageFlagBits::eDepthStencilAttachment,
                    .aspectMask = GetImageAspectMask(m_TargetDepthFormat),
                    .debugName = "BufferTargetDepth"
                });

            layerStack.OnAttach();
            target->Attached.store(true, std::memory_order_release);
            m_Logger->Info("Created render target {}", bufferId);
        });
    }

    std::optional<Renderer::RenderTargetInfo> Renderer::GetRenderTargetInfo(const std::uint32_t bufferId) const
    {
        const auto target = FindBufferTarget(bufferId);
        if (!target || !target->Front)
            return std::nullopt;
        const auto& front = *target->Front;
        return RenderTargetInfo{
            vk::Extent2D{front.Extent.x, front.Extent.y},
            front.Color.GetImageView(),
            front.Color.GetSampler(),
            front.ImGuiTexture,
            front.Serial
        };
    }

    void Renderer::ResizeBufferTarget(const std::uint32_t bufferId, const glm::uvec2 extent)
    {
        if (extent.x == 0 || extent.y == 0)
            return;

        auto target = FindBufferTarget(bufferId);
        if (!target)
        {
            std::unique_lock lock(m_TargetsMutex);
            auto& slot = m_BufferTargets[bufferId];
            if (!slot)
                slot = std::make_shared<BufferTargetResources>(bufferId);
            target = slot;
        }
        const auto packed = BufferTargetResources::PackExtent(extent);
        if (target->DesiredExtent.exchange(packed) != packed)
        {
            WakeViewport();
        }
    }

    void Renderer::SetBufferTargetVisible(const std::uint32_t bufferId, const bool visible)
    {
        if (const auto target = FindBufferTarget(bufferId))
        {
            if (target->Visible.exchange(visible) != visible && visible)
            {
                WakeViewport();
            }
        }
    }

    std::optional<Renderer::BufferTargetStats> Renderer::GetBufferTargetStats(const std::uint32_t bufferId) const
    {
        const auto target = FindBufferTarget(bufferId);
        if (!target || !target->Attached.load(std::memory_order_acquire))
            return std::nullopt;
        BufferTargetStats stats;
        stats.FramesPerSecond = target->FrameMeter.PerSecond();
        stats.FrameMs = target->FrameMeter.LastMs();
        stats.AverageFrameMs = target->FrameMeter.AverageMs();
        stats.GpuMs = target->LastGpuMs.load(std::memory_order_relaxed);
        stats.Submissions = target->LastSubmissions.load(std::memory_order_relaxed);
        stats.FramesRendered = target->FrameMeter.Total();
        stats.Extent = BufferTargetResources::UnpackExtent(target->DesiredExtent.load(std::memory_order_relaxed));
        stats.Visible = target->Visible.load(std::memory_order_relaxed);
        if (const auto since = target->InFlightSinceNs.load(std::memory_order_acquire); since != 0)
        {
            const auto nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            stats.CurrentFrameMs = std::max(0.0, static_cast<double>(nowNs - since) * 1e-6);
        }
        return stats;
    }

    Renderer::UiStats Renderer::GetUiStats() const
    {
        return UiStats{
            m_UiFrameMeter.PerSecond(), m_UiFrameMeter.LastMs(), m_UiFrameMeter.AverageMs(), m_UiFrameMeter.MaxMs()
        };
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
        m_TargetDepthFormat = m_Device->GetDepthFormat();
        if (m_MainWindowResources.Window)
        {
            InitializeWindowResources(m_MainWindowResources.Window, m_MainWindowResources);
            // The offscreen targets match the swapchain so the UI can sample them without conversion.
            m_TargetColorFormat = m_MainWindowResources.SwapChain->GetImageFormat();
            m_TargetDepthFormat = m_MainWindowResources.SwapChain->GetDepthImageFormat();
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
        m_FrameKeepAlive.assign(m_FrameResources.size(), {});
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
            m_Device, m_Logger, glm::uvec2{10000, 10000}, resources.Surface, 3, m_RenderOptions->VSync);
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
                                const std::uint32_t frameIndex)
    {
        auto& swapchain = resources.SwapChain;
        const auto imageIndex = swapchain->GetCurrentImageIndex();
        const auto extent = swapchain->GetExtent();

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

    class Renderer::ViewportChunkSink final : public IRenderGraphChunkSink
    {
    public:
        ViewportChunkSink(VulkanDevice& device, VulkanCommandPool& pool, std::stop_token stopToken)
            : m_Device(device), m_Pool(pool), m_Queue(device.GetBackgroundQueue()), m_Stop(std::move(stopToken)),
              m_Fence(device.GetDevice(), false)
        {
        }

        vk::CommandBuffer BeginChunk() override
        {
            m_Cmd.emplace(m_Pool.AllocateCommandBuffer());
            m_Cmd->Begin();
            return m_Cmd->GetCommandBuffer();
        }

        double SubmitChunkAndWait() override
        {
            if (!m_Cmd)
                return 0.0;
            m_Cmd->End();
            const auto raw = m_Cmd->GetCommandBuffer();
            vk::SubmitInfo info{};
            info.commandBufferCount = 1;
            info.pCommandBuffers = &raw;

            const auto device = m_Device.GetDevice();
            const auto fence = m_Fence.GetFence();
            const auto start = std::chrono::steady_clock::now();
            {
                std::scoped_lock lock(m_Device.GetQueueMutex(m_Queue));
                const auto result = m_Queue.submit(1, &info, fence);
                if (result != vk::Result::eSuccess)
                {
                    m_Cmd.reset();
                    throw std::runtime_error("vkQueueSubmit failed: " + vk::to_string(result));
                }
            }

            while (device.waitForFences(1, &fence, vk::True, 50'000'000ull) == vk::Result::eTimeout)
            {
                // dupdidupdidu
            }

            if (device.resetFences(1, &fence) != vk::Result::eSuccess)
                throw std::runtime_error("vkResetFences failed");
            m_Cmd.reset();

            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            m_GpuMs += ms;
            ++m_Submissions;
            return ms;
        }

        bool Cancelled() const override { return m_Stop.stop_requested(); }
        void Abandon() { m_Cmd.reset(); }

        [[nodiscard]] double GpuMs() const noexcept { return m_GpuMs; }
        [[nodiscard]] std::uint32_t Submissions() const noexcept { return m_Submissions; }

    private:
        VulkanDevice& m_Device;
        VulkanCommandPool& m_Pool;
        vk::Queue m_Queue;
        std::stop_token m_Stop;
        VulkanFence m_Fence;
        std::optional<VulkanCommandBuffer> m_Cmd;
        double m_GpuMs = 0.0;
        std::uint32_t m_Submissions = 0;
    };

    void Renderer::ApplyDesiredExtent(BufferTargetResources& target)
    {
        auto desired = BufferTargetResources::UnpackExtent(target.DesiredExtent.load(std::memory_order_relaxed));
        desired = {std::max(desired.x, 1u), std::max(desired.y, 1u)};
        if (desired == target.Extent)
            return;
        target.Extent = desired;
        target.DepthImage.Resize({desired.x, desired.y, 1});
        target.DepthLayout = vk::ImageLayout::eUndefined;
    }

    std::shared_ptr<TargetImage> Renderer::AcquireTargetImage(BufferTargetResources& target)
    {
        {
            std::scoped_lock lock(target.Mutex);
            std::erase_if(target.Pool, [&](const std::shared_ptr<TargetImage>& image)
            {
                return image.use_count() == 1 && image->Extent != target.Extent;
            });

            for (const auto& image : target.Pool)
            {
                if (image.use_count() == 1 && image->Extent == target.Extent)
                {
                    return image;
                }
            }
            if (target.Pool.size() >= BufferTargetResources::kMaxImages)
            {
                return nullptr;
            }
        }

        auto image = std::make_shared<TargetImage>();
        image->Graveyard = m_TextureGraveyard;
        image->Extent = target.Extent;
        image->Color.Create(
            m_Device,
            VulkanImageSpecification{
                .extent = {target.Extent.x, target.Extent.y, 1},
                .format = m_TargetColorFormat,
                .usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled |
                         vk::ImageUsageFlagBits::eTransferSrc,
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .createSampler = true,
                .debugName = "BufferTargetColor"
            });
        std::scoped_lock lock(target.Mutex);
        target.Pool.push_back(image);
        return image;
    }

    bool Renderer::RenderTargetFrame(BufferTargetResources& target, VulkanCommandPool& pool,
                                     const bool paceToConsumer, std::stop_token stopToken)
    {
        if (!target.LayerStack || !target.Attached.load(std::memory_order_acquire))
            return false;
        if (paceToConsumer)
        {
            std::scoped_lock lock(target.Mutex);
            if (target.Ready)
                return false;
        }

        std::shared_ptr<TargetImage> image;
        try
        {
            ApplyDesiredExtent(target);
            image = AcquireTargetImage(target);
        }
        catch (const std::exception& error)
        {
            m_Logger->Error("Render target {}: could not allocate a frame: {}", target.Id, error.what());
            return false;
        }
        if (!image)
            return false;

        const auto frameStart = std::chrono::steady_clock::now();
        target.InFlightSinceNs.store(
            std::chrono::duration_cast<std::chrono::nanoseconds>(frameStart.time_since_epoch()).count(),
            std::memory_order_release);
        struct InFlightGuard
        {
            BufferTargetResources& Target;
            ~InFlightGuard() { Target.InFlightSinceNs.store(0, std::memory_order_release); }
        } inFlightGuard{target};

        ViewportChunkSink sink(*m_Device, pool, std::move(stopToken));
        try
        {
            target.LayerStack->OnRender();

            if (!target.Graph)
            {
                target.Graph = std::make_unique<RenderGraph>(m_Device, m_Logger, 1);
            }
            auto& graph = *target.Graph;
            graph.Begin(0);
            const auto colorHandle = graph.ImportImage("Target.Color", image->Color, image->Layout);
            const auto depthHandle = graph.ImportImage("Target.Depth", target.DepthImage, target.DepthLayout);
            graph.SetPrimaryColorTarget(colorHandle);
            graph.SetPrimaryDepthTarget(depthHandle);
            target.LayerStack->OnRenderGraph(graph);
            graph.Compile();

            if (graph.PassCount() == 0)
            {
                return false;
            }

            const auto sliceMs = m_RenderOptions->ViewportSliceMs;
            const auto cmd = graph.ExecuteSliced(sink, [&](const std::string& passName) -> AdaptiveSlicer&
            {
                auto [it, inserted] = target.Slicers.try_emplace(passName);
                if (inserted)
                {
                    AdaptiveSlicer::Config config;
                    config.TargetMs = sliceMs;
                    it->second = AdaptiveSlicer(config);
                }
                it->second.SetTargetMs(sliceMs);
                return it->second;
            });

            if (sink.Cancelled())
            {
                sink.Abandon();
                image->Layout = vk::ImageLayout::eUndefined;
                target.DepthLayout = vk::ImageLayout::eUndefined;
                return false;
            }

            TransitionImageLayout(cmd, image->Color.GetImage(), image->Color.GetSpecification().format,
                                  graph.GetCurrentLayout(colorHandle), vk::ImageLayout::eShaderReadOnlyOptimal);
            sink.SubmitChunkAndWait();
            image->Layout = vk::ImageLayout::eShaderReadOnlyOptimal;
            target.DepthLayout = graph.GetCurrentLayout(depthHandle);
        }

        catch (const std::exception& error)
        {
            sink.Abandon();
            image->Layout = vk::ImageLayout::eUndefined;
            target.DepthLayout = vk::ImageLayout::eUndefined;
            m_Logger->Error("Render target {}: frame failed: {}", target.Id, error.what());
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            return false;
        }

        image->Serial = target.NextSerial++;
        {
            std::scoped_lock lock(target.Mutex);
            target.Ready = std::move(image);
        }

        const auto frameEnd = std::chrono::steady_clock::now();
        target.LastGpuMs.store(sink.GpuMs(), std::memory_order_relaxed);
        target.LastSubmissions.store(sink.Submissions(), std::memory_order_relaxed);
        target.FrameMeter.Tick(std::chrono::duration<double, std::milli>(frameEnd - frameStart).count(), frameEnd);
        return true;
    }

    void Renderer::TakeFrontImages(const std::uint32_t frameSlot)
    {
        bool tookFrame = false;
        for (const auto& target : SnapshotBufferTargets())
        {
            if (!target->Attached.load(std::memory_order_acquire))
                continue;

            std::shared_ptr<TargetImage> ready;
            {
                std::scoped_lock lock(target->Mutex);
                ready = std::move(target->Ready);
            }

            if (ready)
            {
                if (!ready->ImGuiTexture && m_MainWindowResources.ImGuiContext)
                {
                    ready->ImGuiTexture = reinterpret_cast<void*>(ImGui_ImplVulkan_AddTexture(
                        ready->Color.GetSampler(), ready->Color.GetImageView(),
                        static_cast<VkImageLayout>(vk::ImageLayout::eShaderReadOnlyOptimal)));
                }

                target->Front = std::move(ready);
                tookFrame = true;
            }
            if (target->Front)
            {
                m_FrameKeepAlive[frameSlot].push_back(target->Front);
            }
        }
        if (tookFrame)
        {
            WakeViewport();
        }
    }

    void Renderer::DrainTextureGraveyard()
    {
        const auto items = m_TextureGraveyard->Take();
        if (items.empty() || !m_MainWindowResources.ImGuiContext)
            return;
        ImGui::SetCurrentContext(static_cast<ImGuiContext*>(m_MainWindowResources.ImGuiContext));
        for (void* texture : items)
        {
            ImGui_ImplVulkan_RemoveTexture(static_cast<VkDescriptorSet>(texture));
        }
    }

    void Renderer::ShutdownBufferTargets()
    {
        for (auto& slot : m_FrameKeepAlive)
        {
            slot.clear();
        }
        std::unordered_map<std::uint32_t, std::shared_ptr<BufferTargetResources>> targets;
        {
            std::unique_lock lock(m_TargetsMutex);
            targets.swap(m_BufferTargets);
        }
        for (auto& [id, target] : targets)
        {
            std::scoped_lock lock(target->Mutex);
            target->Front.reset();
            target->Ready.reset();
            target->Pool.clear();
            target->Graph.reset();
            target->DepthImage.Destroy();
        }
        DrainTextureGraveyard();
    }

    void Renderer::ViewportLoop(std::stop_token stopToken)
    {
        VulkanCommandPool pool(m_Device, m_Logger, m_Device->GetQueueIndices().Graphics);
        std::uint64_t seenSignal = 0;
        auto lastFrameEnd = std::chrono::steady_clock::now();

        while (!stopToken.stop_requested())
        {
            std::queue<std::move_only_function<void()>> tasks;
            {
                std::scoped_lock lock(m_ViewportMutex);
                tasks.swap(m_ViewportQueue);
            }
            while (!tasks.empty())
            {
                try
                {
                    tasks.front()();
                }
                catch (const std::exception& error)
                {
                    m_Logger->Error("Viewport thread task failed: {}", error.what());
                }
                tasks.pop();
            }

            bool rendered = false;
            for (const auto& target : SnapshotBufferTargets())
            {
                if (stopToken.stop_requested())
                    break;
                if (!target->Attached.load(std::memory_order_acquire))
                    continue;

                target->LayerStack->OnSafePoint();
                if (!target->Visible.load(std::memory_order_relaxed))
                    continue;
                rendered |= RenderTargetFrame(*target, pool, true, stopToken);
            }

            if (rendered)
            {
                if (const auto maxFps = m_RenderOptions->ViewportMaxFps; maxFps > 0)
                {
                    const auto deadline = lastFrameEnd + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                        std::chrono::duration<double>(1.0 / maxFps));
                    std::unique_lock lock(m_ViewportMutex);
                    m_ViewportWake.wait_until(lock, stopToken, deadline, [] { return false; });
                }
                lastFrameEnd = std::chrono::steady_clock::now();
            }
            else
            {
                std::unique_lock lock(m_ViewportMutex);
                m_ViewportWake.wait_for(lock, stopToken, std::chrono::milliseconds(50), [&]
                {
                    return m_ViewportSignal != seenSignal || !m_ViewportQueue.empty();
                });
                seenSignal = m_ViewportSignal;
            }
        }

        for (const auto& target : SnapshotBufferTargets())
        {
            if (target->Attached.exchange(false) && target->LayerStack)
            {
                target->LayerStack->OnDetach();
            }
        }
    }

    void Renderer::RenderHeadlessFrame(const float deltaTime, VulkanCommandPool& pool)
    {
        bool rendered = false;
        for (const auto& target : SnapshotBufferTargets())
        {
            if (!target->Attached.load(std::memory_order_acquire))
                continue;
            target->LayerStack->OnUpdate(deltaTime);
            rendered |= RenderTargetFrame(*target, pool, false, {});
        }
        if (!rendered)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    Renderer::ReadbackResult Renderer::ReadBackBufferTarget(const std::uint32_t bufferId, const std::uint64_t minSerial,
                                                            const glm::uvec2 requiredExtent)
    {
        ReadbackResult result;
        const auto target = FindBufferTarget(bufferId);
        if (!target)
        {
            return result;
        }
        std::shared_ptr<TargetImage> image;
        {
            std::scoped_lock lock(target->Mutex);
            image = target->Ready ? target->Ready : target->Front;
        }
        if (!image || image->Serial < minSerial || image->Layout != vk::ImageLayout::eShaderReadOnlyOptimal)
        {
            return result;
        }
        if ((requiredExtent.x != 0 || requiredExtent.y != 0) && image->Extent != requiredExtent)
        {
            return result;
        }

        const auto format = image->Color.GetSpecification().format;
        const auto extent = image->Extent;
        const auto byteSize = static_cast<vk::DeviceSize>(extent.x) * extent.y * 4;

        VulkanBuffer staging(m_Device, MakeReadbackBufferSpecification(byteSize), m_Logger);

        ImmediateSubmit(*m_CommandPool, m_Device->GetGraphicsQueue(), [&](const vk::CommandBuffer cmd)
        {
            TransitionImageLayout(cmd, image->Color.GetImage(), format,
                                  vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eTransferSrcOptimal);
            CopyImageToBuffer(cmd, image->Color.GetImage(), staging.GetBuffer(),
                             vk::Extent3D{extent.x, extent.y, 1});
            TransitionImageLayout(cmd, image->Color.GetImage(), format,
                                  vk::ImageLayout::eTransferSrcOptimal, vk::ImageLayout::eShaderReadOnlyOptimal);
        });

        result.Pixels.resize(byteSize);
        staging.Read(result.Pixels.data(), byteSize);
        result.Extent = vk::Extent2D{extent.x, extent.y};
        result.Format = format;
        result.Serial = image->Serial;
        return result;
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
        const bool headless = m_WindowManager->IsHeadless();
        if (!headless)
        {
            m_ViewportRunning.store(true, std::memory_order_release);
            m_ViewportThread = std::jthread([this](std::stop_token token) { ViewportLoop(std::move(token)); });
        }
        m_ReadyPromise.set_value();

        std::optional<VulkanCommandPool> headlessPool;
        if (headless)
        {
            headlessPool.emplace(m_Device, m_Logger, m_Device->GetQueueIndices().Graphics);
        }

        int iterations = 0;
        const auto start = std::chrono::high_resolution_clock::now();
        auto lastUiFrame = std::chrono::steady_clock::now();
        auto lastStatsLog = lastUiFrame;

        const auto guardImGuiQueue = [this]() -> std::unique_lock<std::mutex>
        {
            if (m_Device->HasDedicatedBackgroundQueue())
                return {};
            return std::unique_lock(m_Device->GetQueueMutex(m_Device->GetGraphicsQueue()));
        };

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
                try
                {
                    task();
                }
                catch (const std::exception& error)
                {
                    m_Logger->Error("Render queue task failed: {}", error.what());
                }
            }
            if (headless)
            {
                const float elapsed = static_cast<float>(
                    std::chrono::duration<double>(
                        std::chrono::high_resolution_clock::now() - start).count());
                const float deltaTime = std::max(0.0f, elapsed - m_LastFrameElapsed);
                m_LastFrameElapsed = elapsed;
                RenderHeadlessFrame(deltaTime, *headlessPool);
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

            if (m_MainWindowResources.LayerStack)
                m_MainWindowResources.LayerStack->OnSafePoint();
            for (auto& [windowId, resources] : m_WindowResources)
            {
                if (resources.LayerStack)
                    resources.LayerStack->OnSafePoint();
            }

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
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            }

            if (!m_RenderOptions->VSync && m_RenderOptions->UiMaxFps > 0)
            {
                const auto interval = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                    std::chrono::duration<double>(1.0 / m_RenderOptions->UiMaxFps));
                std::this_thread::sleep_until(lastUiFrame + interval);
            }

            const std::uint32_t frameSlot = m_FrameIndex;
            FrameResources& frame = m_FrameResources[frameSlot];

            {
                const auto fence = frame.InFlightFence.GetFence();
                try
                {
                    if (m_Device->GetDevice().waitForFences(1, &fence, vk::True, 100'000'000ull) ==
                        vk::Result::eTimeout)
                    {
                        continue;
                    }
                }
                catch (const vk::SystemError& error)
                {
                    m_Logger->Error("Waiting for the UI frame failed: {}", error.what());
                    break;
                }
            }

            if (!m_FrameKeepAlive[frameSlot].empty())
            {
                m_FrameKeepAlive[frameSlot].clear();
                WakeViewport();
            }
            DrainTextureGraveyard();

            std::vector<WindowResources*> framed;
            framed.reserve(activeWindows.size());
            for (auto* resources : activeWindows)
            {
                const auto result = resources->SwapChain->AcquireNextImage(
                    resources->ImageAvailableSemaphores[frameSlot].GetSemaphore(), nullptr, 50'000'000ull);
                if (result == SwapchainAcquireResult::Acquired)
                {
                    framed.push_back(resources);
                }
            }
            if (framed.empty())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                continue;
            }

            frame.InFlightFence.Reset();
            m_FrameIndex = (m_FrameIndex + 1) % m_FrameResources.size();
            iterations++;

            auto& commandBuffer = frame.CommandBuffer;
            commandBuffer.Begin();
            const auto frameNow = std::chrono::steady_clock::now();
            const float elapsed = static_cast<float>(
                std::chrono::duration<double>(
                    std::chrono::high_resolution_clock::now() - start).count());
            const float deltaTime = std::max(0.0f, elapsed - m_LastFrameElapsed);
            m_LastFrameElapsed = elapsed;
            const auto rawCommandBuffer = commandBuffer.GetCommandBuffer();

            if (m_MainWindowResources.ImGuiContext)
            {
                ImGui::SetCurrentContext(
                    static_cast<ImGuiContext*>(m_MainWindowResources.ImGuiContext));
                {
                    const auto queueGuard = guardImGuiQueue();
                    TakeFrontImages(frameSlot);
                    ImGui_ImplVulkan_NewFrame();
                }
                ImGui_ImplSDL3_NewFrame();
                ImGui::NewFrame();
                if (m_MainWindowResources.EnableDockSpace &&
                    (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable))
                    m_MainDockspaceId = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
                for (const auto& target : SnapshotBufferTargets())
                {
                    if (target->Attached.load(std::memory_order_acquire))
                    {
                        target->LayerStack->OnUpdate(deltaTime);
                        target->LayerStack->OnUiRender();
                    }
                }
                if (m_MainWindowResources.LayerStack)
                {
                    m_MainWindowResources.LayerStack->OnUpdate(deltaTime);
                    m_MainWindowResources.LayerStack->OnUiRender();
                }
                ImGui::Render();
            }

            for (auto* resources : framed)
            {
                if (resources == &m_MainWindowResources || !resources->ImGuiContext)
                    continue;
                ImGui::SetCurrentContext(static_cast<ImGuiContext*>(resources->ImGuiContext));
                {
                    const auto queueGuard = guardImGuiQueue();
                    ImGui_ImplVulkan_NewFrame();
                }
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
            for (auto* resources : framed)
            {
                if (resources->ImGuiContext)
                    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(resources->ImGuiContext));
                const auto queueGuard = guardImGuiQueue();
                RenderWindow(*resources, rawCommandBuffer, frameSlot);
            }
            commandBuffer.End();

            std::vector<vk::Semaphore> waitSemaphores;
            std::vector<vk::PipelineStageFlags> waitStages;
            std::vector<vk::Semaphore> signalSemaphores;
            waitSemaphores.reserve(framed.size());
            waitStages.reserve(framed.size());
            signalSemaphores.reserve(framed.size());
            for (auto* resources : framed)
            {
                waitSemaphores.push_back(resources->ImageAvailableSemaphores[frameSlot].GetSemaphore());
                waitStages.push_back(vk::PipelineStageFlagBits::eColorAttachmentOutput);
                signalSemaphores.push_back(
                    resources->RenderFinishedSemaphores[
                        resources->SwapChain->GetCurrentImageIndex()].GetSemaphore());
            }
            vk::SubmitInfo submitInfo{};
            submitInfo.waitSemaphoreCount = static_cast<std::uint32_t>(waitSemaphores.size());
            submitInfo.pWaitSemaphores = waitSemaphores.data();
            submitInfo.pWaitDstStageMask = waitStages.data();
            submitInfo.signalSemaphoreCount = static_cast<std::uint32_t>(signalSemaphores.size());
            submitInfo.pSignalSemaphores = signalSemaphores.data();
            const auto rawSubmitBuffer = commandBuffer.GetCommandBuffer();
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &rawSubmitBuffer;
            const auto graphicsQueue = m_Device->GetGraphicsQueue();
            vk::Result submitResult;
            try
            {
                std::scoped_lock lock(m_Device->GetQueueMutex(graphicsQueue));
                submitResult = graphicsQueue.submit(1, &submitInfo, frame.InFlightFence.GetFence());
            }
            catch (const vk::SystemError& error)
            {
                m_Logger->Error("Failed to submit UI frame: {}", error.what());
                break;
            }
            if (submitResult != vk::Result::eSuccess)
            {
                m_Logger->Error("Failed to submit UI frame: {}", vk::to_string(submitResult));
                break;
            }

            for (std::size_t index = 0; index < framed.size(); ++index)
            {
                framed[index]->SwapChain->Present(m_Device->GetPresentQueue(), signalSemaphores[index]);
            }

            const auto frameEnd = std::chrono::steady_clock::now();
            m_UiFrameMeter.Tick(std::chrono::duration<double, std::milli>(frameEnd - lastUiFrame).count(), frameEnd);
            lastUiFrame = frameEnd;
            (void)frameNow;

            if (frameEnd - lastStatsLog >= std::chrono::seconds(5))
            {
                lastStatsLog = frameEnd;
                const auto uiStats = GetUiStats();
                m_Logger->Debug("Render stats: UI {:.1f} fps (avg {:.2f} ms, max {:.2f} ms)", uiStats.FramesPerSecond,
                                uiStats.AverageFrameMs, uiStats.MaxFrameMs);
                for (const auto& target : SnapshotBufferTargets())
                {
                    if (const auto stats = GetBufferTargetStats(target->Id))
                    {
                        m_Logger->Debug("Render stats: target {} {:.1f} fps (avg {:.1f} ms, gpu {:.1f} ms in {} submissions, {}x{}, {})",
                                        target->Id, stats->FramesPerSecond, stats->AverageFrameMs, stats->GpuMs,
                                        stats->Submissions, stats->Extent.x, stats->Extent.y,
                                        stats->Visible ? "visible" : "hidden");
                    }
                }
            }
        }

        m_ViewportRunning.store(false, std::memory_order_release);
        if (m_ViewportThread.joinable())
        {
            m_ViewportThread.request_stop();
            WakeViewport();
            m_ViewportThread.join();
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
                try
                {
                    task();
                }
                catch (const std::exception& error)
                {
                    m_Logger->Error("Render queue task failed during shutdown: {}", error.what());
                }
            }
        }
        if (headless)
        {
            for (const auto& target : SnapshotBufferTargets())
            {
                if (target->Attached.exchange(false) && target->LayerStack)
                    target->LayerStack->OnDetach();
            }
            headlessPool.reset();
        }
        ShutdownBufferTargets();
        ShutdownImGuiForWindow(m_MainWindowResources);
        for (auto& [windowId, resources] : m_WindowResources)
        {
            ShutdownImGuiForWindow(resources);
        }
        if (m_MainWindowResources.LayerStack)
            m_MainWindowResources.LayerStack->OnDetach();
        for (auto& [windowId, resources] : m_WindowResources)
        {
            if (resources.LayerStack)
                resources.LayerStack->OnDetach();
        }
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
        const auto end = std::chrono::high_resolution_clock::now();
        const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        const auto averageFps = iterations / (std::max<std::int64_t>(duration.count(), 1) / 1000.0);
        m_Logger->Info("Render thread completed. Iterations: {}, Time: {} ms, Average FPS: {}",
                       iterations, duration.count(), averageFps);
    }
}

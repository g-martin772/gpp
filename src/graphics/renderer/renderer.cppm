export module GPP.Graphics:Renderer;

import glm;
import vulkan;
import GPP.Core;
import :Vulkan;
import :Windowing;
import :Shader;
import :HotReload;
import :RenderConfig;
import :RenderGraph;
import :Application.Layer;
import :Application.Theme;
import :FontAssets;
import :UI.Preferences;

namespace GPP
{
    class VulkanContext;
    class WindowManager;

    export class Renderer : public IHostedService
    {
    public:
        using Dependencies = std::tuple<VulkanContext, WindowManager, WindowOptions, WindowDefinitions, RenderOptions,
                                         Logger, IFileSystem, InputState, EventDispatcher, ImGuiOptions, ThemeProxy,
                                         UiPreferences, FontAssetCatalog>;

        Renderer(const std::shared_ptr<VulkanContext>& vulkanContext,
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
                 const std::shared_ptr<FontAssetCatalog>& fontAssets);

        Task<void> StartAsync(std::stop_token stopToken) override;
        Task<void> StopAsync() override;

        Task<void> AwaitReady()
        {
            co_await m_SharedFuture;
            co_return;
        }

        const std::shared_ptr<VulkanDevice>& GetDevice() const noexcept { return m_Device; }

        const std::shared_ptr<VulkanSwapChain>& GetSwapChain() const noexcept
        {
            return m_MainWindowResources.SwapChain;
        }

        const std::shared_ptr<VulkanSwapChain>& GetSwapChain(WindowId id) const noexcept;
        Task<std::shared_ptr<Window>> CreateWindow(const WindowOptions& options,
                                                   std::string name = {});

        void AttachLayerStackToWindow(GuiLayerStack& layerStack, const std::shared_ptr<Window>& window);
        void AttachLayerStackToBuffer(GuiLayerStack& layerStack, std::uint32_t bufferId);

        struct RenderTargetInfo
        {
            vk::Extent2D Extent{};
            vk::ImageView ImageView{};
            vk::Sampler Sampler{};
            void* ImGuiTexture = nullptr;
        };

        [[nodiscard]] std::optional<RenderTargetInfo> GetRenderTargetInfo(
            std::uint32_t bufferId) const;

        void ResizeBufferTarget(std::uint32_t bufferId, glm::uvec2 extent);

        struct WindowResources
        {
            std::shared_ptr<GPP::Window> Window;
            vk::SurfaceKHR Surface;
            std::shared_ptr<VulkanSwapChain> SwapChain;
            VulkanImage DepthImage;
            vk::ImageLayout DepthLayout = vk::ImageLayout::eUndefined;
            GuiLayerStack* LayerStack = nullptr;
            std::vector<VulkanSemaphore> ImageAvailableSemaphores;
            std::vector<VulkanSemaphore> RenderFinishedSemaphores;
            std::vector<vk::ImageLayout> ImageLayouts;
            void* ImGuiContext = nullptr;
            std::string ImGuiIniPath;
            unsigned int ImGuiEffectiveConfigFlags = 0;
            bool EnableDockSpace = false;
            std::unordered_map<std::string, void*> FontCache;
            std::unique_ptr<RenderGraph> Graph;

            ~WindowResources();
        };

    private:
        void InitializeRenderSystem();
        void InitializeWindowResources(const std::shared_ptr<Window>& window,
                                       WindowResources& resources);
        void InitializeWindowSync(WindowResources& resources);
        void InitializeImGuiForWindow(WindowResources& resources, const WindowOptions& windowOptions);
        void ShutdownImGuiForWindow(WindowResources& resources);
        void ApplyThemeToContext(WindowResources& resources) const;
        void ApplyFontPreferencesToContext(WindowResources& resources) const;
        void ApplyThemeToAllContexts();
        void ApplyFontPreferencesToAllContexts();
        void RenderWindow(WindowResources& resources, vk::CommandBuffer commandBuffer,
                          float elapsed, bool renderTargets, std::uint32_t frameIndex);
        Task<void> StopRenderSystem();
        void RenderLoop(std::stop_token stopToken);

        std::shared_ptr<VulkanContext> m_VulkanContext;
        std::shared_ptr<WindowManager> m_WindowManager;
        std::shared_ptr<WindowOptions> m_WindowOptions;
        std::shared_ptr<WindowDefinitions> m_WindowDefinitions;
        std::shared_ptr<RenderOptions> m_RenderOptions;
        std::shared_ptr<IFileSystem> m_FileSystem;
        std::shared_ptr<Logger> m_Logger;
        std::shared_ptr<InputState> m_InputState;
        std::shared_ptr<EventDispatcher> m_Dispatcher;
        std::shared_ptr<ImGuiOptions> m_ImGuiOptions;
        std::shared_ptr<ThemeProxy> m_ThemeProxy;
        std::shared_ptr<UiPreferences> m_UiPreferences;
        std::shared_ptr<FontAssetCatalog> m_FontAssets;
        std::shared_ptr<VulkanDevice> m_Device;
        std::shared_ptr<VulkanCommandPool> m_CommandPool;
        EventSubscription m_ResizeSubscription{};
        EventSubscription m_WindowCloseSubscription{};
        EventSubscription m_ThemeChangedSubscription{};
        EventSubscription m_UiPreferencesSubscription{};
        std::vector<EventSubscription> m_ImGuiInputSubscriptions{};
        mutable std::mutex m_RenderQueueMutex{};
        std::queue<std::move_only_function<void()>> m_RenderQueue{};
        std::unordered_map<std::uint32_t, glm::uvec2> m_PendingResize{};

        struct FrameResources
        {
            VulkanCommandBuffer CommandBuffer;
            VulkanSemaphore ImageAvailableSemaphore;
            VulkanSemaphore RenderFinishedSemaphore;
            VulkanFence InFlightFence;

            FrameResources() = default;
            FrameResources(VulkanCommandBuffer commandBuffer, vk::Device device) noexcept;

            FrameResources(const FrameResources&) = delete;
            FrameResources& operator=(const FrameResources&) = delete;
            FrameResources(FrameResources&&) noexcept = default;
            FrameResources& operator=(FrameResources&&) noexcept = default;
            ~FrameResources();
        };

        struct BufferTargetResources
        {
            GuiLayerStack* LayerStack = nullptr;
            VulkanImage ColorImage;
            VulkanImage DepthImage;
            vk::ImageLayout ColorLayout = vk::ImageLayout::eUndefined;
            vk::ImageLayout DepthLayout = vk::ImageLayout::eUndefined;
            void* ImGuiTexture = nullptr;
            glm::uvec2 Extent{640, 360};
            std::unique_ptr<RenderGraph> Graph;
        };

        WindowResources m_MainWindowResources{};
        std::unordered_map<WindowId, WindowResources> m_WindowResources{};
        std::vector<FrameResources> m_FrameResources{};
        std::vector<VulkanSemaphore> m_RenderFinishedSemaphores{};
        std::vector<vk::ImageLayout> m_SwapchainImageLayouts{};
        std::uint32_t m_FrameIndex = 0;
        float m_LastFrameElapsed = 0.0f;

        bool m_ImGuiEnabled = false;
        std::unordered_map<std::uint32_t, BufferTargetResources> m_BufferTargets;

        std::thread m_RenderThread;
        std::atomic<bool> m_Running{true};
        std::promise<void> m_ReadyPromise;
        std::shared_future<void> m_SharedFuture{m_ReadyPromise.get_future().share()};

        GuiLayerStack* m_MainLayerStack = nullptr;
    };
}

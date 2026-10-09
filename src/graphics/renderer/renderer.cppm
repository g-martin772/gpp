export module GPP.Graphics:Renderer;

import std;
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

    struct TextureGraveyard
    {
        std::mutex Mutex;
        std::vector<void*> Items;

        void Push(void* texture)
        {
            std::scoped_lock lock(Mutex);
            Items.push_back(texture);
        }

        std::vector<void*> Take()
        {
            std::scoped_lock lock(Mutex);
            return std::exchange(Items, {});
        }
    };

    struct TargetImage
    {
        VulkanImage Color;
        glm::uvec2 Extent{0, 0};
        vk::ImageLayout Layout = vk::ImageLayout::eUndefined;
        std::uint64_t Serial = 0;
        void* ImGuiTexture = nullptr;
        std::shared_ptr<TextureGraveyard> Graveyard;

        TargetImage() = default;
        TargetImage(const TargetImage&) = delete;
        TargetImage& operator=(const TargetImage&) = delete;

        ~TargetImage()
        {
            if (ImGuiTexture && Graveyard)
            {
                Graveyard->Push(ImGuiTexture);
            }
        }
    };

    struct BufferTargetResources
    {
        static constexpr std::size_t kMaxImages = 5;

        explicit BufferTargetResources(const std::uint32_t id) : Id(id) {}
        BufferTargetResources(const BufferTargetResources&) = delete;
        BufferTargetResources& operator=(const BufferTargetResources&) = delete;

        static constexpr std::uint64_t PackExtent(const glm::uvec2 e) noexcept
        {
            return (static_cast<std::uint64_t>(e.x) << 32) | e.y;
        }

        static constexpr glm::uvec2 UnpackExtent(const std::uint64_t packed) noexcept
        {
            return {static_cast<std::uint32_t>(packed >> 32), static_cast<std::uint32_t>(packed & 0xFFFFFFFFu)};
        }

        const std::uint32_t Id;
        GuiLayerStack* LayerStack = nullptr;
        std::atomic<bool> Attached{false};
        std::atomic<bool> Visible{true};
        std::atomic<std::uint64_t> DesiredExtent{PackExtent({640, 360})};

        // --- render-thread private ---
        glm::uvec2 Extent{640, 360};
        VulkanImage DepthImage;
        vk::ImageLayout DepthLayout = vk::ImageLayout::eUndefined;
        std::unique_ptr<RenderGraph> Graph;
        std::unordered_map<std::string, AdaptiveSlicer> Slicers;
        std::uint64_t NextSerial = 1;

        // --- hand-off render thread -> UI thread ---
        mutable std::mutex Mutex;
        std::vector<std::shared_ptr<TargetImage>> Pool; // every image; free <=> use_count() == 1
        std::shared_ptr<TargetImage> Ready;

        // --- UI-thread private ---
        std::shared_ptr<TargetImage> Front;

        // --- metrics (render thread writes, anyone reads) ---
        RateMeter FrameMeter;
        std::atomic<double> LastGpuMs{0.0};
        std::atomic<std::uint32_t> LastSubmissions{0};
        std::atomic<std::int64_t> InFlightSinceNs{0};
    };

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
        ~Renderer() override;

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
            std::uint64_t Serial = 0;
        };

        [[nodiscard]] std::optional<RenderTargetInfo> GetRenderTargetInfo(std::uint32_t bufferId) const;

        void ResizeBufferTarget(std::uint32_t bufferId, glm::uvec2 extent);

        void SetBufferTargetVisible(std::uint32_t bufferId, bool visible);

        [[nodiscard]] unsigned int GetMainDockspaceId() const noexcept { return m_MainDockspaceId; }

        struct ReadbackResult
        {
            std::vector<std::uint8_t> Pixels; // tightly packed, row 0 first, 4 bytes/pixel, channel
                                              // order given by Format (no flip needed for PNG: Vulkan
                                              // and PNG both consider row 0 the top row)
            vk::Extent2D Extent{};
            vk::Format Format{};
            std::uint64_t Serial = 0;
        };

        [[nodiscard]] ReadbackResult ReadBackBufferTarget(std::uint32_t bufferId, std::uint64_t minSerial = 0,
                                                          glm::uvec2 requiredExtent = {0, 0});

        struct BufferTargetStats
        {
            double FramesPerSecond = 0.0;
            double FrameMs = 0.0;
            double AverageFrameMs = 0.0;
            double GpuMs = 0.0;
            double CurrentFrameMs = 0.0;
            std::uint32_t Submissions = 0;
            std::uint64_t FramesRendered = 0;
            glm::uvec2 Extent{0, 0};
            bool Visible = false;
        };

        [[nodiscard]] std::optional<BufferTargetStats> GetBufferTargetStats(std::uint32_t bufferId) const;

        struct UiStats
        {
            double FramesPerSecond = 0.0;
            double FrameMs = 0.0;
            double AverageFrameMs = 0.0;
            double MaxFrameMs = 0.0;
        };
        [[nodiscard]] UiStats GetUiStats() const;

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
        class ViewportChunkSink;

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
        void RenderWindow(WindowResources& resources, vk::CommandBuffer commandBuffer, std::uint32_t frameIndex);
        Task<void> StopRenderSystem();
        void RenderLoop(std::stop_token stopToken);
        void RenderHeadlessFrame(float deltaTime, VulkanCommandPool& pool);

        [[nodiscard]] std::shared_ptr<BufferTargetResources> FindBufferTarget(std::uint32_t bufferId) const;
        [[nodiscard]] std::vector<std::shared_ptr<BufferTargetResources>> SnapshotBufferTargets() const;

        void PostToBufferThread(std::move_only_function<void()> task);
        void WakeViewport();
        void ViewportLoop(std::stop_token stopToken);

        bool RenderTargetFrame(BufferTargetResources& target, VulkanCommandPool& pool, bool paceToConsumer,
                               std::stop_token stopToken);
        [[nodiscard]] std::shared_ptr<TargetImage> AcquireTargetImage(BufferTargetResources& target);
        void ApplyDesiredExtent(BufferTargetResources& target);

        void TakeFrontImages(std::uint32_t frameSlot);
        void DrainTextureGraveyard();
        void ShutdownBufferTargets();

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

        WindowResources m_MainWindowResources{};
        std::unordered_map<WindowId, WindowResources> m_WindowResources{};
        std::vector<FrameResources> m_FrameResources{};
        std::vector<std::vector<std::shared_ptr<TargetImage>>> m_FrameKeepAlive{};
        std::vector<VulkanSemaphore> m_RenderFinishedSemaphores{};
        std::vector<vk::ImageLayout> m_SwapchainImageLayouts{};
        std::uint32_t m_FrameIndex = 0;
        unsigned int m_MainDockspaceId = 0;
        float m_LastFrameElapsed = 0.0f;

        bool m_ImGuiEnabled = false;

        mutable std::shared_mutex m_TargetsMutex;
        std::unordered_map<std::uint32_t, std::shared_ptr<BufferTargetResources>> m_BufferTargets;
        std::shared_ptr<TextureGraveyard> m_TextureGraveyard = std::make_shared<TextureGraveyard>();
        vk::Format m_TargetColorFormat = vk::Format::eB8G8R8A8Unorm;
        vk::Format m_TargetDepthFormat = vk::Format::eD32Sfloat;

        std::jthread m_ViewportThread;
        std::mutex m_ViewportMutex;
        std::condition_variable_any m_ViewportWake;
        std::uint64_t m_ViewportSignal = 0;
        std::queue<std::move_only_function<void()>> m_ViewportQueue;
        std::atomic<bool> m_ViewportRunning{false};

        RateMeter m_UiFrameMeter;

        std::thread m_RenderThread;
        std::atomic<bool> m_Running{true};
        std::promise<void> m_ReadyPromise;
        std::shared_future<void> m_SharedFuture{m_ReadyPromise.get_future().share()};

        GuiLayerStack* m_MainLayerStack = nullptr;
    };
}

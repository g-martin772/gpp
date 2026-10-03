import GPP;
import std;

using namespace GPP;

struct DynamicLayer : public GuiLayer
{
    using Dependencies = std::tuple<Logger>;

    explicit DynamicLayer(const std::shared_ptr<Logger>& logger) : GuiLayer(logger)
    {
    }

    void OnUiRender() override
    {
        ImGui::Begin("Dynamic window");
        ImGui::TextUnformatted("This layer was attached after startup.");
        ImGui::End();
    }
};

class TestService : public IHostedService
{
public:
    using Dependencies = std::tuple<WindowManager, WindowOptions, Logger, Renderer>;

    TestService(std::shared_ptr<WindowManager> wm,
                std::shared_ptr<WindowOptions> wo,
                std::shared_ptr<Logger> logger,
                std::shared_ptr<Renderer> renderer)
        : m_WM(wm), m_WO(wo), m_Logger(logger), m_Renderer(renderer)
    {
    }

    Task<void> StartAsync(std::stop_token stopToken) override
    {
        Spawn(Run());
        co_return;
    }

    Task<void> StopAsync() override
    {
        co_return;
    }

private:
    Task<void> Run()
    {
        co_await ResumeOn(ThreadPool::Instance());
        co_await m_WM->AwaitReady();
        co_await m_Renderer->AwaitReady();
        WindowOptions secondOptions = *m_WO;
        secondOptions.Title = "GPP Secondary Window";
        secondOptions.Width = 800;
        secondOptions.Height = 600;
        m_SecondaryWindow = co_await m_Renderer->CreateWindow(secondOptions, "dynamic");
        m_DynamicLayerStack.PushLayer(std::make_shared<DynamicLayer>(m_Logger));
        m_Renderer->AttachLayerStackToWindow(m_DynamicLayerStack, m_SecondaryWindow);
        m_Logger->Info("Secondary engine window created with ID {}", m_SecondaryWindow->GetID());
        //co_await DelayAsync(std::chrono::seconds(1));
        //co_await m_WM->ShowMessageBox("Test", "This is a test message.");
        co_return;
    }

    std::shared_ptr<WindowManager> m_WM;
    std::shared_ptr<WindowOptions> m_WO;
    std::shared_ptr<Logger> m_Logger;
    std::shared_ptr<Renderer> m_Renderer;
    std::shared_ptr<Window> m_SecondaryWindow;
    GuiLayerStack m_DynamicLayerStack;
};

struct MainLayer : public GuiLayer
{
    using Dependencies = std::tuple<Logger, Renderer>;

    MainLayer(const std::shared_ptr<Logger>& logger, const std::shared_ptr<Renderer>& renderer)
        : GuiLayer(logger), m_Renderer(renderer)
    {
    }

    void OnAttach() override
    {
        m_Logger->Info("MainLayer attached");
    }

    void OnRender() override
    {
        //m_Logger->Info("Rendering MainLayer");
    }

    void OnDetach() override
    {
        m_Logger->Info("MainLayer detached");
    }

    void OnUiRender() override
    {
        ImGui::ShowDebugLogWindow(&m_ShowDebugLogWindow);
        ImGui::ShowDemoWindow(&m_ShowDemoWindow);
        ImGui::ShowMetricsWindow(&m_ShowMetricsWindow);
    }

private:
    bool m_ShowDemoWindow = true, m_ShowDebugLogWindow = true, m_ShowMetricsWindow = true;
    std::shared_ptr<Renderer> m_Renderer;
};

struct ViewportLayer : public GuiLayer
{
    using Dependencies = std::tuple<Logger, Renderer>;

    ViewportLayer(const std::shared_ptr<Logger>& logger, const std::shared_ptr<Renderer>& renderer)
        : GuiLayer(logger), m_Renderer(renderer)
    {
    }

    void OnAttach() override
    {
        m_Logger->Info("ViewportLayer attached");
    }

    void OnRender() override
    {
        //m_Logger->Info("Rendering ViewportLayer");
    }

    void OnUiRender() override
    {
        ImGui::Begin("GPP Dockspace");
        ImGui::TextUnformatted("Main engine window");
        ImGui::TextUnformatted("The installed ImGui build lacks docking support.");
        if (const auto target = m_Renderer->GetRenderTargetInfo(5))
        {
            ImGui::Separator();
            ImGui::TextUnformatted("Offscreen cube render target");
            ImGui::Image(
                reinterpret_cast<ImTextureID>(target->ImGuiTexture),
                ImVec2(static_cast<float>(target->Extent.width),
                       static_cast<float>(target->Extent.height)));
        }
        ImGui::End();
    }

    void OnDetach() override
    {
        m_Logger->Info("ViewportLayer detached");
    }
private:
    std::shared_ptr<Renderer> m_Renderer;
};

struct DemoTheme final : public Theme
{
    using Dependencies = std::tuple<Logger>;

    explicit DemoTheme(const std::shared_ptr<Logger>& logger) : Theme(logger)
    {
    }

    void Apply(ImGuiStyle& style, ImGuiIO&) override
    {
        ImGui::StyleColorsDark(&style);
        style.WindowRounding = 6.0f;
        style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.26f, 0.42f, 0.78f, 1.0f);
    }
};

struct SecondaryLayer : public GuiLayer
{
    using Dependencies = std::tuple<Logger>;

    explicit SecondaryLayer(const std::shared_ptr<Logger>& logger) : GuiLayer(logger)
    {
    }

    void OnUiRender() override
    {
        ImGui::Begin("Secondary window");
        ImGui::TextUnformatted("This layer was declared with the builder.");
        ImGui::End();
    }
};

int main(int argc, char* argv[])
{
    auto builder = GuiApplicationBuilder();

    builder.Configuration
           .AddJsonFile("tests/scratch/config.json")
           .AddCommandLine(argc, argv)
           .AddEnvironmentVariables();

    // Global ImGui defaults: every window gets docking + a dockspace unless its own WindowOptions
    // (here, or "ImGui" in config.json) overrides it.
    builder.ConfigureImGui({"DockingEnable", "ViewportsEnable"}, true);
    builder.SetTheme<DemoTheme>();

    WindowOptions upfrontOptions;
    upfrontOptions.Title = "GPP Upfront Window";
    upfrontOptions.ImGuiDockSpace = false; // this window opts out of the global dockspace default
    builder.AddWindow("upfront", upfrontOptions);
    builder.AddGuiLayer<SecondaryLayer>()
           .SetWindowTarget("upfront");

    builder.Services.AddHostedService<TestService>();

    builder.AddGuiLayer<MainLayer>()
           .SetWindowTarget("main");
    builder.AddGuiLayer<ViewportLayer>()
           .SetBufferTarget(5);

    builder.AddHotReloadableLayer("demo-layer", "demo_hot_reload_layer.so")
           .SetWindowTarget("main");

    auto app = builder.Build();


    return app->Run();
}

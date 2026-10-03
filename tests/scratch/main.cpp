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

        // WindowOptions secondOptions = *m_WO;
        // secondOptions.Title = "GPP Secondary Window";
        // secondOptions.Width = 800;
        // secondOptions.Height = 600;
        // m_SecondaryWindow = co_await m_Renderer->CreateWindow(secondOptions, "dynamic");
        // m_DynamicLayerStack.PushLayer(std::make_shared<DynamicLayer>(m_Logger));
        // m_Renderer->AttachLayerStackToWindow(m_DynamicLayerStack, m_SecondaryWindow);
        // m_Logger->Info("Secondary engine window created with ID {}", m_SecondaryWindow->GetID());

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

struct FontPickerLayer : public GuiLayer
{
    using Dependencies = std::tuple<Logger, FontAssetCatalog, UiPreferences>;

    FontPickerLayer(const std::shared_ptr<Logger>& logger,
                    const std::shared_ptr<FontAssetCatalog>& fontAssets,
                    const std::shared_ptr<UiPreferences>& uiPreferences)
        : GuiLayer(logger), m_FontAssets(fontAssets), m_UiPreferences(uiPreferences)
    {
    }

    void OnUiRender() override
    {
        ImGui::Begin("Font & UI Scale");

        const auto fonts = m_FontAssets->GetAvailableFonts();
        const auto current = m_UiPreferences->GetFontName();
        const auto currentLabel = current.empty() ? "(default)" : current;

        if (ImGui::BeginCombo("Font", currentLabel.c_str()))
        {
            if (ImGui::Selectable("(default)", current.empty()))
            {
                m_UiPreferences->SetFont("", m_FontSize);
            }
            for (const auto& font : fonts)
            {
                const bool selected = font.Name == current;
                if (ImGui::Selectable(font.Name.c_str(), selected))
                {
                    m_UiPreferences->SetFont(font.Name, m_FontSize);
                }
                if (selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        if (ImGui::SliderFloat("Font size", &m_FontSize, 8.0f, 32.0f, "%.0f"))
        {
            if (!current.empty())
            {
                m_UiPreferences->SetFont(current, m_FontSize);
            }
        }

        float uiScale = m_UiPreferences->GetUiScale();
        if (ImGui::SliderFloat("UI scale", &uiScale, 0.5f, 2.0f, "%.2f"))
        {
            m_UiPreferences->SetUiScale(uiScale);
        }

        ImGui::End();
    }

private:
    std::shared_ptr<FontAssetCatalog> m_FontAssets;
    std::shared_ptr<UiPreferences> m_UiPreferences;
    float m_FontSize = 16.0f;
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

struct SlowMoverModule : public ISimulationModule
{
    void OnTick(Scene& scene, float deltaTime) override
    {
        for (auto [entity, transform, velocity] : scene.Registry().view<TransformComponent, VelocityComponent>().each())
        {
            transform.Position += velocity.Linear * deltaTime;
        }
    }
};

struct SimulationDemoLayer : public GuiLayer
{
    using Dependencies = std::tuple<Logger, SceneManager>;

    static constexpr auto ScenePath = "tests/scratch/simulation_demo_scene.yaml";

    SimulationDemoLayer(const std::shared_ptr<Logger>& logger, const std::shared_ptr<SceneManager>& scenes)
        : GuiLayer(logger), m_Scenes(scenes)
    {
    }

    void OnAttach() override
    {
        bool loaded = true;
        Scene scene;
        try
        {
            scene = m_Scenes->LoadSceneFromFile(ScenePath);
        }
        catch (const std::exception&)
        {
            loaded = false;
            scene = Scene("SimulationDemo");

            const auto a = scene.CreateEntity("A", "Demo");
            scene.Registry().emplace<TransformComponent>(a, TransformComponent{.Position = {-2.0f, 0.0f, 0.0f}});
            scene.Registry().emplace<VelocityComponent>(a, VelocityComponent{.Linear = {0.1f, 0.0f, 0.0f}});

            const auto b = scene.CreateEntity("B", "Demo");
            scene.Registry().emplace<TransformComponent>(b, TransformComponent{.Position = {2.0f, 0.0f, 0.0f}});
            scene.Registry().emplace<VelocityComponent>(b, VelocityComponent{.Linear = {-0.05f, 0.02f, 0.0f}});
        }

        m_Logger->Info("SimulationDemoLayer: {} scene '{}'", loaded ? "loaded" : "created", scene.Metadata().Name);

        m_Runner = m_Scenes->CreateSimulation(std::move(scene), std::make_shared<SlowMoverModule>());
        m_Runner->Start();
    }

    void OnUiRender() override
    {
        if (!m_Runner) return;

        ImGui::Begin("Simulation Demo");
        {
            auto view = m_Runner->LockRenderScene();
            for (auto [entity, meta, transform] : view->Registry().view<MetadataComponent, TransformComponent>().each())
            {
                ImGui::Text("%s: (%.2f, %.2f, %.2f)", meta.Name.c_str(),
                           transform.Position.x, transform.Position.y, transform.Position.z);
            }
        }
        ImGui::End();
    }

    void OnDetach() override
    {
        if (!m_Runner) return;

        m_Runner->Stop();

        const auto yaml = m_Runner->LockRenderScene()->SerializeToYaml();
        std::ofstream file(ScenePath, std::ios::binary | std::ios::trunc);
        file << yaml;

        m_Logger->Info("SimulationDemoLayer: saved scene to {}", ScenePath);
    }

private:
    std::shared_ptr<SceneManager> m_Scenes;
    std::shared_ptr<SimulationRunner> m_Runner;
};

int main(int argc, char* argv[])
{
    auto builder = GuiApplicationBuilder();

    builder.Configuration
           .AddJsonFile("tests/scratch/config.json")
           .AddCommandLine(argc, argv)
           .AddEnvironmentVariables();

    builder.ConfigureImGui({"DockingEnable", "ViewportsEnable"}, true);
    builder.SetTheme("demo_theme.so", true);

    // WindowOptions upfrontOptions;
    // upfrontOptions.Title = "GPP Upfront Window";
    // upfrontOptions.ImGuiDockSpace = false;
    // builder.AddWindow("upfront", upfrontOptions);
    // builder.AddGuiLayer<SecondaryLayer>()
    //        .SetWindowTarget("upfront");

    builder.Services.AddHostedService<TestService>();

    builder.AddGuiLayer<MainLayer>()
           .SetWindowTarget("main");
    builder.AddGuiLayer<ViewportLayer>()
           .SetBufferTarget(5);
    builder.AddGuiLayer<FontPickerLayer>()
           .SetWindowTarget("main");
    builder.AddGuiLayer<SimulationDemoLayer>()
           .SetWindowTarget("main");

    builder.AddHotReloadableLayer("demo-layer", "demo_hot_reload_layer.so")
           .SetWindowTarget("main");

    auto app = builder.Build();


    return app->Run();
}

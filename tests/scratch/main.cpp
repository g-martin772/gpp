#include <imgui.h>

import GPP;
import std;

using namespace GPP;

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
        m_SecondaryWindow = co_await m_WM->CreateWindow(secondOptions);
        m_Logger->Info("Secondary engine window created with ID {}", m_SecondaryWindow->GetID());
        co_await DelayAsync(std::chrono::seconds(1));
        //co_await m_WM->ShowMessageBox("Test", "This is a test message.");
        co_return;
    }

    std::shared_ptr<WindowManager> m_WM;
    std::shared_ptr<WindowOptions> m_WO;
    std::shared_ptr<Logger> m_Logger;
    std::shared_ptr<Renderer> m_Renderer;
    std::shared_ptr<Window> m_SecondaryWindow;
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

private:
    std::shared_ptr<Renderer> m_Renderer;
};

struct ViewportLayer : public GuiLayer
{
    using Dependencies = std::tuple<Logger>;

    ViewportLayer(const std::shared_ptr<Logger>& logger) : GuiLayer(logger)
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

    void OnDetach() override
    {
        m_Logger->Info("ViewportLayer detached");
    }
};

int main(int argc, char* argv[])
{
    auto builder = GuiApplicationBuilder();

    builder.Configuration
           .AddJsonFile("tests/scratch/config.json")
           .AddCommandLine(argc, argv)
           .AddEnvironmentVariables();

    builder.Services.AddHostedService<TestService>();

    builder.AddGuiLayer<MainLayer>()
           .SetWindowTarget(WindowManager::MainWindowId);
    builder.AddGuiLayer<ViewportLayer>()
           .SetBufferTarget(5);

    auto app = builder.Build();


    return app->Run();
}

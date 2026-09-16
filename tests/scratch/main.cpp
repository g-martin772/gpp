import GPP;
import std;

using namespace GPP;

class TestService : public IHostedService
{
public:
    using Dependencies = std::tuple<WindowManager, WindowOptions, Logger>;

    TestService(std::shared_ptr<WindowManager> wm, std::shared_ptr<WindowOptions> wo, std::shared_ptr<Logger> logger)
        : m_WM(wm), m_WO(wo), m_Logger(logger)
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
        //co_await m_WM->CreateWindow(*m_WO);
        co_await DelayAsync(std::chrono::seconds(1));
        //co_await m_WM->ShowMessageBox("Test", "This is a test message.");
        co_return;
    }

    std::shared_ptr<WindowManager> m_WM;
    std::shared_ptr<WindowOptions> m_WO;
    std::shared_ptr<Logger> m_Logger;
};

struct MainLayer : public GuiLayer
{
    using Dependencies = std::tuple<Logger>;

    MainLayer(const std::shared_ptr<Logger>& logger) : GuiLayer(logger)
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

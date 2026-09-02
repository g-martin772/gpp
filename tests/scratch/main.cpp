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
        co_await m_WM->AwaitReady();
        //co_await m_WM->CreateWindow(*m_WO);
        //co_await m_WM->ShowMessageBox("Test", "This is a test message.");
        co_return;
    }

    std::shared_ptr<WindowManager> m_WM;
    std::shared_ptr<WindowOptions> m_WO;
    std::shared_ptr<Logger> m_Logger;
};

int main(int argc, char* argv[])
{
    Logger::LogInfo("[App] Starting Scratch");
    Logger::LogInfo("[App] Main Thread ID: {}", std::this_thread::get_id());

    auto builder = GuiApplication::CreateBuilder();

    builder.Configuration
           .AddJsonFile("config.json", &builder.FS)
           .AddCommandLine(argc, argv)
           .AddEnvironmentVariables();

    builder.Services.Configure<WindowOptions>("Graphics:Window");
    builder.Services.AddHostedService<TestService>();

    auto app = builder.Build();

    return app.Run();
}

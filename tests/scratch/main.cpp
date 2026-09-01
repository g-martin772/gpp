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

    void StartAsync(std::stop_token stopToken) override
    {
        Spawn(RunServiceAsync());
    }

    void StopAsync() override
    {
    }

private:
    Task<void> RunServiceAsync()
    {
        co_await DelayAsync(std::chrono::milliseconds(100));
        co_await m_WM->CreateWindow(*m_WO);
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

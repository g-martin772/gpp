import GPP;
import std;

using namespace GPP;

struct WindowOptions : public IService {
    int Width{1280};
    int Height{720};
    std::string Title{"GPP Engine"};

    static WindowOptions FromConfig(const IConfigurationSection& config) {
        WindowOptions options;
        options.Width = config.GetValue<int>("Width", 1280);
        options.Height = config.GetValue<int>("Height", 720);
        options.Title = config.GetValue<std::string>("Title", "GPP Engine");
        return options;
    }
};

int main(int argc, char* argv[])
{
    Logger::LogInfo("[App] Starting Scratch");
    Logger::LogInfo("[App] Main Thread ID: {}", std::this_thread::get_id());

    auto builder = App::CreateCliBuilder();

    builder.Configuration
            .AddJsonFile("config.json", &builder.FS)
            .AddCommandLine(argc, argv)
            .AddEnvironmentVariables();

    builder.Services.Configure<WindowOptions>("Graphics:Window");

    auto app = builder.Build();
    const auto& appConfig = app.GetConfiguration();
    Logger::LogInfo("Configured window width: {}", appConfig.GetSection("Graphics:Window")->GetValue<int>("Width", 1280));

    return app.Run();
}

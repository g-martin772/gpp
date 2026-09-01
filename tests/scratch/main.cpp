import GPP;
import std;

using namespace GPP;

struct WindowOptions : public IService {
    int m_Width{1280};
    int m_Height{720};
    std::string m_Title{"GPP Engine"};
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

    builder.Services.Configure<WindowOptions>(
       "Graphics:Window",
       [](const IConfigurationSection& config) {
           WindowOptions options;
           options.m_Width = config.GetValue<int>("Width", 1280);
           options.m_Height = config.GetValue<int>("Height", 720);
           options.m_Title = config.GetValue<std::string>("Title", "Vulkan Renderer");
           return options;
       }
   );

    auto app = builder.Build();
    const auto& appConfig = app.GetConfiguration();
    Logger::LogInfo("Configured window width: {}", appConfig.GetSection("Graphics:Window")->GetValue<int>("Width", 1280));

    return app.Run();
}

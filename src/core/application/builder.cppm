export module GPP.Core:Application.Builder;

import std;
import :Logger;
import :IO.File;
import :IO.AppPaths;
import :Application.Info;
import :Application.Config;
import :DI.Container;
import :Events;
import :Assets;

namespace GPP
{
    class Application;
    class EventDispatcher;
    class ServiceProvider;

    export template <typename T = Application> requires std::is_base_of_v<Application, T>
    class builder
    {
    public:
        builder()
        {
            Services.AddSingleton<Logger>([](ServiceProvider& _)
            {
                auto logger = std::make_shared<Logger>();
                logger->CreateConsoleLogger("GPP APP");
#ifdef NDEBUG
                logger->SetLevel(LogLevel::Info);
#else
                logger->SetLevel(LogLevel::Trace);
#endif
                return logger;
            });

            Services.AddSingleton<IFileSystem, FileSystem>([this](ServiceProvider& _)
            {
                return std::make_shared<FileSystem>(FS);
            });
            Services.Configure<AppInfo>("GPP:App",
                [defaults = m_AppInfoDefaults](const IConfigurationSection& section)
                {
                    return AppInfo::FromConfig(section, *defaults);
                });
            Services.AddSingleton<AppPaths>();
            Services.AddSingleton<EventDispatcher>();
            Services.Configure<AssetOptions>("GPP:Assets");
            Services.AddSingleton<AssetDirectories>();
        }

        virtual ~builder() = default;

        ServiceCollection Services{};
        ConfigurationBuilder Configuration{};
        FileSystem FS{};

        // Programmatic defaults; keys present in the GPP:App config section override them.
        builder& SetAppInfo(AppInfo info)
        {
            *m_AppInfoDefaults = std::move(info);
            return *this;
        }

        virtual std::shared_ptr<T> Build()
        {
            auto config = Configuration.Build();
            Services.ApplyConfiguration(*config);
            return std::make_shared<T>(Services.Build(), std::move(config));
        }

    private:
        std::shared_ptr<AppInfo> m_AppInfoDefaults = std::make_shared<AppInfo>();
    };


    export class CliApplicationBuilder : public builder<>
    {
    public:
        CliApplicationBuilder();
        std::shared_ptr<Application> Build() override;
    };

    export class WebApplicationBuilder : public builder<>
    {
    public:
        WebApplicationBuilder();
        std::shared_ptr<Application> Build() override;
    };

    export class App
    {
    public:
        static builder<> CreateBuilder()
        {
            return builder();
        }

        static CliApplicationBuilder CreateCliBuilder()
        {
            return CliApplicationBuilder();
        }

        static WebApplicationBuilder CreateWebBuilder()
        {
            return WebApplicationBuilder();
        }
    };
}

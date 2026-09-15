export module GPP.Core:Application.Builder;

import std;
import :Logger;
import :IO.File;
import :DI.Container;
import :Events;

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
            Services.AddSingleton<EventDispatcher>();
        }

        virtual ~builder() = default;

        ServiceCollection Services{};
        ConfigurationBuilder Configuration{};
        FileSystem FS{};

        virtual std::shared_ptr<T> Build()
        {
            auto config = Configuration.Build();
            Services.ApplyConfiguration(*config);
            return std::make_shared<T>(Services.Build(), std::move(config));
        }
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

export module GPP.Core:Application;

import std;
import :DI;
import :Logger;

namespace GPP
{
    export class Application
    {
    public:
        using MainThreadTask = std::move_only_function<void()>;

        explicit Application(ServiceProvider&& provider);
        ~Application();

        static Application& Instance();

        int Run();
        void Stop();
        void ScheduleOnMainThread(MainThreadTask task);

        ServiceProvider& GetServiceProvider() noexcept;

    private:
        static Application* s_Instance;
        static void HandleSignal(int signal);

        ServiceProvider m_ServiceProvider;
        std::vector<std::shared_ptr<IHostedService>> m_HostedServices;
        std::queue<MainThreadTask> m_MainThreadQueue{};

        std::stop_source m_StopSource;
        std::condition_variable m_ConditionVariable;
        std::mutex m_Mutex;
        bool m_Running;
    };

    export struct ResumeOnApplicationAwaiter
    {
        Application* app{nullptr};
        bool await_ready() const noexcept;
        void await_suspend(std::coroutine_handle<> handle) const;
        static void await_resume() noexcept;
    };

    export ResumeOnApplicationAwaiter ResumeOn(Application& app);

    export class ApplicationBuilder
    {
    public:
        ApplicationBuilder();
        virtual ~ApplicationBuilder() = default;

        ServiceCollection Services{};

        virtual Application Build();
    };


    export class CliApplicationBuilder : public ApplicationBuilder
    {
    public:
        CliApplicationBuilder();
        Application Build() override;
    };

    export class GuiApplicationBuilder : public ApplicationBuilder
    {
    public:
        GuiApplicationBuilder();
        Application Build() override;
    };

    export class WebApplicationBuilder : public ApplicationBuilder
    {
    public:
        WebApplicationBuilder();
        Application Build() override;
    };

    export class App
    {
    public:
        static ApplicationBuilder CreateBuilder()
        {
            return ApplicationBuilder();
        }

        static CliApplicationBuilder CreateCliBuilder()
        {
            return CliApplicationBuilder();
        }

        static GuiApplicationBuilder CreateGuiBuilder()
        {
            return GuiApplicationBuilder();
        }

        static WebApplicationBuilder CreateWebBuilder()
        {
            return WebApplicationBuilder();
        }
    };
}

export module GPP.Core:Application;

import std;
import :DI;
import :Logger;
import :IO.File;
export import :Application.Config;


namespace GPP
{
    export class Application
    {
    public:
        using MainThreadTask = std::move_only_function<void()>;

        explicit Application(ServiceProvider&& provider, std::unique_ptr<IConfiguration> configuration);
        ~Application();

        static Application& Instance();

        int Run();
        void Stop();
        void ScheduleOnMainThread(MainThreadTask task);
        void ScheduleContinuousOnMainThread(MainThreadTask task);

        ServiceProvider& GetServiceProvider() noexcept;
        IConfiguration& GetConfiguration();

    private:
        static Application* s_Instance;
        static void HandleSignal(int signal);

        ServiceProvider m_ServiceProvider;
        std::unique_ptr<IConfiguration> m_Configuration;
        std::vector<std::shared_ptr<IHostedService>> m_HostedServices;
        std::vector<MainThreadTask> m_ContinuousTasks{};
        std::queue<MainThreadTask> m_MainThreadQueue{};

        std::stop_source m_StopSource;
        std::condition_variable m_ConditionVariable;
        std::mutex m_Mutex;
        bool m_Running;

        template <typename U> requires std::is_base_of_v<Application, U>
        friend class builder;
    };

    export struct ResumeOnApplicationAwaiter
    {
        Application* app{nullptr};
        bool await_ready() const noexcept;
        void await_suspend(std::coroutine_handle<> handle) const;
        static void await_resume() noexcept;
    };

    export ResumeOnApplicationAwaiter ResumeOn(Application& app);


}

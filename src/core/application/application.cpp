module;

#include <csignal>

module GPP.Core;

import :Application;

namespace GPP
{
    Application* Application::s_Instance = nullptr;

    void Application::HandleSignal(int signal) {
        switch (signal) {
            case SIGINT:
            case SIGTERM:
                if (s_Instance) {
                    std::cout << std::endl;
                    s_Instance->Stop();
                }
                break;
            default:
                break;
        }
    }

    Application::Application(ServiceProvider&& provider) : m_ServiceProvider(std::move(provider)),
                                                           m_Running(false)
    {
        m_HostedServices = m_ServiceProvider.GetHostedServices();
        s_Instance = this;
        std::signal(SIGINT, HandleSignal);
        std::signal(SIGTERM, HandleSignal);
    }

    Application& Application::Instance()
    {
        if (!s_Instance)
        {
            throw std::runtime_error("No active Application instance has been created");
        }

        return *s_Instance;
    }

    Application::~Application()
    {
        if (s_Instance == this)
        {
            s_Instance = nullptr;
        }

        if (m_Running)
        {
            Stop();
        }
    }

    void Application::ScheduleOnMainThread(MainThreadTask task)
    {
        if (!task)
        {
            return;
        }

        {
            std::scoped_lock lock(m_Mutex);
            m_MainThreadQueue.push(std::move(task));
        }

        m_ConditionVariable.notify_one();
    }

    int Application::Run()
    {
        {
            std::scoped_lock lock(m_Mutex);
            if (m_Running) return 0;
            m_Running = true;
        }

        const auto logger = m_ServiceProvider.GetRequiredService<Logger>();
        logger->Info("Starting application...");

        for (const auto service : m_HostedServices)
        {
            service->StartAsync(m_StopSource.get_token());
        }

        logger->Info("Application running");

        while (m_Running)
        {
            std::queue<MainThreadTask> pendingTasks;

            {
                std::unique_lock lock(m_Mutex);
                m_ConditionVariable.wait(lock, [this] { return !m_Running || !m_MainThreadQueue.empty(); });
                if (!m_Running)
                {
                    break;
                }

                while (!m_MainThreadQueue.empty())
                {
                    pendingTasks.push(std::move(m_MainThreadQueue.front()));
                    m_MainThreadQueue.pop();
                }
            }

            while (!pendingTasks.empty())
            {
                auto task = std::move(pendingTasks.front());
                pendingTasks.pop();

                if (task)
                {
                    task();
                }
            }
        }

        logger->Info("Shutdown signal received! Stopping application...");

        m_StopSource.request_stop();
        for (const auto service : m_HostedServices)
        {
            service->StopAsync();
        }

        logger->Info("Application terminated successfully");
        return 0;
    }

    void Application::Stop()
    {
        {
            std::scoped_lock lock(m_Mutex);
            if (!m_Running) return;
            m_Running = false;
        }
        m_ConditionVariable.notify_all();
    }

    ServiceProvider& Application::GetServiceProvider() noexcept
    {
        return m_ServiceProvider;
    }

    bool ResumeOnApplicationAwaiter::await_ready() const noexcept
    {
        return false;
    }

    void ResumeOnApplicationAwaiter::await_suspend(std::coroutine_handle<> handle) const
    {
        if (!app)
        {
            throw std::runtime_error("Application instance is not available");
        }

        app->ScheduleOnMainThread([handle]
        {
            handle.resume();
        });
    }

    void ResumeOnApplicationAwaiter::await_resume() noexcept
    {
    }

    ResumeOnApplicationAwaiter ResumeOn(Application& app)
    {
        return ResumeOnApplicationAwaiter{ .app = &app };
    }

    ApplicationBuilder::ApplicationBuilder()
    {
        Services.AddSingleton<Logger>([](ServiceProvider& _)
        {
            auto logger = std::make_shared<Logger>();
            logger->CreateConsoleLogger("GPP APP");
            logger->SetLevel(LogLevel::Info);
            return logger;
        });
    }

    Application ApplicationBuilder::Build()
    {
        return Application(Services.Build());
    }


    CliApplicationBuilder::CliApplicationBuilder()
    {
    }

    Application CliApplicationBuilder::Build()
    {
        return ApplicationBuilder::Build();
    }

    GuiApplicationBuilder::GuiApplicationBuilder()
    {
    }

    Application GuiApplicationBuilder::Build()
    {
        return ApplicationBuilder::Build();
    }

    WebApplicationBuilder::WebApplicationBuilder()
    {
    }

    Application WebApplicationBuilder::Build()
    {
        return ApplicationBuilder::Build();
    }
}

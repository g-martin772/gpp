module;

#include <csignal>

module GPP.Core;

import :Application;
import :IO.File;

namespace GPP
{
    Application* Application::s_Instance = nullptr;

    void Application::HandleSignal(int signal)
    {
        switch (signal)
        {
        case SIGINT:
        case SIGTERM:
            if (s_Instance)
            {
                std::cout << std::endl;
                s_Instance->Stop();
            }
            break;
        default:
            break;
        }
    }

    Application::Application(ServiceProvider&& provider, std::unique_ptr<IConfiguration> configuration) :
        m_ServiceProvider(std::move(provider)),
        m_Configuration(std::move(configuration)),
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

    void Application::ScheduleContinuousOnMainThread(MainThreadTask task)
    {
        if (!task)
        {
            return;
        }

        {
            std::scoped_lock lock(m_Mutex);
            m_ContinuousTasks.push_back(std::move(task));
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

        std::vector<Task<void>> startTasks;
        for (auto service : m_HostedServices)
        {
            startTasks.push_back(service->StartAsync(m_StopSource.get_token()));
        }
        WhenAll(std::move(startTasks)).get();

        logger->Info("Application running");

        while (m_Running)
        {
            std::queue<MainThreadTask> pendingTasks;

            {
                std::unique_lock lock(m_Mutex);
                if (m_ContinuousTasks.empty())
                {
                    m_ConditionVariable.wait(lock, [this]
                    {
                        return !m_Running || !m_MainThreadQueue.empty() || !m_ContinuousTasks.
                            empty();
                    });
                }
                else
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }

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

            for (auto& task : m_ContinuousTasks)
            {
                task();
            }
        }

        logger->Info("Shutdown signal received! Stopping application...");

        m_StopSource.request_stop();
        std::vector<Task<void>> stopTasks;
        for (auto service : m_HostedServices)
        {
            stopTasks.push_back(service->StopAsync());
        }
        WhenAll(std::move(stopTasks)).get();

        logger->Info("Application terminated successfully");
        return 0;
    }

    void Application::Stop()
    {
        m_Running = false;
        m_ConditionVariable.notify_all();
    }

    ServiceProvider& Application::GetServiceProvider() noexcept
    {
        return m_ServiceProvider;
    }

    IConfiguration& Application::GetConfiguration()
    {
        return *m_Configuration;
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
        return ResumeOnApplicationAwaiter{.app = &app};
    }

    ApplicationBuilder::ApplicationBuilder()
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

    Application ApplicationBuilder::Build()
    {
        auto config = Configuration.Build();
        Services.ApplyConfiguration(*config);
        return Application(Services.Build(), std::move(config));
    }


    CliApplicationBuilder::CliApplicationBuilder()
    {
    }

    Application CliApplicationBuilder::Build()
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

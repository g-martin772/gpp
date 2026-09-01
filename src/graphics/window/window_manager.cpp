module;
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
module GPP.Graphics;

import :Windowing.WindowManager;

namespace GPP
{
    WindowManager::WindowManager(std::shared_ptr<Logger> logger)
        : m_Logger(logger)
    {
    }

    WindowManager::~WindowManager()
    {
    }

    Task<void> WindowManager::StartAsync(std::stop_token stopToken)
    {
        Application::Instance().ScheduleOnMainThread([this]
        {
            if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
            {
                throw std::runtime_error(std::string("Failed to initialize SDL3: ") + SDL_GetError());
            }
            m_IsInitialized = true;
            m_Logger->Info("SDL initialized successfully");
            m_ReadyPromise.set_value();
        });

        Application::Instance().ScheduleContinuousOnMainThread([this]
        {
            if (m_ShouldQuit && !m_Quitting)
            {
                m_Quitting = true;
                Application::Instance().Stop();
            }

            if (!m_IsInitialized || m_ShouldQuit)
            {
                return;
            }

            PollEvents();
        });
        co_return;
    }

    Task<void> WindowManager::StopAsync()
    {
        Application::Instance().ScheduleOnMainThread([this]
        {
            m_Windows.clear();
            SDL_Quit();
        });
        co_return;
    }

    Task<void> WindowManager::AwaitReady()
    {
        co_await m_ReadyPromise.get_future();
        co_return;
    }

    Task<std::shared_ptr<Window>> WindowManager::CreateWindow(const WindowOptions& options)
    {
        co_await ResumeOn(Application::Instance());
        SDL_WindowFlags flags = SDL_WINDOW_VULKAN;

        if (options.Resizable)
        {
            flags |= SDL_WINDOW_RESIZABLE;
        }
        if (options.Fullscreen)
        {
            flags |= SDL_WINDOW_FULLSCREEN;
        }

        SDL_Window* sdlWindow = SDL_CreateWindow(
            options.Title.c_str(),
            options.Width,
            options.Height,
            flags
        );

        if (!sdlWindow)
        {
            throw std::runtime_error(std::string("Failed to create SDL3 window: ") + SDL_GetError());
        }

        auto wrappedWindow = std::make_shared<Window>(sdlWindow);
        m_Windows[wrappedWindow->GetID()] = wrappedWindow;

        co_return wrappedWindow;
    }

    std::shared_ptr<Window> WindowManager::GetWindow(SDL_WindowID id) const
    {
        auto it = m_Windows.find(id);
        if (it != m_Windows.end())
        {
            return it->second;
        }
        return nullptr;
    }

    void WindowManager::TriggerWindowClose(SDL_WindowID id)
    {
        m_Windows.erase(id);
        if (m_Windows.empty())
        {
            m_ShouldQuit = true;
        }
    }

    void WindowManager::PollEvents()
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            switch (event.type)
            {
            case SDL_EVENT_QUIT:
                {
                    m_ShouldQuit = true;
                    break;
                }
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                {
                    TriggerWindowClose(event.window.windowID);
                    break;
                }
            default:
                break;
            }
        }
    }

    bool WindowManager::ShouldQuit() const noexcept
    {
        return m_ShouldQuit;
    }

    Task<void> WindowManager::ShowMessageBox(std::string_view title, std::string_view message, bool isError)
    {
        co_await ResumeOn(Application::Instance());
        SDL_MessageBoxFlags flags = isError ? SDL_MESSAGEBOX_ERROR : SDL_MESSAGEBOX_INFORMATION;
        SDL_ShowSimpleMessageBox(flags, title.data(), message.data(), nullptr);
        co_return;
    }
}

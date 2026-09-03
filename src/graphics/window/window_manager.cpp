module;
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
module GPP.Graphics;

import :Windowing.WindowManager;

namespace GPP
{
    WindowManager::WindowManager(std::shared_ptr<Logger> logger, std::shared_ptr<EventDispatcher> dispatcher)
        : m_Logger(std::move(logger)), m_Dispatcher(std::move(dispatcher))
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
            if (!SDL_Vulkan_LoadLibrary("/usr/lib/libvulkan.so.1"))
            {
                throw std::runtime_error(std::string("SDL3 failed to bind system Vulkan: ") + SDL_GetError());
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
        co_await m_SharedFuture;
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

        auto wrappedWindow = std::shared_ptr<Window>(new Window(sdlWindow));
        m_Windows[wrappedWindow->GetID()] = wrappedWindow;

        co_return wrappedWindow;
    }

    std::shared_ptr<Window> WindowManager::GetWindow(WindowId id) const
    {
        auto it = m_Windows.find(id);
        if (it != m_Windows.end())
        {
            return it->second;
        }
        return nullptr;
    }

    void WindowManager::TriggerWindowClose(WindowId id)
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
                    m_Dispatcher->Publish(QuitEvent{});
                    break;
                }
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                {
                    const auto windowId = static_cast<WindowId>(event.window.windowID);
                    TriggerWindowClose(windowId);
                    m_Dispatcher->Publish(WindowCloseRequestedEvent{windowId});
                    break;
                }
            case SDL_EVENT_WINDOW_RESIZED:
                {
                    m_Dispatcher->Publish(WindowResizedEvent{
                        event.window.windowID, event.window.data1, event.window.data2});
                    break;
                }
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP:
                {
                    m_Dispatcher->Publish(KeyEvent{
                        event.key.windowID, static_cast<KeyCode>(event.key.key), static_cast<ScanCode>(event.key.scancode),
                        event.type == SDL_EVENT_KEY_DOWN, event.key.repeat});
                    break;
                }
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP:
                {
                    m_Dispatcher->Publish(MouseButtonEvent{
                        event.button.windowID, static_cast<MouseButton>(event.button.button),
                        event.type == SDL_EVENT_MOUSE_BUTTON_DOWN});
                    break;
                }
            case SDL_EVENT_MOUSE_MOTION:
                {
                    m_Dispatcher->Publish(MouseMotionEvent{
                        event.motion.windowID, event.motion.x, event.motion.y,
                        event.motion.xrel, event.motion.yrel});
                    break;
                }
            case SDL_EVENT_MOUSE_WHEEL:
                {
                    m_Dispatcher->Publish(MouseWheelEvent{
                        event.wheel.windowID, event.wheel.x, event.wheel.y});
                    break;
                }
            case SDL_EVENT_TEXT_INPUT:
                {
                    m_Dispatcher->Publish(TextInputEvent{
                        event.text.windowID, event.text.text ? event.text.text : ""});
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

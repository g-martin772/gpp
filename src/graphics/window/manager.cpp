module;
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
module GPP.Graphics;

import :Windowing.WindowManager;

namespace GPP
{
    WindowManager::WindowManager(std::shared_ptr<Logger> logger,
                                 std::shared_ptr<EventDispatcher> dispatcher,
                                 std::shared_ptr<WindowDefinitions> definitions,
                                 std::shared_ptr<WindowOptions> options)
        : m_Logger(std::move(logger)),
          m_Dispatcher(std::move(dispatcher)),
          m_Definitions(std::move(definitions)),
          m_Options(std::move(options))
    {
    }

    WindowManager::~WindowManager()
    {
    }

    Task<void> WindowManager::StartAsync(std::stop_token stopToken)
    {
        Application::Instance().ScheduleOnMainThread([this]
        {
            if (IsHeadless())
            {
                m_IsInitialized = true;
                m_Logger->Info("Windowing disabled (headless mode).");
                m_ReadyPromise.set_value();
                return;
            }
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
            std::scoped_lock lock(m_WindowsMutex);
            m_Windows.clear();
            m_WindowNames.clear();
            if (!IsHeadless())
            {
                SDL_Quit();
            }
        });
        co_return;
    }

    Task<void> WindowManager::AwaitReady()
    {
        co_await m_SharedFuture;
        co_return;
    }

    Task<std::shared_ptr<Window>> WindowManager::CreateWindow(const WindowOptions& options,
                                                              std::string name)
    {
        co_await ResumeOn(Application::Instance());
        if (IsHeadless())
        {
            throw std::runtime_error("Cannot create a window while running headless.");
        }
        if (name.empty())
        {
            name = std::format("window-{}", m_Windows.size());
        }
        {
            std::scoped_lock lock(m_WindowsMutex);
            if (m_WindowNames.contains(name))
            {
                throw std::runtime_error(std::format("A window named '{}' already exists.", name));
            }
        }
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
        auto id = wrappedWindow->GetID();
        {
            std::scoped_lock lock(m_WindowsMutex);
            m_Windows[id] = wrappedWindow;
            m_WindowNames.emplace(std::move(name), id);
        }
        m_Logger->Debug("Created window with ID {}: {}x{}, Title: '{}'", id, options.Width, options.Height, options.Title);

        co_return wrappedWindow;
    }

    std::shared_ptr<Window> WindowManager::GetWindow(WindowId id) const
    {
        std::scoped_lock lock(m_WindowsMutex);
        auto it = m_Windows.find(id);
        if (it != m_Windows.end())
        {
            return it->second;
        }
        return nullptr;
    }

    std::shared_ptr<Window> WindowManager::GetWindow(std::string_view name) const
    {
        std::scoped_lock lock(m_WindowsMutex);
        const auto nameIt = m_WindowNames.find(std::string(name));
        return nameIt == m_WindowNames.end() ? nullptr : m_Windows.at(nameIt->second);
    }

    std::shared_ptr<Window> WindowManager::GetMainWindow() const
    {
        return GetWindow(MainWindowName);
    }

    std::optional<WindowId> WindowManager::GetWindowId(std::string_view name) const
    {
        std::scoped_lock lock(m_WindowsMutex);
        const auto it = m_WindowNames.find(std::string(name));
        return it == m_WindowNames.end() ? std::nullopt : std::optional{it->second};
    }

    std::string WindowManager::GetWindowName(WindowId id) const
    {
        std::scoped_lock lock(m_WindowsMutex);
        for (const auto& [name, windowId] : m_WindowNames)
        {
            if (windowId == id)
            {
                return name;
            }
        }
        return {};
    }

    std::vector<std::shared_ptr<Window>> WindowManager::GetWindows() const
    {
        std::scoped_lock lock(m_WindowsMutex);
        std::vector<std::shared_ptr<Window>> windows;
        windows.reserve(m_Windows.size());
        for (const auto& [id, window] : m_Windows)
            windows.push_back(window);
        return windows;
    }

    void WindowManager::TriggerWindowClose(WindowId id)
    {
        std::scoped_lock lock(m_WindowsMutex);
        if (const auto it = m_Windows.find(id); it != m_Windows.end())
        {
            // native window is destroyed by render thread
            m_Windows.erase(it);
        }
        for (auto it = m_WindowNames.begin(); it != m_WindowNames.end();)
        {
            it = it->second == id ? m_WindowNames.erase(it) : std::next(it);
        }
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
            ImGuiRawEvent imguiEvent{
                .Type = event.type
            };
            switch (event.type)
            {
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP:
                imguiEvent.Window = event.key.windowID;
                imguiEvent.Key = static_cast<KeyCode>(event.key.key);
                imguiEvent.Scan = static_cast<ScanCode>(event.key.scancode);
                imguiEvent.Modifiers = event.key.mod;
                imguiEvent.Repeat = event.key.repeat;
                m_Dispatcher->Publish(std::move(imguiEvent));
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP:
                imguiEvent.Window = event.button.windowID;
                imguiEvent.Button = static_cast<MouseButton>(event.button.button);
                m_Dispatcher->Publish(std::move(imguiEvent));
                break;
            case SDL_EVENT_MOUSE_MOTION:
                imguiEvent.Window = event.motion.windowID;
                imguiEvent.X = event.motion.x;
                imguiEvent.Y = event.motion.y;
                imguiEvent.DeltaX = event.motion.xrel;
                imguiEvent.DeltaY = event.motion.yrel;
                m_Dispatcher->Publish(std::move(imguiEvent));
                break;
            case SDL_EVENT_MOUSE_WHEEL:
                imguiEvent.Window = event.wheel.windowID;
                imguiEvent.X = event.wheel.x;
                imguiEvent.Y = event.wheel.y;
                m_Dispatcher->Publish(std::move(imguiEvent));
                break;
            case SDL_EVENT_TEXT_INPUT:
                imguiEvent.Window = event.text.windowID;
                imguiEvent.Text = event.text.text ? event.text.text : "";
                m_Dispatcher->Publish(std::move(imguiEvent));
                break;
            case SDL_EVENT_WINDOW_MOUSE_ENTER:
            case SDL_EVENT_WINDOW_MOUSE_LEAVE:
            case SDL_EVENT_WINDOW_FOCUS_GAINED:
            case SDL_EVENT_WINDOW_FOCUS_LOST:
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            case SDL_EVENT_WINDOW_MOVED:
            case SDL_EVENT_WINDOW_RESIZED:
                imguiEvent.Window = event.window.windowID;
                imguiEvent.X = static_cast<float>(event.window.data1);
                imguiEvent.Y = static_cast<float>(event.window.data2);
                m_Dispatcher->Publish(std::move(imguiEvent));
                break;
            case SDL_EVENT_DISPLAY_ORIENTATION:
            case SDL_EVENT_DISPLAY_ADDED:
            case SDL_EVENT_DISPLAY_REMOVED:
            case SDL_EVENT_DISPLAY_MOVED:
            case SDL_EVENT_DISPLAY_CONTENT_SCALE_CHANGED:
                m_Dispatcher->Publish(std::move(imguiEvent));
                break;
            default:
                break;
            }
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
                    const auto windowId = event.window.windowID;
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

    bool WindowManager::IsHeadless() const noexcept
    {
        return (m_Options && m_Options->Headless) ||
            (m_Definitions && std::any_of(
                m_Definitions->Items.begin(), m_Definitions->Items.end(),
                [](const WindowDefinition& definition) { return definition.Options.Headless; }));
    }

    Task<void> WindowManager::ShowMessageBox(std::string_view title, std::string_view message, bool isError)
    {
        co_await ResumeOn(Application::Instance());
        SDL_MessageBoxFlags flags = isError ? SDL_MESSAGEBOX_ERROR : SDL_MESSAGEBOX_INFORMATION;
        SDL_ShowSimpleMessageBox(flags, title.data(), message.data(), nullptr);
        co_return;
    }
}

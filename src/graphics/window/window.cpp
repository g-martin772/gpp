module;
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

module GPP.Graphics;

import :Windowing.Window;

namespace GPP
{
    Window::Window(SDL_Window* window) : m_Window(window)
    {
        if (!m_Window)
        {
            throw std::runtime_error("Cannot wrap a null SDL_Window pointer.");
        }
    }

    Window::~Window()
    {
        if (m_Window)
        {
            if (m_Window)
            {
                SDL_DestroyWindow(m_Window); // should always on main thread anyway
            }
            m_Window = nullptr;
        }
    }

    Window::Window(Window&& other) noexcept : m_Window(other.m_Window)
    {
        other.m_Window = nullptr;
    }

    Window& Window::operator=(Window&& other) noexcept
    {
        if (this != &other)
        {
            if (m_Window)
            {
                DestroyWindow().get();
            }
            m_Window = other.m_Window;
            other.m_Window = nullptr;
        }
        return *this;
    }

    SDL_Window* Window::GetNativeHandle() const noexcept
    {
        return m_Window;
    }

    SDL_WindowID Window::GetID() const noexcept
    {
        return SDL_GetWindowID(m_Window);
    }

    Task<void> Window::GetSize(int* width, int* height) const noexcept
    {
        co_await ResumeOn(Application::Instance());
        SDL_GetWindowSize(m_Window, width, height);
        co_return;
    }

    Task<void> Window::SetSize(int width, int height) noexcept
    {
        co_await ResumeOn(Application::Instance());
        SDL_SetWindowSize(m_Window, width, height);
        co_return;
    }

    Task<std::string> Window::GetTitle() const noexcept
    {
        co_await ResumeOn(Application::Instance());
        co_return SDL_GetWindowTitle(m_Window);
    }

    Task<void> Window::SetTitle(const std::string& title) noexcept
    {
        co_await ResumeOn(Application::Instance());
        SDL_SetWindowTitle(m_Window, title.c_str());
        co_return;
    }

    Task<bool> Window::CreateVulkanSurface(VkInstance instance, VkSurfaceKHR* outSurface) const noexcept
    {
        co_await ResumeOn(Application::Instance());
        co_return SDL_Vulkan_CreateSurface(m_Window, instance, nullptr, outSurface);
    }

    Task<void> Window::DestroyWindow()
    {
        co_await ResumeOn(Application::Instance());
        SDL_DestroyWindow(m_Window);
        co_return;
    }
}

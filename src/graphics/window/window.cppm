module;
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

export module GPP.Graphics:Windowing.Window;

import std;
import GPP.Core;
import :Windowing.Events;

namespace GPP
{
    export class WindowManager;

    export struct WindowOptions : public IService
    {
        int Width;
        int Height;
        std::string Title;
        bool Resizable;
        bool Fullscreen;

        static WindowOptions FromConfig(const IConfigurationSection& config)
        {
            WindowOptions options;
            options.Width = config.GetValue<int>("Width", 1280);
            options.Height = config.GetValue<int>("Height", 720);
            options.Title = config.GetValue<std::string>("Title", "GPP Engine");
            options.Resizable = config.GetValue<bool>("Resizable", true);
            options.Fullscreen = config.GetValue<bool>("Fullscreen", false);
            return options;
        }
    };

    export class Window
    {
    public:
        ~Window();

        // prevent copy
        Window(const Window&) = delete;
        Window& operator=(const Window&) = delete;
        // allow move
        Window(Window&& other) noexcept;
        Window& operator=(Window&& other) noexcept;

        [[nodiscard]] WindowId GetID() const noexcept;
        [[nodiscard]] Task<void> DestroyWindow();
        [[nodiscard]] Task<void> GetSize(int* width, int* height) const noexcept;
        [[nodiscard]] Task<void> SetSize(int width, int height) noexcept;
        [[nodiscard]] Task<std::string> GetTitle() const noexcept;
        [[nodiscard]] Task<void> SetTitle(const std::string& title) noexcept;
        [[nodiscard]] Task<bool> CreateVulkanSurface(VkInstance instance, VkSurfaceKHR* outSurface) const noexcept;
    private:
        explicit Window(SDL_Window* window);
        friend class WindowManager;
        SDL_Window* m_Window{nullptr};
    };
}

module;
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

export module GPP.Graphics:Windowing.Window;

import std;
import vulkan;
import GPP.Core;
import :Windowing.Events;

namespace GPP
{
    export class WindowManager;

    export struct WindowOptions : public IService
    {
        int Width = 1280;
        int Height = 720;
        std::string Title = "GPP Engine";
        bool Resizable = true;
        bool Fullscreen = false;
        bool Headless = false;

        std::optional<std::vector<std::string>> ImGuiConfigFlags;
        std::optional<bool> ImGuiDockSpace;

        static WindowOptions FromConfig(const IConfigurationSection& config)
        {
            WindowOptions options;
            options.Width = config.GetValue<int>("Width", 1280);
            options.Height = config.GetValue<int>("Height", 720);
            options.Title = config.GetValue<std::string>("Title", "GPP Engine");
            options.Resizable = config.GetValue<bool>("Resizable", true);
            options.Fullscreen = config.GetValue<bool>("Fullscreen", false);
            options.Headless = config.GetValue<bool>("Headless", false);

            const auto imgui = config.GetSection("ImGui");
            std::vector<std::string> configFlags;
            const auto flagsSection = imgui->GetSection("ConfigFlags");
            for (std::size_t index = 0;; ++index)
            {
                std::string value;
                if (!flagsSection->TryGetValue(std::to_string(index), value))
                {
                    break;
                }
                if (!value.empty())
                {
                    configFlags.push_back(std::move(value));
                }
            }
            if (!configFlags.empty())
            {
                options.ImGuiConfigFlags = std::move(configFlags);
            }
            if (std::string dockSpace; imgui->TryGetValue("EnableDockSpace", dockSpace))
            {
                options.ImGuiDockSpace = (dockSpace == "true" || dockSpace == "1" ||
                                           dockSpace == "yes" || dockSpace == "on");
            }
            return options;
        }
    };

    export struct WindowDefinition
    {
        std::string Name;
        WindowOptions Options;
    };

    export struct WindowDefinitions : public IService
    {
        std::vector<WindowDefinition> Items;
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
        [[nodiscard]] void* GetNativeHandle() const noexcept;
        [[nodiscard]] Task<void> DestroyWindow();
        [[nodiscard]] Task<void> GetSize(int* width, int* height) const noexcept;
        [[nodiscard]] Task<void> SetSize(int width, int height) noexcept;
        [[nodiscard]] Task<std::string> GetTitle() const noexcept;
        [[nodiscard]] Task<void> SetTitle(const std::string& title) noexcept;
        [[nodiscard]] Task<bool> CreateVulkanSurface(VkInstance instance, vk::SurfaceKHR* outSurface) const noexcept;
    private:
        explicit Window(SDL_Window* window);
        void DestroyNativeWindow() noexcept;
        friend class WindowManager;
        SDL_Window* m_Window{nullptr};
    };
}

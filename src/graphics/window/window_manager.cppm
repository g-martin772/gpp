module;
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

export module GPP.Graphics:Windowing.WindowManager;

import std;
import GPP.Core;
import :Windowing.Window;

namespace GPP
{
    export class WindowManager : public IHostedService
    {
    public:
        using Dependencies = std::tuple<Logger>;
        WindowManager(std::shared_ptr<Logger> logger);
        ~WindowManager() override;

        Task<void> StartAsync(std::stop_token stopToken) override;
        Task<void> StopAsync() override;
        Task<void> AwaitReady();

        Task<std::shared_ptr<Window>> CreateWindow(const WindowOptions& options);
        std::shared_ptr<Window> GetWindow(SDL_WindowID id) const;

        void TriggerWindowClose(SDL_WindowID id);

        void PollEvents();
        bool ShouldQuit() const noexcept;

        Task<void> ShowMessageBox(std::string_view title, std::string_view message, bool isError = false);
    private:
        std::unordered_map<SDL_WindowID, std::shared_ptr<Window>> m_Windows;
        bool m_ShouldQuit{false}, m_Quitting{false}, m_IsInitialized{false};
        std::shared_ptr<Logger> m_Logger;
        std::promise<void> m_ReadyPromise;
    };
}

export module GPP.Graphics:Windowing.WindowManager;

import std;
import GPP.Core;
import :Windowing.Window;
import :Windowing.Events;

namespace GPP
{
    export class WindowManager : public IHostedService
    {
    public:
        static constexpr WindowId MainWindowId = 6;

        using Dependencies = std::tuple<Logger, EventDispatcher>;
        WindowManager(std::shared_ptr<Logger> logger, std::shared_ptr<EventDispatcher> dispatcher);
        ~WindowManager() override;

        Task<void> StartAsync(std::stop_token stopToken) override;
        Task<void> StopAsync() override;
        Task<void> AwaitReady();

        Task<std::shared_ptr<Window>> CreateWindow(const WindowOptions& options);
        std::shared_ptr<Window> GetWindow(WindowId id) const;
        std::vector<std::shared_ptr<Window>> GetWindows() const;

        void TriggerWindowClose(WindowId id);

        void PollEvents();
        bool ShouldQuit() const noexcept;

        Task<void> ShowMessageBox(std::string_view title, std::string_view message, bool isError = false);
    private:
        mutable std::mutex m_WindowsMutex;
        std::unordered_map<WindowId, std::shared_ptr<Window>> m_Windows;
        bool m_ShouldQuit{false}, m_Quitting{false}, m_IsInitialized{false};
        std::shared_ptr<Logger> m_Logger;
        std::shared_ptr<EventDispatcher> m_Dispatcher;
        std::promise<void> m_ReadyPromise;
        std::shared_future<void> m_SharedFuture{ m_ReadyPromise.get_future().share() };
    };
}

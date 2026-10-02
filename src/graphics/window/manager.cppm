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
        static constexpr std::string_view MainWindowName = "main";

        using Dependencies = std::tuple<Logger, EventDispatcher, WindowDefinitions, WindowOptions>;
        WindowManager(std::shared_ptr<Logger> logger,
                      std::shared_ptr<EventDispatcher> dispatcher,
                      std::shared_ptr<WindowDefinitions> definitions,
                      std::shared_ptr<WindowOptions> options);
        ~WindowManager() override;

        Task<void> StartAsync(std::stop_token stopToken) override;
        Task<void> StopAsync() override;
        Task<void> AwaitReady();

        Task<std::shared_ptr<Window>> CreateWindow(const WindowOptions& options,
                                                   std::string name = {});
        std::shared_ptr<Window> GetWindow(WindowId id) const;
        std::shared_ptr<Window> GetWindow(std::string_view name) const;
        std::shared_ptr<Window> GetMainWindow() const;
        std::optional<WindowId> GetWindowId(std::string_view name) const;
        std::string GetWindowName(WindowId id) const;
        std::vector<std::shared_ptr<Window>> GetWindows() const;
        bool IsHeadless() const noexcept;

        void TriggerWindowClose(WindowId id);

        void PollEvents();
        bool ShouldQuit() const noexcept;

        Task<void> ShowMessageBox(std::string_view title, std::string_view message, bool isError = false);
    private:
        mutable std::mutex m_WindowsMutex;
        std::unordered_map<WindowId, std::shared_ptr<Window>> m_Windows;
        std::unordered_map<std::string, WindowId> m_WindowNames;
        bool m_ShouldQuit{false}, m_Quitting{false}, m_IsInitialized{false};
        std::shared_ptr<Logger> m_Logger;
        std::shared_ptr<EventDispatcher> m_Dispatcher;
        std::shared_ptr<WindowDefinitions> m_Definitions;
        std::shared_ptr<WindowOptions> m_Options;
        std::promise<void> m_ReadyPromise;
        std::shared_future<void> m_SharedFuture{ m_ReadyPromise.get_future().share() };
    };
}

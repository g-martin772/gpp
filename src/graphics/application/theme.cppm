export module GPP.Graphics:Application.Theme;

import std;
import GPP.Core;
import imgui;

namespace GPP
{
    export struct Theme : public IService
    {
        explicit Theme(const std::shared_ptr<Logger>& logger)
            : m_Logger(logger)
        {
        }

        virtual ~Theme() override = default;

        virtual void Apply(ImGuiStyle& style, ImGuiIO& io) = 0;

    protected:
        std::shared_ptr<Logger> m_Logger;
    };

    export class ThemeProxy final : public IService
    {
    public:
        using Dependencies = std::tuple<Logger>;

        explicit ThemeProxy(const std::shared_ptr<Logger>& logger)
            : m_Logger(logger)
        {
        }

        Theme* SwapActive(Theme* theme) noexcept
        {
            std::scoped_lock lock(m_Mutex);
            return std::exchange(m_Active, theme);
        }

        [[nodiscard]] Theme* GetActive() const noexcept
        {
            std::scoped_lock lock(m_Mutex);
            return m_Active;
        }

    private:
        std::shared_ptr<Logger> m_Logger;
        mutable std::mutex m_Mutex;
        Theme* m_Active = nullptr;
    };

    export struct ThemeChangedEvent
    {
    };
}

export module GPP.Graphics:UI.Preferences;

import std;
import GPP.Core;
import :RenderConfig;

namespace GPP
{
    export struct UiPreferencesChangedEvent
    {
    };

    export class UiPreferences : public IService
    {
    public:
        using Dependencies = std::tuple<FontOptions, EventDispatcher>;

        UiPreferences(std::shared_ptr<FontOptions> options, std::shared_ptr<EventDispatcher> dispatcher)
            : m_Dispatcher(std::move(dispatcher))
        {
            std::scoped_lock lock(m_Mutex);
            m_FontName = options->DefaultFont;
            m_FontSize = options->DefaultFontSize;
            m_UiScale = options->UiScale;
        }

        [[nodiscard]] std::string GetFontName() const
        {
            std::scoped_lock lock(m_Mutex);
            return m_FontName;
        }

        [[nodiscard]] float GetFontSize() const
        {
            std::scoped_lock lock(m_Mutex);
            return m_FontSize;
        }

        [[nodiscard]] float GetUiScale() const
        {
            std::scoped_lock lock(m_Mutex);
            return m_UiScale;
        }

        void SetFont(std::string name, float size)
        {
            {
                std::scoped_lock lock(m_Mutex);
                m_FontName = std::move(name);
                m_FontSize = size;
            }
            m_Dispatcher->Publish(UiPreferencesChangedEvent{});
        }

        void SetUiScale(float scale)
        {
            {
                std::scoped_lock lock(m_Mutex);
                m_UiScale = scale;
            }
            m_Dispatcher->Publish(UiPreferencesChangedEvent{});
        }

    private:
        std::shared_ptr<EventDispatcher> m_Dispatcher;
        mutable std::mutex m_Mutex;
        std::string m_FontName;
        float m_FontSize = 16.0f;
        float m_UiScale = 1.0f;
    };
}

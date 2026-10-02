import GPP;
import std;

#include <gpp/hot_reload_export.h>

using namespace GPP;

namespace
{
    struct DemoHotReloadLayer final : public HotReloadableLayer
    {
        using Dependencies = std::tuple<Logger>;

        explicit DemoHotReloadLayer(const std::shared_ptr<Logger>& logger)
            : HotReloadableLayer(logger)
        {
        }

        void OnAttach() override
        {
            m_Logger->Info("DemoHotReloadLayer attached");
        }

        void OnDetach() override
        {
            m_Logger->Info("DemoHotReloadLayer detached");
        }

        void OnUiRender() override
        {
            ImGui::Begin("Hot-Reloadable Layer");
            ImGui::TextUnformatted("Plugin World");
            ImGui::Text("Build marker: %d", 5);
            ImGui::End();
        }
    };
}

GPP_DEFINE_HOT_RELOAD_LAYER(DemoHotReloadLayer)

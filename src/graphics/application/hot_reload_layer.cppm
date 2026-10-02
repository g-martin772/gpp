export module GPP.Graphics:Application.HotReloadLayer;

import std;
import GPP.Core;
import :Application.Layer;

namespace GPP
{
    export struct HotReloadableLayer : public GuiLayer
    {
        using GuiLayer::GuiLayer;
        ~HotReloadableLayer() override = default;
    };

    export class HotReloadLayerProxy final : public GuiLayer
    {
    public:
        using Dependencies = std::tuple<Logger>;
        using GuiLayer::GuiLayer;

        void OnAttach() override;
        void OnDetach() override;
        void OnUpdate(float deltaTime) override;
        void OnRender() override;
        void OnUiRender() override;
        void OnEvent() override;

        HotReloadableLayer* SwapActive(HotReloadableLayer* newLayer) noexcept;
        [[nodiscard]] HotReloadableLayer* GetActive() const noexcept;
    private:
        mutable std::mutex m_Mutex;
        HotReloadableLayer* m_Active = nullptr;
        bool m_StackAttached = false;
    };
}

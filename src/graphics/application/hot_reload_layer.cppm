export module GPP.Graphics:Application.HotReloadLayer;

import std;
import GPP.Core;
import :RenderGraph;
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
        void OnRenderGraph(RenderGraph& graph) override;
        void OnUiRender() override;
        void OnEvent() override;
        void OnSafePoint() override;

        HotReloadableLayer* SwapActive(HotReloadableLayer* newLayer) noexcept;
        void QueueSwap(HotReloadableLayer* newLayer, RetiredHotReloadInstance retired = {});

        [[nodiscard]] HotReloadableLayer* GetActive() const noexcept;
    private:
        struct PendingSwap
        {
            HotReloadableLayer* Layer = nullptr;
            RetiredHotReloadInstance Retired;
        };

        mutable std::shared_mutex m_CallMutex;
        HotReloadableLayer* m_Active = nullptr;
        bool m_StackAttached = false;

        std::mutex m_PendingMutex;
        std::vector<PendingSwap> m_Pending;
        std::atomic<bool> m_HasPending{false};
    };
}

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

        template <typename Fn>
        bool Guarded(const char* hook, Fn&& fn) noexcept
        {
            try
            {
                fn();
                return true;
            }
            catch (const std::exception& exception)
            {
                ReportFailure(hook, exception.what());
            }
            catch (...)
            {
                ReportFailure(hook, "unknown exception");
            }
            return false;
        }

        void ReportFailure(const char* hook, const char* message) noexcept;

        mutable std::shared_mutex m_CallMutex;
        std::atomic<bool> m_ReportedFailure{false};
        HotReloadableLayer* m_Active = nullptr;
        bool m_StackAttached = false;

        std::mutex m_PendingMutex;
        std::vector<PendingSwap> m_Pending;
        std::atomic<bool> m_HasPending{false};
    };
}

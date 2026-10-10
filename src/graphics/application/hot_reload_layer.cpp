module GPP.Graphics;

import :Application.HotReloadLayer;
import :RenderGraph;
import std;

namespace GPP
{
    void HotReloadLayerProxy::OnAttach()
    {
        m_StackAttached = true;
        std::shared_lock lock(m_CallMutex);
        if (m_Active)
        {
            Guarded("OnAttach", [&] { m_Active->OnAttach(); });
        }
    }

    void HotReloadLayerProxy::OnDetach()
    {
        {
            std::shared_lock lock(m_CallMutex);
            if (m_Active)
            {
                Guarded("OnDetach", [&] { m_Active->OnDetach(); });
            }
        }
        m_StackAttached = false;
    }

    void HotReloadLayerProxy::OnUpdate(float deltaTime)
    {
        std::shared_lock lock(m_CallMutex);
        if (m_Active)
        {
            Guarded("OnUpdate", [&] { m_Active->OnUpdate(deltaTime); });
        }
    }

    void HotReloadLayerProxy::OnRender()
    {
        std::shared_lock lock(m_CallMutex);
        if (m_Active)
        {
            Guarded("OnRender", [&] { m_Active->OnRender(); });
        }
    }

    void HotReloadLayerProxy::OnRenderGraph(RenderGraph& graph)
    {
        std::shared_lock lock(m_CallMutex);
        if (m_Active)
        {
            Guarded("OnRenderGraph", [&] { m_Active->OnRenderGraph(graph); });
        }
    }

    void HotReloadLayerProxy::OnUiRender()
    {
        std::shared_lock lock(m_CallMutex);
        if (m_Active)
        {
            Guarded("OnUiRender", [&] { m_Active->OnUiRender(); });
        }
    }

    void HotReloadLayerProxy::OnEvent()
    {
        std::shared_lock lock(m_CallMutex);
        if (m_Active)
        {
            Guarded("OnEvent", [&] { m_Active->OnEvent(); });
        }
    }

    void HotReloadLayerProxy::OnSafePoint()
    {
        if (!m_HasPending.load(std::memory_order_acquire))
        {
            return;
        }

        std::vector<PendingSwap> pending;
        {
            std::scoped_lock lock(m_PendingMutex);
            pending.swap(m_Pending);
            m_HasPending.store(false, std::memory_order_release);
        }
        if (pending.empty())
        {
            return;
        }

        HotReloadableLayer* const target = pending.back().Layer;

        HotReloadableLayer* previous = nullptr;
        {
            std::unique_lock lock(m_CallMutex);
            previous = std::exchange(m_Active, nullptr);
        }

        if (m_StackAttached && previous)
        {
            Guarded("OnDetach", [&] { previous->OnDetach(); });
        }
        bool attached = true;
        if (target)
        {
            target->SetLayerTarget(GetLayerTarget());
            if (m_StackAttached)
            {
                attached = Guarded("OnAttach", [&] { target->OnAttach(); });
            }
        }

        {
            std::unique_lock lock(m_CallMutex);
            m_Active = attached ? target : nullptr;
        }
        m_ReportedFailure.store(false);
    }

    void HotReloadLayerProxy::ReportFailure(const char* hook, const char* message) noexcept
    {
        if (m_ReportedFailure.exchange(true) || !m_Logger)
        {
            return;
        }
        m_Logger->Error("Hot-reloaded layer threw in {}: {} (further errors suppressed until the next reload)",
                        hook, message);
    }

    HotReloadableLayer* HotReloadLayerProxy::SwapActive(HotReloadableLayer* newLayer) noexcept
    {
        HotReloadableLayer* previous = nullptr;
        {
            std::unique_lock lock(m_CallMutex);
            previous = std::exchange(m_Active, nullptr);
        }
        if (m_StackAttached && previous)
        {
            Guarded("OnDetach", [&] { previous->OnDetach(); });
        }
        bool attached = true;
        if (newLayer)
        {
            newLayer->SetLayerTarget(GetLayerTarget());
            if (m_StackAttached)
            {
                attached = Guarded("OnAttach", [&] { newLayer->OnAttach(); });
            }
        }
        {
            std::unique_lock lock(m_CallMutex);
            m_Active = attached ? newLayer : nullptr;
        }
        return previous;
    }

    void HotReloadLayerProxy::QueueSwap(HotReloadableLayer* newLayer, RetiredHotReloadInstance retired)
    {
        if (!m_StackAttached)
        {
            SwapActive(newLayer);
            return;
        }
        std::scoped_lock lock(m_PendingMutex);
        m_Pending.push_back(PendingSwap{newLayer, std::move(retired)});
        m_HasPending.store(true, std::memory_order_release);
    }

    HotReloadableLayer* HotReloadLayerProxy::GetActive() const noexcept
    {
        std::shared_lock lock(m_CallMutex);
        return m_Active;
    }
}

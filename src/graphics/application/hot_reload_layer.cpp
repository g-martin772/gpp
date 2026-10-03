module GPP.Graphics;

import :Application.HotReloadLayer;
import :RenderGraph;
import std;

namespace GPP
{
    void HotReloadLayerProxy::OnAttach()
    {
        std::scoped_lock lock(m_Mutex);
        m_StackAttached = true;
        if (m_Active)
        {
            m_Active->OnAttach();
        }
    }

    void HotReloadLayerProxy::OnDetach()
    {
        std::scoped_lock lock(m_Mutex);
        if (m_Active)
        {
            m_Active->OnDetach();
        }
        m_StackAttached = false;
    }

    void HotReloadLayerProxy::OnUpdate(float deltaTime)
    {
        if (auto* active = GetActive())
        {
            active->OnUpdate(deltaTime);
        }
    }

    void HotReloadLayerProxy::OnRender()
    {
        if (auto* active = GetActive())
        {
            active->OnRender();
        }
    }

    void HotReloadLayerProxy::OnRenderGraph(RenderGraph& graph)
    {
        if (auto* active = GetActive())
        {
            active->OnRenderGraph(graph);
        }
    }

    void HotReloadLayerProxy::OnUiRender()
    {
        if (auto* active = GetActive())
        {
            active->OnUiRender();
        }
    }

    void HotReloadLayerProxy::OnEvent()
    {
        if (auto* active = GetActive())
        {
            active->OnEvent();
        }
    }

    HotReloadableLayer* HotReloadLayerProxy::SwapActive(HotReloadableLayer* newLayer) noexcept
    {
        std::scoped_lock lock(m_Mutex);
        HotReloadableLayer* previous = m_Active;
        if (m_StackAttached && previous)
        {
            previous->OnDetach();
        }
        m_Active = newLayer;
        if (m_StackAttached && newLayer)
        {
            newLayer->OnAttach();
        }
        return previous;
    }

    HotReloadableLayer* HotReloadLayerProxy::GetActive() const noexcept
    {
        std::scoped_lock lock(m_Mutex);
        return m_Active;
    }
}

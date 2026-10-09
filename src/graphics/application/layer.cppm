export module GPP.Graphics:Application.Layer;

import std;
import GPP.Core;

import :Windowing;
import :RenderGraph;

namespace GPP
{
    export struct LayerTarget
    {
        LayerTarget() = default;

        enum class Type : std::uint32_t
        {
            eNone = 0,
            eLayerTargetWindow,
            eLayerTargetBuffer
        };

        Type Type{Type::eNone};
        std::uint32_t Id = -1;
        std::string Name;

        bool operator==(const LayerTarget&) const = default;
    };

    export struct GuiLayer : public IService
    {
        GuiLayer(const std::shared_ptr<Logger>& logger)
            : m_Logger(logger)
        {
        }

        virtual void OnAttach()
        {
        }

        virtual void OnDetach()
        {
        }

        virtual void OnUpdate(float deltaTime)
        {
        }

        virtual void OnRender()
        {
        }

        virtual void OnRenderGraph(RenderGraph& graph)
        {
        }

        virtual void OnUiRender()
        {
        }

        virtual void OnEvent(/*???*/)
        {
        }

        virtual void OnSafePoint()
        {
        }

        void SetLayerTarget(const LayerTarget& target) noexcept { m_LayerTarget = target; }
        [[nodiscard]] const LayerTarget& GetLayerTarget() const noexcept { return m_LayerTarget; }

    protected:
        std::shared_ptr<Logger> m_Logger;
        LayerTarget m_LayerTarget;
        std::string m_LayerName;

        friend struct GuiLayerBuilder;
    };

    export struct GuiLayerBuilder
    {
        std::type_index LayerType;
        std::string LayerName;
        LayerTarget Target;

        GuiLayerBuilder(const std::type_index layerType)
            : LayerType(layerType), LayerName(layerType.name())
        {
        }

        GuiLayerBuilder& SetWindowTarget(const WindowId target)
        {
            Target.Type = LayerTarget::Type::eLayerTargetWindow;
            Target.Id = target;
            return *this;
        }

        GuiLayerBuilder& SetWindowTarget(std::string name)
        {
            Target.Type = LayerTarget::Type::eLayerTargetWindow;
            Target.Id = -1;
            Target.Name = std::move(name);
            return *this;
        }

        GuiLayerBuilder& SetBufferTarget(const std::uint32_t bufferId)
        {
            Target.Type = LayerTarget::Type::eLayerTargetBuffer;
            Target.Id = bufferId;
            return *this;
        }

        std::shared_ptr<GuiLayer> Build(ServiceProvider& sp) const
        {
            auto layer = sp.GetServiceByTypeIndex<GuiLayer>(LayerType);
            layer->m_LayerTarget = Target;
            layer->m_LayerName = LayerName;
            return layer;
        }
    };

    export class GuiLayerStack
    {
    public:
        void PushLayer(const std::shared_ptr<GuiLayer>& layer)
        {
            m_Layers.emplace(m_Layers.begin() + m_LayerInsertIndex, layer);
            m_LayerInsertIndex++;
        }

        void PopLayer(const std::shared_ptr<GuiLayer>& layer)
        {
            if (const auto it = std::find(m_Layers.begin(), m_Layers.begin() + m_LayerInsertIndex, layer);
                it != m_Layers.begin() + m_LayerInsertIndex)
            {
                layer->OnDetach();
                m_Layers.erase(it);
                m_LayerInsertIndex--;
            }
        }

        void OnAttach() const
        {
            for (const auto layer : m_Layers)
            {
                layer->OnAttach();
            }
        }

        void OnDetach() const
        {
            for (const auto layer : m_Layers)
            {
                layer->OnDetach();
            }
        }

        void OnUpdate(float deltaTime) const
        {
            for (const auto layer : m_Layers)
            {
                layer->OnUpdate(deltaTime);
            }
        }

        void OnRender() const
        {
            for (const auto layer : m_Layers)
            {
                layer->OnRender();
            }
        }

        void OnRenderGraph(RenderGraph& graph) const
        {
            for (const auto layer : m_Layers)
            {
                layer->OnRenderGraph(graph);
            }
        }

        void OnUiRender() const
        {
            for (const auto layer : m_Layers)
            {
                layer->OnUiRender();
            }
        }

        void OnEvent(/*???*/) const
        {
            for (const auto layer : m_Layers)
            {
                layer->OnEvent();
            }
        }

        void OnSafePoint() const
        {
            for (const auto layer : m_Layers)
            {
                layer->OnSafePoint();
            }
        }

    private:
        std::vector<std::shared_ptr<GuiLayer>> m_Layers = {};
        std::uint32_t m_LayerInsertIndex = 0;
    };
}

template <>
struct std::hash<GPP::LayerTarget>
{
    std::size_t operator()(const GPP::LayerTarget& target) const noexcept
    {
        std::size_t h1 = std::hash<std::uint32_t>{}(static_cast<std::uint32_t>(target.Type));
        std::size_t h2 = std::hash<std::uint32_t>{}(target.Id);
        std::size_t h3 = std::hash<std::string>{}(target.Name);
        return h1 ^ (h2 << 1) ^ (h3 << 2);
    }
};

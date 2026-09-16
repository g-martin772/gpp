export module GPP.Graphics:Application;

export import :Application.Layer;

import std;
import GPP.Core;

namespace GPP
{
    export class GuiApplicationBuilder;
    export using LayerStackTable = std::unordered_map<LayerTarget, GuiLayerStack>;

    export struct GuiApplication : public Application
    {
        GuiApplication(ServiceProvider&& provider, std::unique_ptr<IConfiguration> configuration)
            : Application(std::move(provider), std::move(configuration))
        {
        }

        GuiApplication(ServiceProvider&& provider, std::unique_ptr<IConfiguration> configuration, LayerStackTable&& layerStacks)
            : Application(std::move(provider), std::move(configuration)), m_LayerStacks(std::move(layerStacks))
        {
        }

        void OnStart() override;
        void OnUpdate(float deltaTime) override;
        void OnStop() override;

    private:
        LayerStackTable m_LayerStacks;
        friend class GuiApplicationBuilder;
    };

    class GuiApplicationBuilder : public builder<GuiApplication>
    {
    public:
        GuiApplicationBuilder();
        std::shared_ptr<GuiApplication> Build() override;

        template <typename TLayer> requires std::is_base_of_v<GuiLayer, TLayer>
        GuiLayerBuilder& AddGuiLayer()
        {
            const auto layer = GuiLayerBuilder(std::type_index(typeid(TLayer)));
            m_LayerDescriptions.push_back(layer);
            Services.AddSingleton<TLayer>();
            return m_LayerDescriptions.back();
        }

    private:
        std::vector<GuiLayerBuilder> m_LayerDescriptions;
    };
}

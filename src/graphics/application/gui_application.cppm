export module GPP.Graphics:Application;

export import :Application.Layer;
export import :Application.HotReloadLayer;
export import :Application.HotReloadLayerManager;
export import :Application.Theme;
export import :Application.ThemeManager;

import std;
import GPP.Core;
import :RenderConfig;
import :ShaderAssets;
import :FontAssets;
import :UI.Preferences;

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

        GuiApplicationBuilder& SetHeadless(bool enabled = true)
        {
            m_Headless = enabled;
            return *this;
        }

        GuiApplicationBuilder& AddWindow(std::string name, WindowOptions options)
        {
            m_WindowDefinitions.Items.push_back(WindowDefinition{
                .Name = std::move(name),
                .Options = std::move(options)
            });
            return *this;
        }

        template <typename TLayer> requires std::is_base_of_v<GuiLayer, TLayer>
        GuiLayerBuilder& AddGuiLayer()
        {
            const auto layer = GuiLayerBuilder(std::type_index(typeid(TLayer)));
            m_LayerDescriptions.push_back(layer);
            Services.AddSingleton<TLayer>();
            return m_LayerDescriptions.back();
        }

        HotReloadLayerBuilder& AddHotReloadableLayer(std::string id, std::filesystem::path libraryPath)
        {
            m_HotReloadLayerDescriptions.emplace_back(std::move(id), std::move(libraryPath));
            return m_HotReloadLayerDescriptions.back();
        }

        GuiApplicationBuilder& ConfigureImGui(std::vector<std::string> configFlags, bool enableDockSpace = false)
        {
            ImGuiOptions options;
            options.ConfigFlags = std::move(configFlags);
            options.EnableDockSpace = enableDockSpace;
            m_ImGuiOptions = std::move(options);
            return *this;
        }

        template <typename TTheme> requires std::is_base_of_v<Theme, TTheme>
        GuiApplicationBuilder& SetTheme()
        {
            m_ThemeDescription = ThemeDescription{.ClassType = std::type_index(typeid(TTheme))};
            Services.AddSingleton<TTheme>();
            return *this;
        }

        GuiApplicationBuilder& SetTheme(std::filesystem::path libraryPath, bool enableHotReload = false,
                                        std::chrono::milliseconds pollingInterval = std::chrono::milliseconds(300))
        {
            m_ThemeDescription = ThemeDescription{
                .LibraryPath = std::move(libraryPath),
                .EnableHotReload = enableHotReload,
                .PollingInterval = pollingInterval
            };
            return *this;
        }

    private:
        std::vector<GuiLayerBuilder> m_LayerDescriptions;
        std::vector<HotReloadLayerBuilder> m_HotReloadLayerDescriptions;
        WindowDefinitions m_WindowDefinitions;
        std::optional<ImGuiOptions> m_ImGuiOptions;
        std::optional<ThemeDescription> m_ThemeDescription;
        bool m_Headless = false;
    };
}

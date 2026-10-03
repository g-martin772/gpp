export module GPP.Graphics:RenderConfig;

import std;
import GPP.Core;

namespace GPP
{
    inline std::vector<std::string> ParseStringArray(const IConfigurationSection& config,
                                                       const std::string& key)
    {
        std::vector<std::string> values;
        const auto section = config.GetSection(key);
        for (std::size_t index = 0;; ++index)
        {
            std::string value;
            if (!section->TryGetValue(std::to_string(index), value))
            {
                break;
            }
            if (!value.empty())
            {
                values.push_back(std::move(value));
            }
        }
        return values;
    }

    export struct RenderOptions : public IService
    {
        std::vector<std::filesystem::path> ShaderAssetDirectories{"shaders"};
        bool EnableShaderHotReload = false;
        std::chrono::milliseconds ShaderHotReloadInterval{250};

        static RenderOptions FromConfig(const IConfigurationSection& config)
        {
            RenderOptions options;
            options.ShaderAssetDirectories.clear();
            for (std::size_t index = 0;; ++index)
            {
                std::string value;
                if (!config.GetSection("ShaderAssetDirectories")->TryGetValue(
                    std::to_string(index), value))
                {
                    break;
                }
                if (!value.empty())
                {
                    options.ShaderAssetDirectories.emplace_back(std::move(value));
                }
            }
            if (options.ShaderAssetDirectories.empty())
            {
                options.ShaderAssetDirectories.emplace_back("shaders");
            }
            options.EnableShaderHotReload =
                config.GetValue<bool>("EnableShaderHotReload", false);
            options.ShaderHotReloadInterval = std::chrono::milliseconds(
                std::max(1, config.GetValue<int>("ShaderHotReloadIntervalMilliseconds", 250)));
            return options;
        }
    };

    export struct LayerHotReloadOptions : public IService
    {
        bool Enabled = false;
        std::chrono::milliseconds PollingInterval{300};
        std::filesystem::path ShadowDirectory{".gpp/hot-reload-cache"};
        bool ShadowCopy = true;

        static LayerHotReloadOptions FromConfig(const IConfigurationSection& config)
        {
            LayerHotReloadOptions options;
            options.Enabled = config.GetValue<bool>("Enabled", false);
            options.PollingInterval = std::chrono::milliseconds(
                std::max(1, config.GetValue<int>("PollingIntervalMilliseconds", 300)));
            options.ShadowDirectory = config.GetValue<std::string>(
                "ShadowDirectory", ".gpp/hot-reload-cache");
            options.ShadowCopy = config.GetValue<bool>("ShadowCopy", true);
            return options;
        }
    };

    export struct ImGuiOptions : public IService
    {
        // Names of ImGuiConfigFlags_ enumerators, e.g. "DockingEnable", "ViewportsEnable",
        // "NavEnableKeyboard". Resolved to the actual bitmask by ParseImGuiConfigFlags (:UI.Backend),
        // which keeps this options type free of an imgui dependency.
        std::vector<std::string> ConfigFlags;
        bool EnableDockSpace = false;

        static ImGuiOptions FromConfig(const IConfigurationSection& config)
        {
            ImGuiOptions options;
            options.ConfigFlags = ParseStringArray(config, "ConfigFlags");
            options.EnableDockSpace = config.GetValue<bool>("EnableDockSpace", false);
            return options;
        }
    };

    export struct ThemeOptions : public IService
    {
        // Only the hot-reloadable (.so) registration path is representable in JSON; an in-process
        // theme is registered by class via GuiApplicationBuilder::SetTheme<TTheme>() in code.
        std::filesystem::path LibraryPath;
        bool EnableHotReload = false;
        std::chrono::milliseconds PollingInterval{300};

        static ThemeOptions FromConfig(const IConfigurationSection& config)
        {
            ThemeOptions options;
            options.LibraryPath = config.GetValue<std::string>("LibraryPath", "");
            options.EnableHotReload = config.GetValue<bool>("EnableHotReload", false);
            options.PollingInterval = std::chrono::milliseconds(
                std::max(1, config.GetValue<int>("PollingIntervalMilliseconds", 300)));
            return options;
        }
    };

    export struct FontOptions : public IService
    {
        std::vector<std::filesystem::path> FontDirectories{"fonts"};
        std::string DefaultFont;
        float DefaultFontSize = 16.0f;
        float UiScale = 1.0f;

        static FontOptions FromConfig(const IConfigurationSection& config)
        {
            FontOptions options;
            options.FontDirectories.clear();
            for (const auto& value : ParseStringArray(config, "FontDirectories"))
            {
                options.FontDirectories.emplace_back(value);
            }
            if (options.FontDirectories.empty())
            {
                options.FontDirectories.emplace_back("fonts");
            }
            options.DefaultFont = config.GetValue<std::string>("DefaultFont", "");
            options.DefaultFontSize = config.GetValue<float>("DefaultFontSize", 16.0f);
            options.UiScale = config.GetValue<float>("UiScale", 1.0f);
            return options;
        }
    };
}

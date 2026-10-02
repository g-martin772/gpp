export module GPP.Graphics:RenderConfig;

import std;
import GPP.Core;

namespace GPP
{
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
}

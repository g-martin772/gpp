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
}

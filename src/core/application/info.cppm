export module GPP.Core:Application.Info;

import std;
import :DI.Service;
import :Application.Config;

namespace GPP
{
    export struct AppInfo : IService
    {
        std::string Id = "gpp-app";
        std::string Name = "GPP App";
        std::string Organization;
        std::string Version = "0.0.0";
        std::string Summary;
        std::string Categories;
        std::string IconFile;

        static AppInfo FromConfig(const IConfigurationSection& config)
        {
            return FromConfig(config, AppInfo{});
        }

        // Keys missing from config keep the values of `defaults`.
        static AppInfo FromConfig(const IConfigurationSection& config, AppInfo defaults)
        {
            defaults.Id = config.GetValue<std::string>("Id", defaults.Id);
            defaults.Name = config.GetValue<std::string>("Name", defaults.Name);
            defaults.Organization = config.GetValue<std::string>("Organization", defaults.Organization);
            defaults.Version = config.GetValue<std::string>("Version", defaults.Version);
            defaults.Summary = config.GetValue<std::string>("Summary", defaults.Summary);
            defaults.Categories = config.GetValue<std::string>("Categories", defaults.Categories);
            defaults.IconFile = config.GetValue<std::string>("IconFile", defaults.IconFile);
            return defaults;
        }
    };
}

module;

#include <nlohmann/json.hpp>

module GPP.Core;

import :Application.Config;

#if defined(_WIN32)
extern "C" char** _environ;
#else
extern "C" char** environ;
#endif

namespace GPP
{
    std::string ConfigurationSection::GetValue(const std::string& key) const
    {
        std::string outValue;
        if (TryGetValue(key, outValue)) return outValue;
        return "";
    }

    bool ConfigurationSection::TryGetValue(const std::string& key, std::string& outValue) const
    {
        std::string fullKey = m_Prefix.empty() ? key : m_Prefix + ":" + key;
        auto it = m_Data->find(fullKey);
        if (it != m_Data->end()) {
            outValue = it->second;
            return true;
        }
        return false;
    }

    std::unique_ptr<IConfigurationSection> ConfigurationSection::GetSection(const std::string& path) const
    {
        std::string fullPrefix = m_Prefix.empty() ? path : m_Prefix + ":" + path;
        return std::make_unique<ConfigurationSection>(m_Data, fullPrefix);
    }

    std::string Configuration::GetValue(const std::string& key) const
    {
        auto it = m_Data->find(key);
        if (it != m_Data->end()) return it->second;
        return "";
    }

    bool Configuration::TryGetValue(const std::string& key, std::string& outValue) const
    {
        auto it = m_Data->find(key);
        if (it != m_Data->end()) {
            outValue = it->second;
            return true;
        }
        return false;
    }

    std::unique_ptr<IConfigurationSection> Configuration::GetSection(const std::string& path) const
    {
        return std::make_unique<ConfigurationSection>(m_Data, path);
    }

    CommandLineProvider::CommandLineProvider(int argc, char* argv[])
    {
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg.starts_with("--")) {
                arg = arg.substr(2); // Strip prefix "--"
                auto eqPos = arg.find('=');
                if (eqPos != std::string::npos) {
                    std::string key = arg.substr(0, eqPos);
                    std::string val = arg.substr(eqPos + 1);
                    m_Args[key] = val;
                } else if (i + 1 < argc) {
                    std::string nextArg = argv[i + 1];
                    if (!nextArg.starts_with("--")) {
                        m_Args[arg] = nextArg;
                        ++i;
                    } else {
                        m_Args[arg] = "true";
                    }
                } else {
                    m_Args[arg] = "true";
                }
            }
        }
    }

    void CommandLineProvider::Load(std::unordered_map<std::string, std::string>& data)
    {
        for (const auto& [key, val] : m_Args) {
            data[key] = val;
        }
    }

    void EnvironmentVariablesProvider::Load(std::unordered_map<std::string, std::string>& data)
    {
#ifdef _WIN32
        char** envs = _environ;
#else
        char** envs = environ;
#endif
        if (!envs) return;

        for (char** env = envs; *env != nullptr; ++env) {
            std::string envStr(*env);
            auto eqPos = envStr.find('=');
            if (eqPos != std::string::npos) {
                std::string key = envStr.substr(0, eqPos);
                std::string val = envStr.substr(eqPos + 1);

                if (key.starts_with(m_Prefix)) {
                    std::string cleanKey = key.substr(m_Prefix.length());

                    // replace "__" with ":"
                    std::size_t pos = 0;
                    while ((pos = cleanKey.find("__", pos)) != std::string::npos) {
                        cleanKey.replace(pos, 2, ":");
                        pos += 1;
                    }
                    data[cleanKey] = val;
                }
            }
        }
    }

    void JsonConfigurationProvider::Load(std::unordered_map<std::string, std::string>& data)
    {
        auto path = m_FilePath;

        if (m_FileSystem) {
            path = m_FileSystem->ResolvePath(m_FilePath);
        }

        std::ifstream file(m_FilePath);
        if (!file.is_open()) throw std::runtime_error("Failed to open JSON configuration file: " + m_FilePath);

        try {
            nlohmann::json j;
            file >> j;
            FlattenJson(j, "", data);
        } catch (const std::exception& e) {
            throw std::runtime_error("Failed to load JSON configuration from " + m_FilePath + ": " + e.what());
        }
    }

    void JsonConfigurationProvider::FlattenJson(const nlohmann::json& j, const std::string& prefix,
        std::unordered_map<std::string, std::string>& data)
    {
        if (j.is_object()) {
            for (auto it = j.begin(); it != j.end(); ++it) {
                std::string key = prefix.empty() ? it.key() : prefix + ":" + it.key();
                FlattenJson(it.value(), key, data);
            }
        } else if (j.is_array()) {
            for (std::size_t idx = 0; idx < j.size(); ++idx) {
                std::string key = prefix + ":" + std::to_string(idx);
                FlattenJson(j[idx], key, data);
            }
        } else {
            if (j.is_string()) {
                data[prefix] = j.get<std::string>();
            } else if (j.is_boolean()) {
                data[prefix] = j.get<bool>() ? "true" : "false";
            } else if (j.is_number()) {
                data[prefix] = j.dump();
            } else if (j.is_null()) {
                data[prefix] = "null";
            }
        }
    }

    ConfigurationBuilder& ConfigurationBuilder::AddCommandLine(int argc, char* argv[])
    {
        m_Providers.push_back(std::make_unique<CommandLineProvider>(argc, argv));
        return *this;
    }

    ConfigurationBuilder& ConfigurationBuilder::AddEnvironmentVariables(std::string prefix)
    {
        m_Providers.push_back(std::make_unique<EnvironmentVariablesProvider>(std::move(prefix)));
        return *this;
    }

    ConfigurationBuilder& ConfigurationBuilder::AddJsonFile(std::string filePath, IFileSystem* fs)
    {
        m_Providers.push_back(std::make_unique<JsonConfigurationProvider>(std::move(filePath), fs));
        return *this;
    }

    std::unique_ptr<IConfiguration> ConfigurationBuilder::Build()
    {
        auto mergedData = std::make_shared<std::unordered_map<std::string, std::string>>();
        for (const auto& provider : m_Providers) {
            provider->Load(*mergedData);
        }
        return std::make_unique<Configuration>(std::move(mergedData));
    }
}

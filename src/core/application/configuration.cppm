module;

#include <nlohmann/json.hpp>

export module GPP.Core:Application.Config;

import std;
import :IO.File;

namespace GPP {
    export class IConfigurationSection {
    public:
        virtual ~IConfigurationSection() = default;
        
        [[nodiscard]] virtual std::string GetValue(const std::string& key) const = 0;
        [[nodiscard]] virtual bool TryGetValue(const std::string& key, std::string& outValue) const = 0;
        [[nodiscard]] virtual std::unique_ptr<IConfigurationSection> GetSection(const std::string& path) const = 0;

        template<typename T>
        [[nodiscard]] T GetValue(const std::string& key, T defaultValue) const;
    };

    export template<typename T>
    concept ConfigurableOptions = requires(const IConfigurationSection& config) {
        { T::FromConfig(config) } -> std::same_as<T>;
    };

    export class IConfiguration : public IConfigurationSection {
    public:
        ~IConfiguration() override = default;
    };

    export class ConfigurationSection : public IConfigurationSection {
    public:
        ConfigurationSection(std::shared_ptr<std::unordered_map<std::string, std::string>> data, std::string prefix)
            : m_Data(std::move(data)), m_Prefix(std::move(prefix)) {}

        [[nodiscard]] std::string GetValue(const std::string& key) const override;
        [[nodiscard]] bool TryGetValue(const std::string& key, std::string& outValue) const override;
        [[nodiscard]] std::unique_ptr<IConfigurationSection> GetSection(const std::string& path) const override;
    private:
        std::shared_ptr<std::unordered_map<std::string, std::string>> m_Data;
        std::string m_Prefix;
    };

    export class Configuration : public IConfiguration {
    public:
        explicit Configuration(std::shared_ptr<std::unordered_map<std::string, std::string>> data)
            : m_Data(std::move(data)) {}

        [[nodiscard]] std::string GetValue(const std::string& key) const override;
        [[nodiscard]] bool TryGetValue(const std::string& key, std::string& outValue) const override;
        [[nodiscard]] std::unique_ptr<IConfigurationSection> GetSection(const std::string& path) const override;
    private:
        std::shared_ptr<std::unordered_map<std::string, std::string>> m_Data;
    };

    export class IConfigurationProvider {
    public:
        virtual ~IConfigurationProvider() = default;
        virtual void Load(std::unordered_map<std::string, std::string>& data) = 0;
    };

    export class CommandLineProvider : public IConfigurationProvider {
    public:
        CommandLineProvider(int argc, char* argv[]);
        void Load(std::unordered_map<std::string, std::string>& data) override;
    private:
        std::unordered_map<std::string, std::string> m_Args;
    };

    export class EnvironmentVariablesProvider : public IConfigurationProvider {
    public:
        explicit EnvironmentVariablesProvider(std::string prefix = "GPP_") 
            : m_Prefix(std::move(prefix)) {}

        void Load(std::unordered_map<std::string, std::string>& data) override;
    private:
        std::string m_Prefix;
    };

    export class JsonConfigurationProvider : public IConfigurationProvider {
    public:
        explicit JsonConfigurationProvider(std::string filePath, IFileSystem* fs = nullptr)
            : m_FilePath(std::move(filePath)), m_FileSystem(fs) {}

        void Load(std::unordered_map<std::string, std::string>& data) override;
    private:
        void FlattenJson(const nlohmann::json& j, const std::string& prefix, std::unordered_map<std::string, std::string>& data);
        std::string m_FilePath;
        IFileSystem* m_FileSystem;
    };

    export class ConfigurationBuilder {
    public:
        ConfigurationBuilder() = default;
        ConfigurationBuilder& AddCommandLine(int argc, char* argv[]);
        ConfigurationBuilder& AddEnvironmentVariables(std::string prefix = "GPP_");
        ConfigurationBuilder& AddJsonFile(std::string filePath, IFileSystem* fs = nullptr);
        std::unique_ptr<IConfiguration> Build();
    private:
        std::vector<std::unique_ptr<IConfigurationProvider>> m_Providers{};
    };

    template <typename T>
    T IConfigurationSection::GetValue(const std::string& key, T defaultValue) const
    {
        std::string valStr;
        if (!TryGetValue(key, valStr)) {
            return defaultValue;
        }

        if constexpr (std::is_same_v<T, std::string>) {
            return valStr;
        } else if constexpr (std::is_same_v<T, bool>) {
            return (valStr == "true" || valStr == "1" || valStr == "yes" || valStr == "on");
        } else {
            std::istringstream iss(valStr);
            T result;
            if (iss >> result) {
                return result;
            }
            return defaultValue;
        }
    }
}

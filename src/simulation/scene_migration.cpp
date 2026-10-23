module;
#include <yaml-cpp/yaml.h>
module GPP.Simulation;

import :SceneMigration;
import :ComponentRegistry;
import GPP.Core;
import std;

namespace GPP
{
    std::string SceneVersions::Describe() const
    {
        std::string out = std::format("scene schema v{}", Schema);
        for (const auto& [key, version] : Extensions) out += std::format(", {} v{}", key, version);
        return out;
    }

    SceneMigrations::SceneMigrations()
    {
        m_Current[""] = kSceneSchemaVersion;
        m_Migrators.push_back(SceneMigrator{
            .From = 1,
            .To = 2,
            .Description = "legacy units to SI",
            .Apply = [](YAML::Node& root)
            {
                if (!root["TimeScale"]) root["TimeScale"] = Units::Legacy::kTimeS;
                const auto entities = root["Entities"];
                if (!entities || !entities.IsSequence()) return;
                for (const auto& entity : entities)
                {
                    const auto components = entity["Components"];
                    if (!components || !components.IsMap()) continue;
                    for (const auto& pair : components)
                    {
                        const auto* info = ComponentRegistry::Instance().FindByName(pair.first.as<std::string>(""));
                        if (!info || !info->Decode || !info->MigrateLegacy) continue;
                        YAML::Node node = pair.second;
                        info->MigrateLegacy(node);
                    }
                }
            }});
    }

    SceneMigrations& SceneMigrations::Instance()
    {
        static SceneMigrations instance;
        return instance;
    }

    void SceneMigrations::RegisterExtension(std::string key, const int currentVersion)
    {
        std::scoped_lock lock(m_Mutex);
        m_Current[std::move(key)] = currentVersion;
    }

    void SceneMigrations::Register(SceneMigrator migrator)
    {
        std::scoped_lock lock(m_Mutex);
        std::erase_if(m_Migrators, [&](const SceneMigrator& existing)
        {
            return existing.Extension == migrator.Extension && existing.From == migrator.From;
        });
        m_Migrators.push_back(std::move(migrator));
    }

    std::optional<int> SceneMigrations::CurrentVersion(const std::string& extension) const
    {
        std::scoped_lock lock(m_Mutex);
        const auto it = m_Current.find(extension);
        return it != m_Current.end() ? std::optional(it->second) : std::nullopt;
    }

    const SceneMigrator* SceneMigrations::Find(const std::string& extension, const int from) const
    {
        std::scoped_lock lock(m_Mutex);
        const auto it = std::ranges::find_if(m_Migrators, [&](const SceneMigrator& migrator)
        {
            return migrator.Extension == extension && migrator.From == from;
        });
        return it != m_Migrators.end() ? &*it : nullptr;
    }

    std::vector<std::string> SceneMigrations::Extensions() const
    {
        std::scoped_lock lock(m_Mutex);
        std::vector<std::string> keys;
        for (const auto& [key, version] : m_Current)
        {
            if (!key.empty()) keys.push_back(key);
        }
        return keys;
    }

    SceneVersions ReadSceneVersions(const YAML::Node& root)
    {
        SceneVersions versions;
        if (!root.IsMap()) return versions;
        if (const auto schema = root["SchemaVersion"]) versions.Schema = schema.as<int>(1);
        for (const auto& key : SceneMigrations::Instance().Extensions())
        {
            const auto node = root[key];
            if (!node || !node.IsMap()) continue;
            const auto current = *SceneMigrations::Instance().CurrentVersion(key);
            versions.Extensions[key] = node["Version"] ? node["Version"].as<int>(current) : current;
        }
        return versions;
    }

    std::string DescribeSceneVersions(const std::string& yaml)
    {
        try
        {
            return ReadSceneVersions(YAML::Load(yaml)).Describe();
        }
        catch (const std::exception&)
        {
            return {};
        }
    }

    namespace
    {
        void Advance(const std::string& extension, const std::string& label, YAML::Node node, int& version,
                     const int current, SceneMigrationReport& report)
        {
            if (version > current)
            {
                throw SceneVersionError(std::format("{} version {} is newer than this build supports ({}); "
                                                    "update the application to open this file",
                                                    label, version, current));
            }
            while (version < current)
            {
                const auto* migrator = SceneMigrations::Instance().Find(extension, version);
                if (!migrator)
                {
                    throw SceneVersionError(std::format("{} version {} cannot be migrated: no step from v{} to v{}",
                                                        label, version, version, version + 1));
                }
                if (migrator->Apply) migrator->Apply(node);
                report.Steps.push_back(std::format("{} v{} -> v{}{}", label, migrator->From, migrator->To,
                                                   migrator->Description.empty() ? "" : " (" + migrator->Description + ")"));
                version = migrator->To;
                if (!extension.empty()) node["Version"] = version;
            }
        }
    }

    SceneMigrationReport MigrateSceneDocument(YAML::Node& root)
    {
        SceneMigrationReport report;
        report.Loaded = ReadSceneVersions(root);
        report.Current = report.Loaded;

        auto& migrations = SceneMigrations::Instance();
        Advance("", "scene schema", root, report.Current.Schema, *migrations.CurrentVersion(""), report);
        root["SchemaVersion"] = report.Current.Schema;

        for (auto& [key, version] : report.Current.Extensions)
        {
            Advance(key, "extension '" + key + "'", root[key], version, *migrations.CurrentVersion(key), report);
        }
        return report;
    }
}

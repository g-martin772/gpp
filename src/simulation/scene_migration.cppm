module;
#include <yaml-cpp/yaml.h>
export module GPP.Simulation:SceneMigration;

import std;

namespace GPP
{
    export constexpr int kSceneSchemaVersion = 2;

    export struct SceneMigrator
    {
        std::string Extension;
        int From{0};
        int To{0};
        std::string Description;
        std::function<void(YAML::Node&)> Apply;
    };

    export struct SceneVersions
    {
        int Schema{1};
        std::map<std::string, int> Extensions;

        [[nodiscard]] std::string Describe() const;
    };

    export struct SceneMigrationReport
    {
        SceneVersions Loaded;
        SceneVersions Current;
        std::vector<std::string> Steps;
    };

    export class SceneVersionError : public std::runtime_error
    {
    public:
        using std::runtime_error::runtime_error;
    };

    // Bumping a version: (1) raise kSceneSchemaVersion, or the extension's current version passed to
    // RegisterExtension; (2) Register a SceneMigrator {Extension ("" = scene root), From = old, To = new,
    // Apply} that rewrites the YAML node in place; (3) add a golden file for the old version and a test that
    // it loads and re-saves at the current one. Steps run in order before any decoding; the version key is
    // set by the chain, not by Apply. Extension nodes carry their version in a "Version" key.
    export class SceneMigrations
    {
    public:
        static SceneMigrations& Instance();

        void RegisterExtension(std::string key, int currentVersion);
        void Register(SceneMigrator migrator);

        [[nodiscard]] std::optional<int> CurrentVersion(const std::string& extension) const;
        [[nodiscard]] const SceneMigrator* Find(const std::string& extension, int from) const;
        [[nodiscard]] std::vector<std::string> Extensions() const;

    private:
        SceneMigrations();

        mutable std::mutex m_Mutex;
        std::map<std::string, int> m_Current;
        std::vector<SceneMigrator> m_Migrators;
    };

    export [[nodiscard]] SceneVersions ReadSceneVersions(const YAML::Node& root);
    export [[nodiscard]] std::string DescribeSceneVersions(const std::string& yaml);

    export SceneMigrationReport MigrateSceneDocument(YAML::Node& root);
}

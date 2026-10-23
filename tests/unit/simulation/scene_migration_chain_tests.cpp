#include <catch2/catch_test_macros.hpp>
#include <yaml-cpp/yaml.h>

import GPP;
import std;

using namespace GPP;

namespace
{
    std::string ReadFixture(const std::string& name)
    {
        std::ifstream stream(std::filesystem::path(GPP_SOURCE_DIR) / "tests" / "fixtures" / "scenes" / name);
        return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    }

    // ChainExt: v1 {Alpha}, v2 adds Beta (= 2 * Alpha), v3 renames Alpha to Gamma.
    void RegisterChainExtension()
    {
        static std::once_flag flag;
        std::call_once(flag, []
        {
            auto& migrations = SceneMigrations::Instance();
            migrations.RegisterExtension("ChainExt", 3);
            migrations.Register({.Extension = "ChainExt", .From = 1, .To = 2, .Description = "add Beta",
                                 .Apply = [](YAML::Node& node) { node["Beta"] = node["Alpha"].as<int>() * 2; }});
            migrations.Register({.Extension = "ChainExt", .From = 2, .To = 3, .Description = "rename Alpha",
                                 .Apply = [](YAML::Node& node)
                                 {
                                     node["Gamma"] = node["Alpha"].as<int>();
                                     node.remove("Alpha");
                                 }});
            migrations.RegisterExtension("GapExt", 3);
            migrations.Register({.Extension = "GapExt", .From = 1, .To = 2, .Apply = [](YAML::Node&) {}});
        });
    }
}

TEST_CASE ("Schema v1 golden scene migrates and re-saves at the current version", "[simulation][scene][migration]")
{
    Scene scene;
    scene.DeserializeFromYaml(ReadFixture("schema-v1.yaml"));

    CHECK(scene.Metadata().LoadedSchemaVersion == 1);
    CHECK(scene.Metadata().LoadedVersions.Schema == 1);
    REQUIRE(scene.Metadata().MigrationSteps.size() == 1);
    CHECK(scene.Metadata().MigrationSteps[0].find("scene schema v1 -> v2") == 0);
    CHECK(scene.Metadata().TimeScale == Units::Legacy::kTimeS);

    const auto entity = scene.FindByGuid(11);
    REQUIRE(scene.IsValid(entity));
    CHECK(scene.Registry().get<TransformComponent>(entity).Position.x == Units::Legacy::kLengthM);

    const auto saved = scene.SerializeToYaml();
    CHECK(saved.find("SchemaVersion: " + std::to_string(kSceneSchemaVersion)) != std::string::npos);
    Scene reloaded;
    reloaded.DeserializeFromYaml(saved);
    CHECK(reloaded.Metadata().MigrationSteps.empty());
    CHECK(reloaded.Metadata().LoadedVersions.Schema == kSceneSchemaVersion);
    CHECK(reloaded.SerializeToYaml() == saved);
}

TEST_CASE ("Extension migrators run in order from the file's version", "[simulation][scene][migration]")
{
    RegisterChainExtension();

    SECTION ("from v1 through every step")
    {
        Scene scene;
        scene.DeserializeFromYaml(ReadFixture("extension-v1.yaml"));
        REQUIRE(scene.Metadata().MigrationSteps.size() == 2);
        CHECK(scene.Metadata().MigrationSteps[0] == "extension 'ChainExt' v1 -> v2 (add Beta)");
        CHECK(scene.Metadata().MigrationSteps[1] == "extension 'ChainExt' v2 -> v3 (rename Alpha)");
        CHECK(scene.Metadata().LoadedVersions.Extensions.at("ChainExt") == 1);

        const auto* node = scene.FindExtension("ChainExt");
        REQUIRE(node != nullptr);
        CHECK((*node)["Version"].as<int>() == 3);
        CHECK((*node)["Beta"].as<int>() == 14);
        CHECK((*node)["Gamma"].as<int>() == 7);
        CHECK_FALSE((*node)["Alpha"]);
    }
    SECTION ("from v2 only the last step")
    {
        Scene scene;
        scene.DeserializeFromYaml(ReadFixture("extension-v2.yaml"));
        REQUIRE(scene.Metadata().MigrationSteps.size() == 1);
        const auto* node = scene.FindExtension("ChainExt");
        REQUIRE(node != nullptr);
        CHECK((*node)["Beta"].as<int>() == 20);
        CHECK((*node)["Gamma"].as<int>() == 7);
    }
    SECTION ("migrated extensions save at the current version")
    {
        Scene scene;
        scene.DeserializeFromYaml(ReadFixture("extension-v1.yaml"));
        Scene reloaded;
        reloaded.DeserializeFromYaml(scene.SerializeToYaml());
        CHECK(reloaded.Metadata().MigrationSteps.empty());
        CHECK(reloaded.Metadata().LoadedVersions.Extensions.at("ChainExt") == 3);
    }
}

TEST_CASE ("Files newer than the build or with gaps in the chain are refused without touching the scene",
           "[simulation][scene][migration][transactional]")
{
    RegisterChainExtension();
    Scene scene;
    scene.DeserializeFromYaml("Scene: Keep\nEntities:\n  - Guid: 1\n    Components:\n      Metadata: {Name: K}\n");

    const auto keptIntact = [&]
    {
        CHECK(scene.Metadata().Name == "Keep");
        CHECK(scene.IsValid(scene.FindByGuid(1)));
    };

    CHECK_THROWS_AS(scene.DeserializeFromYaml("Scene: N\nSchemaVersion: 99\nEntities: []\n"), SceneVersionError);
    keptIntact();
    CHECK_THROWS_AS(scene.DeserializeFromYaml("Scene: N\nChainExt: {Version: 4}\nEntities: []\n"), SceneVersionError);
    keptIntact();
    CHECK_THROWS_AS(scene.DeserializeFromYaml("Scene: N\nGapExt: {Version: 1}\nEntities: []\n"), SceneVersionError);
    keptIntact();

    try
    {
        scene.DeserializeFromYaml("Scene: N\nSchemaVersion: 99\nEntities: []\n");
    }
    catch (const SceneVersionError& error)
    {
        CHECK(std::string(error.what()).find("newer than this build") != std::string::npos);
    }
}

TEST_CASE ("Validation reports versions and keeps original line numbers through migration",
           "[simulation][scene][migration][validation]")
{
    RegisterChainExtension();

    SECTION ("newer scene or extension is an error")
    {
        auto diagnostics = ValidateSceneYaml("Scene: N\nSchemaVersion: 99\nEntities: []\n");
        REQUIRE(diagnostics.size() == 1);
        CHECK(diagnostics[0].Severity == DiagnosticSeverity::Error);
        CHECK(diagnostics[0].Line == 2);

        diagnostics = ValidateSceneYaml("Scene: N\nEntities: []\nChainExt:\n  Version: 9\n");
        REQUIRE(diagnostics.size() == 1);
        CHECK(diagnostics[0].Severity == DiagnosticSeverity::Error);
        CHECK(diagnostics[0].Line == 4);
    }
    SECTION ("unknown extension with a version is a warning")
    {
        const auto diagnostics = ValidateSceneYaml("Scene: N\nEntities: []\nMystery:\n  Version: 4\n");
        REQUIRE(diagnostics.size() == 1);
        CHECK(diagnostics[0].Severity == DiagnosticSeverity::Warning);
        CHECK(diagnostics[0].Line == 3);
    }
    SECTION ("a gap in the chain is an error")
    {
        const auto diagnostics = ValidateSceneYaml("Scene: N\nEntities: []\nGapExt: {Version: 1}\n");
        REQUIRE(diagnostics.size() == 1);
        CHECK(diagnostics[0].Severity == DiagnosticSeverity::Error);
    }
    SECTION ("golden files validate and diagnostics point at the original lines")
    {
        CHECK(ValidateSceneYaml(ReadFixture("extension-v1.yaml")).empty());
        const auto diagnostics = ValidateSceneYaml(ReadFixture("schema-v1.yaml"));
        REQUIRE(diagnostics.size() == 1);
        CHECK(diagnostics[0].Component == "Bogus");
        CHECK(diagnostics[0].Line == 10);
    }
}

TEST_CASE ("Version summary names the scene schema and each registered extension", "[simulation][scene][migration]")
{
    RegisterChainExtension();
    CHECK(DescribeSceneVersions(ReadFixture("extension-v1.yaml")) == "scene schema v2, ChainExt v1");
    CHECK(DescribeSceneVersions(ReadFixture("schema-v1.yaml")) == "scene schema v1");
    CHECK(DescribeSceneVersions("not: [valid").empty());
}

#include <catch2/catch_test_macros.hpp>
#include <glm/glm.hpp>
#include <yaml-cpp/yaml.h>

import GPP;
import std;

using namespace GPP;

namespace
{
    enum class ProbeMode { Idle, Active };

    struct ProbeComponent
    {
        ProbeMode Mode{ProbeMode::Idle};
        double Radius{1.0};
        float Gain{1.0f};
        std::uint64_t Target{0};
    };

    void RegisterProbe()
    {
        static std::once_flag flag;
        std::call_once(flag, []
        {
            RegisterComponent<ProbeComponent>("Probe", ComponentDescription<ProbeComponent>{
                .Fields = {
                    Field("Mode", &ProbeComponent::Mode, {.Options = {"Idle", "Active"}}),
                    Field("Radius", &ProbeComponent::Radius, {.Dim = Units::Dimension::Length}),
                    Field("Gain", &ProbeComponent::Gain),
                    Field("Target", &ProbeComponent::Target),
                }});
        });
    }

    std::vector<SceneDiagnostic> Validate(const std::string& entityComponents, const std::string& header = {})
    {
        RegisterProbe();
        return ValidateSceneYaml(header + "Scene: V\nSchemaVersion: 2\nEntities:\n  - Guid: 5\n    Components:\n" + entityComponents);
    }

    const SceneDiagnostic* Find(const std::vector<SceneDiagnostic>& diagnostics, const std::string& fragment)
    {
        for (const auto& d : diagnostics)
            if (d.Message.find(fragment) != std::string::npos) return &d;
        return nullptr;
    }
}

TEST_CASE ("Valid scene yields no diagnostics", "[simulation][scene][validation]")
{
    const auto diagnostics = Validate("      Metadata:\n        Name: Ok\n      Probe:\n        Mode: Active\n"
                                      "        Radius: 2 km\n        Gain: 3\n");
    CHECK(diagnostics.empty());
}

TEST_CASE ("Unknown component is a located warning with entity context", "[simulation][scene][validation]")
{
    const auto diagnostics = Validate("      Metadata:\n        Name: Rock\n      Bogus:\n        X: 1\n");
    REQUIRE(diagnostics.size() == 1);
    const auto& d = diagnostics[0];
    CHECK(d.Severity == DiagnosticSeverity::Warning);
    CHECK(d.Component == "Bogus");
    CHECK(d.EntityGuid == 5);
    CHECK(d.EntityName == "Rock");
    CHECK(d.Line == 8);
    CHECK(d.Column == 7);
    CHECK(d.Format("f.yaml").starts_with("f.yaml:8:7: warning: entity 'Rock' (5), Bogus: "));
}

TEST_CASE ("Unknown field and bad enum value are warnings", "[simulation][scene][validation]")
{
    const auto diagnostics = Validate("      Probe:\n        Mode: Sleeping\n        Spin: 1\n");
    REQUIRE(diagnostics.size() == 2);
    const auto* mode = Find(diagnostics, "unknown value 'Sleeping'");
    REQUIRE(mode);
    CHECK(mode->Field == "Mode");
    CHECK(mode->Severity == DiagnosticSeverity::Warning);
    CHECK(mode->Line == 7);
    const auto* spin = Find(diagnostics, "unknown field 'Spin'");
    REQUIRE(spin);
    CHECK(spin->Field == "Spin");
    CHECK(spin->Line == 8);
    CHECK(CountDiagnostics(diagnostics, DiagnosticSeverity::Error) == 0);
}

TEST_CASE ("Unknown unit and unparsable values are errors with the field", "[simulation][scene][validation]")
{
    SECTION ("tagged unit")
    {
        const auto diagnostics = Validate("      Probe:\n        Radius: {value: 2, unit: parsec-ish}\n");
        REQUIRE(diagnostics.size() == 1);
        CHECK(diagnostics[0].Severity == DiagnosticSeverity::Error);
        CHECK(diagnostics[0].Field == "Radius");
        CHECK(diagnostics[0].Message.find("unknown unit") != std::string::npos);
    }
    SECTION ("quantity string")
    {
        const auto diagnostics = Validate("      Probe:\n        Radius: 3 furlongs\n");
        REQUIRE(diagnostics.size() == 1);
        CHECK(diagnostics[0].Severity == DiagnosticSeverity::Error);
        CHECK(diagnostics[0].Field == "Radius");
    }
    SECTION ("plain number field")
    {
        const auto diagnostics = Validate("      Probe:\n        Gain: lots\n");
        REQUIRE(diagnostics.size() == 1);
        CHECK(diagnostics[0].Severity == DiagnosticSeverity::Error);
        CHECK(diagnostics[0].Field == "Gain");
        CHECK(diagnostics[0].Line == 7);
    }
}

TEST_CASE ("Schema, syntax, guid and reference problems are reported", "[simulation][scene][validation]")
{
    SECTION ("newer schema")
    {
        const auto diagnostics = ValidateSceneYaml("Scene: V\nSchemaVersion: 99\nEntities: []\n");
        REQUIRE(diagnostics.size() == 1);
        CHECK(diagnostics[0].Severity == DiagnosticSeverity::Error);
        CHECK(diagnostics[0].Line == 2);
    }
    SECTION ("syntax error")
    {
        const auto diagnostics = ValidateSceneYaml("Scene: V\nEntities: [\n");
        REQUIRE(diagnostics.size() == 1);
        CHECK(diagnostics[0].Severity == DiagnosticSeverity::Error);
        CHECK(diagnostics[0].Line > 0);
    }
    SECTION ("not a mapping")
    {
        const auto diagnostics = ValidateSceneYaml("- a\n- b\n");
        REQUIRE(diagnostics.size() == 1);
        CHECK(diagnostics[0].Severity == DiagnosticSeverity::Error);
    }
    SECTION ("duplicate and missing guid, dangling reference")
    {
        RegisterProbe();
        const auto diagnostics = ValidateSceneYaml(
            "Scene: V\nEntities:\n"
            "  - Guid: 1\n    Components:\n      Probe:\n        Target: 77\n"
            "  - Guid: 1\n    Components: {}\n"
            "  - Components: {}\n");
        CHECK(Find(diagnostics, "duplicate Guid"));
        CHECK(Find(diagnostics, "no Guid"));
        const auto* ref = Find(diagnostics, "references entity 77");
        REQUIRE(ref);
        CHECK(ref->Field == "Target");
    }
}

TEST_CASE ("SceneManager::LoadSceneFromFile returns diagnostics without failing the load",
           "[simulation][scene][validation]")
{
    RegisterProbe();
    const auto dir = std::filesystem::temp_directory_path() / "gpp_validation_load";
    std::filesystem::create_directories(dir);
    const auto file = dir / "warn.yaml";
    { std::ofstream(file) << "Scene: Warny\nEntities:\n  - Guid: 9\n    Components:\n      Bogus: {}\n"; }

    SceneManager manager(std::make_shared<Logger>(), std::make_shared<FileSystem>());
    std::vector<SceneDiagnostic> diagnostics;
    const auto& scene = manager.LoadSceneFromFile(file, &diagnostics);

    CHECK(scene.Metadata().Name == "Warny");
    REQUIRE(diagnostics.size() == 1);
    CHECK(diagnostics[0].Component == "Bogus");
    std::filesystem::remove_all(dir);
}

TEST_CASE ("Scene::SpawnFromYaml leaves no entity behind when decoding throws", "[simulation][scene][transactional]")
{
    RegisterProbe();
    Scene scene;
    const auto entity = scene.SpawnFromYaml(4242, "Components:\n  Probe:\n    Radius: {value: 1, unit: nonsense}\n");
    CHECK(entity == entt::entity{entt::null});
    CHECK(scene.FindByGuid(4242) == entt::entity{entt::null});
}

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

import GPP;
import std;
import glm;

using namespace GPP;
using Catch::Approx;
namespace fs = std::filesystem;

namespace
{
    constexpr const char* kSceneYaml = R"(Scene: Mods
SchemaVersion: 2
Entities:
  - Guid: 5
    Components:
      Metadata: {Guid: 5, Name: Mover, TypeTag: Entity}
      Transform: {Position: [1, 0, 0]}
      Scripts:
        - Name: mover
          Props: {Speed: 3}
)";

    struct Rig
    {
        fs::path Root = fs::temp_directory_path() / "gpp_script_module_tests";
        std::unique_ptr<AssetDirectories> Assets;

        explicit Rig(const std::string& script)
        {
            fs::remove_all(Root);
            fs::create_directories(Root);
            std::ofstream(Root / "mover.luau") << script;
            AssetOptions options;
            options.Kinds["Scripts"] = AssetKindOptions{{Root}, {".luau"}};
            Assets = std::make_unique<AssetDirectories>(options);
        }

        ~Rig() { fs::remove_all(Root); }

        std::expected<HeadlessResult, std::string> Run(std::shared_ptr<ScriptSimulationModule>& module, const std::uint64_t ticks)
        {
            RegisterBaseComponents();
            RegisterScriptComponents();
            Scene scene;
            scene.DeserializeFromYaml(kSceneYaml);
            module = std::make_shared<ScriptSimulationModule>(*Assets);
            return RunHeadless(std::move(scene), {module}, {.Ticks = ticks, .Dt = 0.01, .Seed = 11,
                                                          .OnRunnerCreated = [&](SimulationRunner& runner) { module->SetRandom(runner.Random()); }});
        }
    };

    constexpr const char* kMover = R"(
return {
    properties = { { name = "Speed", type = "float", default = 2 } },
    OnTick = function(self, dt)
        local x, y, z = scene.get_xyz(self.entity, "Transform", "Position")
        scene.set_xyz(self.entity, "Transform", "Position", x + self.props.Speed * dt + math.random() * 0, y, z)
    end,
})";
}

TEST_CASE("The script module runs attached scripts inside a headless simulation", "[scripting][module][headless]")
{
    Rig rig(kMover);
    std::shared_ptr<ScriptSimulationModule> module;
    const auto result = rig.Run(module, 100);
    REQUIRE(result.has_value());
    CHECK(module->TakeErrors().empty());
    const auto& scene = result->Final;
    const auto position = scene.Registry().get<TransformComponent>(scene.FindByGuid(5)).Position;
    CHECK(position.x == Approx(4.0f).margin(1e-3));

    std::shared_ptr<ScriptSimulationModule> again;
    CHECK(rig.Run(again, 100)->Hash == result->Hash);
}

TEST_CASE("The script module collects script errors", "[scripting][module]")
{
    Rig rig("return { OnTick = function() error('nope') end }");
    std::shared_ptr<ScriptSimulationModule> module;
    const auto result = rig.Run(module, 3);
    REQUIRE(result.has_value());
    const auto errors = module->TakeErrors();
    REQUIRE_FALSE(errors.empty());
    CHECK(errors.front().Script == "mover");
    CHECK(errors.front().Message.find("nope") != std::string::npos);
    CHECK(module->TakeErrors().empty());
}

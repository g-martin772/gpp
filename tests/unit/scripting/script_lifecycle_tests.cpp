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
    constexpr const char* kProbe = R"(
return {
    OnStart = function(self) events.emit("log", "start") end,
    OnFixedTick = function(self) events.emit("log", "fixed") end,
    OnTick = function(self) events.emit("log", "tick") end,
    OnLateTick = function(self) events.emit("log", "late") end,
    OnContact = function(self, other, point, normal, impulse, velocity, material, otherMaterial)
        events.emit("contact", self.entity, other, point, normal, impulse, velocity, material, otherMaterial)
    end,
    OnTrigger = function(self, other, entered) events.emit("trigger", self.entity, other, entered) end,
    OnStop = function(self) events.emit("log", "stop") end,
}
)";

    constexpr const char* kScene = R"(Scene: Lifecycle
SchemaVersion: 2
Entities:
  - Guid: 1
    Components:
      Metadata: {Guid: 1, Name: Upper, TypeTag: Entity}
      Transform: {Position: [0, 3, 0]}
      Velocity: {Linear: [0, -4, 0]}
      RigidBody: {Type: Dynamic, Mass: 1, EnableGravity: false}
      Collider: {Shape: Sphere, Radius: 0.5, MaterialId: 7}
      Scripts:
        - Name: probe
  - Guid: 2
    Components:
      Metadata: {Guid: 2, Name: Lower, TypeTag: Entity}
      Transform: {Position: [0, 0, 0]}
      Velocity: {Linear: [0, -1, 0]}
      RigidBody: {Type: Dynamic, Mass: 1, EnableGravity: false}
      Collider: {Shape: Sphere, Radius: 0.5, MaterialId: 9}
      Scripts:
        - Name: probe
)";

    struct Recorded
    {
        std::string Name;
        std::vector<LuauValue> Args;
    };

    struct Run
    {
        std::uint64_t Hash{0};
        std::vector<Recorded> Events;
        std::vector<ScriptError> Errors;
    };

    Run Execute(const std::uint64_t ticks)
    {
        const auto root = fs::temp_directory_path() / "gpp_script_lifecycle_tests";
        fs::remove_all(root);
        fs::create_directories(root);
        std::ofstream(root / "probe.luau") << kProbe;
        AssetOptions options;
        options.Kinds["Scripts"] = AssetKindOptions{{root}, {".luau"}};
        AssetDirectories assets(options);

        RegisterBaseComponents();
        RegisterScriptComponents();
        Scene scene;
        scene.DeserializeFromYaml(kScene);

        Run run;
        auto physics = std::make_shared<PhysicsSimulationModule>(std::make_shared<EventDispatcher>(), std::make_shared<Logger>());
        auto scripts = std::make_shared<ScriptSimulationModule>(assets);
        scripts->SetPhysics(physics);
        (void)scripts->Host().Events().Subscribe({}, [&](const ScriptEvent& event) { run.Events.push_back({event.Name, event.Args}); });
        const auto result = RunHeadless(std::move(scene), {scripts, physics},
                                        {.Ticks = ticks, .Dt = 1.0 / 60.0, .Seed = 5,
                                         .OnRunnerCreated = [&](SimulationRunner& runner) { scripts->SetRandom(runner.Random()); }});
        REQUIRE(result.has_value());
        run.Hash = result->Hash;
        run.Errors = scripts->TakeErrors();
        fs::remove_all(root);
        return run;
    }

    std::vector<std::string> Names(const Run& run, const std::string& name)
    {
        std::vector<std::string> out;
        for (const auto& event : run.Events)
        {
            if (event.Name == name && !event.Args.empty() && std::holds_alternative<std::string>(event.Args[0]))
            {
                out.push_back(std::get<std::string>(event.Args[0]));
            }
        }
        return out;
    }
}

TEST_CASE("Lifecycle callbacks run in tick order and OnStop runs when the runner shuts down", "[scripting][lifecycle][headless]")
{
    const auto run = Execute(2);
    REQUIRE(run.Errors.empty());
    const auto log = Names(run, "log");
    const std::vector<std::string> expected{"start", "start", "fixed", "tick", "fixed", "tick", "late", "late",
                                            "fixed", "tick", "fixed", "tick", "late", "late", "stop", "stop"};
    CHECK(log == expected);
}

TEST_CASE("Physics contacts reach both scripts through the simulation module", "[scripting][lifecycle][headless][physics]")
{
    const auto run = Execute(90);
    REQUIRE(run.Errors.empty());
    std::vector<const Recorded*> contacts;
    for (const auto& event : run.Events)
    {
        if (event.Name == "contact") contacts.push_back(&event);
    }
    REQUIRE(contacts.size() == 2);
    const auto& first = contacts[0]->Args;
    const auto& second = contacts[1]->Args;
    REQUIRE(first.size() == 8);
    CHECK(std::get<std::uint64_t>(first[0]) == 1);
    CHECK(std::get<std::uint64_t>(first[1]) == 2);
    CHECK(std::get<std::uint64_t>(second[0]) == 2);
    CHECK(std::get<glm::vec3>(first[3]).y == Approx(1.0f).margin(0.05f));
    CHECK(std::get<glm::vec3>(second[3]).y == Approx(-1.0f).margin(0.05f));
    CHECK(std::get<glm::vec3>(first[5]).y == Approx(-3.0f).margin(0.05f));
    CHECK(std::get<double>(first[4]) > 1.0);
    CHECK(std::get<double>(first[6]) == 7);
    CHECK(std::get<double>(first[7]) == 9);
    CHECK(std::get<double>(second[6]) == 9);
}

TEST_CASE("Contact delivery and callbacks are deterministic", "[scripting][lifecycle][headless][physics][determinism]")
{
    const auto a = Execute(90);
    const auto b = Execute(90);
    CHECK(a.Hash == b.Hash);
    REQUIRE(a.Events.size() == b.Events.size());
    for (std::size_t i = 0; i < a.Events.size(); ++i)
    {
        CHECK(a.Events[i].Name == b.Events[i].Name);
        CHECK(a.Events[i].Args == b.Events[i].Args);
    }
}

TEST_CASE("The runner calls OnLateTick for every module after all OnTicks", "[simulation][lifecycle]")
{
    struct Recorder final : ISimulationModule
    {
        Recorder(std::vector<std::string>& log, std::string name, const SimulationPhase phase) : Log(log), Name(std::move(name)), Phase_(phase) {}
        [[nodiscard]] SimulationPhase Phase() const noexcept override { return Phase_; }
        void OnTick(Scene&, float) override { Log.push_back(Name + ":tick"); }
        void OnLateTick(Scene&, float) override { Log.push_back(Name + ":late"); }
        std::vector<std::string>& Log;
        std::string Name;
        SimulationPhase Phase_;
    };

    std::vector<std::string> log;
    const auto result = RunHeadless(Scene("Late"),
                                    {std::make_shared<Recorder>(log, "post", SimulationPhase::PostSim),
                                     std::make_shared<Recorder>(log, "pre", SimulationPhase::PreSim)},
                                    {.Ticks = 2, .Dt = 1.0 / 60.0});
    REQUIRE(result.has_value());
    const std::vector<std::string> expected{"pre:tick", "post:tick", "pre:late", "post:late",
                                            "pre:tick", "post:tick", "pre:late", "post:late"};
    CHECK(log == expected);
}

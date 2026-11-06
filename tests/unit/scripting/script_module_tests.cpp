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

namespace
{
    constexpr const char* kDriver = R"(
return {
    properties = { { name = "Speed", type = "float", default = 2 } },
    OnTick = function(self, dt)
        local x, y, z = scene.get_xyz(self.entity, "Transform", "Position")
        local m = input.mouse_delta()
        y += input.scroll() * 10
        if input.pressed("jump") then z += 5 end
        if input.released("jump") then z += 1 end
        if input.held("fire") then y += 0.5 end
        scene.set_xyz(self.entity, "Transform", "Position", x + input.axis("move_x") * self.props.Speed * dt + m.x * 0.001, y, z)
    end,
})";

    struct InputRig
    {
        Rig Files;
        Scene World;
        std::shared_ptr<ScriptSimulationModule> Module;
        std::unique_ptr<SimulationRunner> Runner;

        explicit InputRig(const std::string& script) : Files(script)
        {
            RegisterBaseComponents();
            RegisterScriptComponents();
            World.DeserializeFromYaml(kSceneYaml);
            Module = std::make_shared<ScriptSimulationModule>(*Files.Assets);
            Runner = std::make_unique<SimulationRunner>(World, Module);
            Module->SetRandom(Runner->Random());
            Module->SetInput(Runner->Input());
        }

        [[nodiscard]] glm::dvec3 Position() const
        {
            const auto snapshot = Runner->AcquireSnapshot();
            return snapshot->Registry().get<TransformComponent>(snapshot->FindByGuid(5)).Position;
        }
    };
}

TEST_CASE("input.* reads the frame the runner sampled for the tick", "[scripting][module][input]")
{
    InputRig rig(kDriver);
    rig.Runner->StepAndWait(5);
    CHECK(rig.Position().x == Approx(1.0).margin(1e-6));
    CHECK(rig.Runner->Input()->Used());

    rig.Runner->Input()->Set("move_x", 1.0f);
    rig.Runner->Input()->AddMouse({1000.0f, 0.0f});
    rig.Runner->StepAndWait(3);
    CHECK(rig.Position().x == Approx(1.0 + 1.0 + 3 * 3.0 * (1.0 / 60.0)).margin(1e-4));

    rig.Runner->Input()->Set("jump", 1.0f);
    rig.Runner->StepAndWait(4);
    CHECK(rig.Position().z == Approx(5.0));
    rig.Runner->Input()->Set("jump", 0.0f);
    rig.Runner->Input()->Set("fire", 1.0f);
    rig.Runner->StepAndWait(2);
    CHECK(rig.Position().z == Approx(6.0));
    CHECK(rig.Position().y == Approx(1.0));
    CHECK(rig.Module->TakeErrors().empty());
}

TEST_CASE("input.scroll accumulates per tick and replays to the same hash", "[scripting][module][input][replay]")
{
    InputRig rig(kDriver);
    rig.Runner->BeginRecording(5);
    rig.Runner->StepAndWait(2);
    rig.Runner->Input()->AddScroll(1.0f);
    rig.Runner->Input()->AddScroll(0.5f);
    rig.Runner->StepAndWait(1);
    CHECK(rig.Position().y == Approx(15.0));
    rig.Runner->StepAndWait(2);
    CHECK(rig.Position().y == Approx(15.0));
    rig.Runner->Input()->AddScroll(-2.0f);
    rig.Runner->StepAndWait(1);
    CHECK(rig.Position().y == Approx(-5.0));
    const auto recording = rig.Runner->EndRecording();
    const auto live = rig.Runner->StateHash();
    REQUIRE(recording.Inputs.size() == 2);
    CHECK(recording.Inputs[0].Frame.Scroll == Approx(1.5f));

    auto replay = [&](const SimulationRecording& which)
    {
        auto module = std::make_shared<ScriptSimulationModule>(*rig.Files.Assets);
        ReplayOptions options;
        options.OnRunnerCreated = [module](SimulationRunner& runner)
        {
            module->SetRandom(runner.Random());
            module->SetInput(runner.Input());
        };
        return VerifyReplay(which, {module}, options);
    };
    const auto loaded = RecordingFromYaml(RecordingToYaml(recording));
    CHECK(loaded.Inputs[1].Frame.Scroll == Approx(-2.0f));
    const auto verified = replay(loaded);
    INFO(verified.Message);
    CHECK(verified.Ok);
    CHECK(verified.FinalHash == live);

    auto without = recording;
    without.Inputs.clear();
    CHECK_FALSE(replay(without).Ok);
}

TEST_CASE("Scripts reading input replay to the same hash", "[scripting][module][input][replay]")
{
    InputRig rig(kDriver);
    rig.Runner->StepAndWait(2);
    rig.Runner->BeginRecording(5);
    rig.Runner->Input()->Set("move_x", 0.5f);
    rig.Runner->StepAndWait(6);
    rig.Runner->Input()->Set("jump", 1.0f);
    rig.Runner->Input()->AddMouse({300.0f, 0.0f});
    rig.Runner->StepAndWait(3);
    rig.Runner->Input()->Set("jump", 0.0f);
    rig.Runner->Input()->Set("fire", 1.0f);
    rig.Runner->Input()->Set("move_x", 0.0f);
    rig.Runner->StepAndWait(6);
    const auto recording = rig.Runner->EndRecording();
    const auto live = rig.Runner->StateHash();
    CHECK(rig.Position().z == Approx(6.0));
    REQUIRE(recording.FinalHash.has_value());
    CHECK(*recording.FinalHash == live);

    auto replay = [&](const SimulationRecording& which)
    {
        auto module = std::make_shared<ScriptSimulationModule>(*rig.Files.Assets);
        ReplayOptions options;
        options.OnRunnerCreated = [module](SimulationRunner& runner)
        {
            module->SetRandom(runner.Random());
            module->SetInput(runner.Input());
        };
        return VerifyReplay(which, {module}, options);
    };
    const auto verified = replay(recording);
    INFO(verified.Message);
    CHECK(verified.Ok);
    CHECK(verified.FinalHash == live);

    auto without = recording;
    without.Inputs.clear();
    CHECK_FALSE(replay(without).Ok);
}

TEST_CASE("Deferred start, Stop and posted events follow the play session", "[scripting][module][input]")
{
    constexpr const char* source = R"(
return {
    OnStart = function(self) events.emit("started") end,
    OnTick = function(self, dt) scene.set_xyz(self.entity, "Transform", "Position", time.elapsed(), 0, 0) end,
    OnEvent = function(self, name, a)
        if name == "ping" then scene.set_xyz(self.entity, "Transform", "Scale", a, 1, 1) end
    end,
    OnStop = function(self) events.emit("stopped") end,
})";
    Rig rig(source);
    RegisterBaseComponents();
    RegisterScriptComponents();
    Scene scene;
    scene.DeserializeFromYaml(kSceneYaml);
    auto module = std::make_shared<ScriptSimulationModule>(*rig.Assets, ScriptModuleOptions{.DeferStart = true});
    std::vector<std::string> names;
    (void)module->Host().Events().Subscribe({}, [&](const ScriptEvent& event) { names.push_back(event.Name); });

    SimulationRunner runner(scene, module);
    module->SetInput(runner.Input());
    runner.Step(1);
    CHECK(names == std::vector<std::string>{"started"});
    CHECK(module->Statuses().size() == 1);

    module->PostEvent("ping", {7.0f});
    runner.Step(1);
    CHECK(names.size() == 2);
    CHECK(runner.AcquireSnapshot()->Registry().get<TransformComponent>(runner.AcquireSnapshot()->FindByGuid(5)).Scale.x == Approx(7.0f));

    module->SetIgnored({"mover"});
    runner.Step(1);
    CHECK(module->Statuses().empty());

    module->SetIgnored({});
    runner.Step(1);
    CHECK(module->Statuses().size() == 1);
    const auto before = names.size();
    runner.EnqueueSystemEdit([&](Scene& live) { module->Stop(live); });
    runner.Step(1);
    REQUIRE(names.size() == before + 2);
    CHECK(names[before] == "stopped");
    CHECK(names[before + 1] == "started");
    CHECK(module->TakeErrors().empty());
}

namespace
{
    constexpr const char* kCounter = R"(
return {
    OnStart = function(self)
        self.starts = (self.starts or 0) + 1
        self.history = { 1, 2, { deep = vector.create(1, 2, 3) } }
        self.tag = "a:b\n;c"
        self.pair = vec2(0.5, -2)
        self.quad = vec4(1, 2, 3, 4)
        self.me = self.entity
        self.flag = true
        self.callback = function() end
    end,
    OnTick = function(self, dt)
        self.count = (self.count or 0) + 1
        self.total = (self.total or 0) + dt * 0.1
        scene.set_xyz(self.entity, "Transform", "Position", self.count, self.history[3].deep.y, self.starts)
    end,
}
)";

    struct SelfRig
    {
        Rig Files;
        std::shared_ptr<ScriptSimulationModule> Module;
        std::unique_ptr<SimulationRunner> Runner;

        explicit SelfRig(const std::string& script) : Files(script)
        {
            RegisterBaseComponents();
            RegisterScriptComponents();
            Scene scene;
            scene.DeserializeFromYaml(kSceneYaml);
            Module = std::make_shared<ScriptSimulationModule>(*Files.Assets);
            Runner = std::make_unique<SimulationRunner>(std::move(scene), Module);
            Module->SetRandom(Runner->Random());
            Module->SetInput(Runner->Input());
        }

        [[nodiscard]] glm::dvec3 Position() const
        {
            const auto snapshot = Runner->AcquireSnapshot();
            return snapshot->Registry().get<TransformComponent>(snapshot->FindByGuid(5)).Position;
        }
    };
}

TEST_CASE("Script self state is part of the module state and survives a restore", "[scripting][module][state]")
{
    SelfRig first(kCounter);
    first.Runner->StepAndWait(5);
    CHECK(first.Position().x == Approx(5.0));
    const auto saved = first.Module->SaveState();
    REQUIRE_FALSE(saved.empty());
    CHECK(saved.find("callback") == std::string::npos);
    CHECK(saved == first.Module->SaveState());

    SelfRig second(kCounter);
    second.Module->LoadState(saved);
    CHECK(second.Module->SaveState() == saved);
    second.Runner->StepAndWait(1);
    CHECK(second.Position().x == Approx(6.0));
    CHECK(second.Position().z == Approx(1.0));
    CHECK(second.Module->SaveState() != saved);

    first.Runner->StepAndWait(1);
    CHECK(second.Module->SaveState() == first.Module->SaveState());
    CHECK(second.Module->TakeErrors().empty());
}

TEST_CASE("Restoring state over a live instance replaces its fields", "[scripting][module][state]")
{
    SelfRig rig(kCounter);
    rig.Runner->StepAndWait(3);
    const auto saved = rig.Module->SaveState();
    rig.Runner->StepAndWait(4);
    CHECK(rig.Position().x == Approx(7.0));
    rig.Module->LoadState(saved);
    rig.Runner->StepAndWait(1);
    CHECK(rig.Position().x == Approx(4.0));
}

TEST_CASE("A recording started mid-run replays scripts with their self state", "[scripting][module][state][replay]")
{
    SelfRig rig(kCounter);
    rig.Runner->StepAndWait(4);
    rig.Runner->BeginRecording(3);
    rig.Runner->StepAndWait(9);
    const auto recording = rig.Runner->EndRecording();
    REQUIRE_FALSE(recording.State.Modules.empty());
    REQUIRE(recording.FinalHash.has_value());

    auto module = std::make_shared<ScriptSimulationModule>(*rig.Files.Assets);
    ReplayOptions options;
    options.OnRunnerCreated = [module](SimulationRunner& runner)
    {
        module->SetRandom(runner.Random());
        module->SetInput(runner.Input());
    };
    const auto verified = VerifyReplay(recording, {module}, options);
    INFO(verified.Message);
    CHECK(verified.Ok);
}

TEST_CASE("A reload request re-reads script sources on the next tick", "[scripting][module]")
{
    SelfRig rig("return { OnTick = function(self) scene.set_xyz(self.entity, 'Transform', 'Position', 1, 0, 0) end }");
    rig.Runner->StepAndWait(2);
    CHECK(rig.Position().x == Approx(1.0));
    std::ofstream(rig.Files.Root / "mover.luau", std::ios::trunc) << "return { OnTick = function(self) scene.set_xyz(self.entity, 'Transform', 'Position', 2, 0, 0) end }";
    rig.Module->RequestReload();
    rig.Runner->StepAndWait(1);
    CHECK(rig.Position().x == Approx(2.0));
}

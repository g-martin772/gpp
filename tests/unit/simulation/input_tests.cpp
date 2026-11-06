#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

import GPP;
import std;
import glm;

using namespace GPP;
using Catch::Approx;

namespace
{
    struct EchoModule final : ISimulationModule
    {
        std::uint64_t Target{0};
        std::shared_ptr<SimulationInput> Input;
        std::vector<InputFrame> Seen;

        void OnTick(Scene& scene, float dt) override
        {
            Seen.push_back(Input->Current());
            const auto entity = scene.FindByGuid(Target);
            if (!scene.IsValid(entity)) return;
            auto& position = scene.Registry().get<TransformComponent>(entity).Position;
            position.x += dt * Input->Axis("move_x") * 10.0f;
            position.y += Input->MouseDelta().y;
            if (Input->Pressed("jump")) position.z += 1.0;
            if (Input->Released("jump")) position.z += 0.25;
        }
    };

    Scene MakeScene(std::uint64_t& target)
    {
        Scene scene("Input");
        scene.SetSeed(5);
        const auto entity = scene.CreateEntity("Mover", "Box");
        scene.Registry().emplace<TransformComponent>(entity);
        target = scene.GuidOf(entity);
        return scene;
    }
}

TEST_CASE("Input frames latch taps between ticks and drop zeros", "[input]")
{
    SimulationInput input;
    input.Set("fire", 1.0f);
    input.Set("fire", 0.0f);
    input.Set("move_x", 0.25f);
    input.AddMouse({2.0f, -1.0f});
    input.AddMouse({1.0f, 4.0f});

    const auto first = input.Sample();
    CHECK(first.Pressed.contains("fire"));
    CHECK(first.Released.contains("fire"));
    CHECK_FALSE(first.Values.contains("fire"));
    CHECK(first.Values.at("move_x") == Approx(0.25f));
    CHECK(first.Pressed.size() == 1);
    CHECK(first.Mouse == glm::vec2(3.0f, 3.0f));
    CHECK(first.Scroll == 0.0f);

    const auto second = input.Sample();
    CHECK(second.Pressed.empty());
    CHECK(second.Released.empty());
    CHECK(second.Mouse == glm::vec2(0.0f));
    CHECK(second.Values == first.Values);

    input.AddScroll(1.0f);
    input.AddScroll(0.25f);
    input.AddScroll(std::numeric_limits<float>::infinity());
    const auto scrolled = input.Sample();
    CHECK(scrolled.Scroll == Approx(1.25f));
    CHECK(scrolled.HasEvents());
    CHECK(input.Sample().Scroll == 0.0f);
    input.AddScroll(2.0f);
    input.Begin(input.Sample());
    CHECK(input.Scroll() == Approx(2.0f));

    input.Set("move_x", std::numeric_limits<float>::quiet_NaN());
    CHECK(input.Sample().Values.empty());
}

TEST_CASE("Input queries read the frame begun for the tick and flag use", "[input]")
{
    SimulationInput input;
    CHECK_FALSE(input.Used());
    CHECK(input.Axis("move_x") == 0.0f);
    CHECK(input.Used());
    input.ResetUsed();
    CHECK_FALSE(input.Used());

    input.Set("jump", 1.0f);
    input.Set("move_x", 0.4f);
    CHECK_FALSE(input.Held("jump"));
    input.Begin(input.Sample());
    CHECK(input.Held("jump"));
    CHECK(input.Pressed("jump"));
    CHECK_FALSE(input.Held("move_x"));
    CHECK(input.Axis("move_x") == Approx(0.4f));
    input.Begin(input.Sample());
    CHECK_FALSE(input.Pressed("jump"));
    CHECK(input.Held("jump"));

    input.Clear();
    input.Begin(input.Sample());
    CHECK(input.Released("jump"));
    CHECK_FALSE(input.Held("jump"));
}

TEST_CASE("Action bindings parse, format and round trip through YAML", "[input][actions]")
{
    CHECK(ActionMap::ParseBinding("key:W")->Source == "key:w");
    CHECK(ActionMap::ParseBinding("-key:A")->Scale == -1.0f);
    CHECK(ActionMap::ParseBinding("0.5 * mouse:Left")->Scale == 0.5f);
    CHECK(ActionMap::ParseBinding("0.5*mouse:left")->Source == "mouse:left");
    CHECK_FALSE(ActionMap::ParseBinding("w").has_value());
    CHECK_FALSE(ActionMap::ParseBinding("x*key:w").has_value());
    CHECK_FALSE(ActionMap::ParseBinding("key:").has_value());
    CHECK(ActionMap::FormatBinding({"key:a", -1.0f}) == "-key:a");

    auto map = ActionMap::Defaults();
    map.Set("thrust", {{"key:W", 1.0f}, {"pad:righttrigger", 0.5f}});
    const auto loaded = ActionMap::FromYaml(map.ToYaml());
    REQUIRE(loaded.has_value());
    CHECK(*loaded == map);
    CHECK(loaded->Find("thrust")->Bindings[0].Source == "key:w");

    const auto scalar = ActionMap::FromYaml("Actions:\n  fire: mouse:left\n  none:\n");
    REQUIRE(scalar.has_value());
    CHECK(scalar->Find("fire")->Bindings.size() == 1);
    CHECK(scalar->Find("none")->Bindings.empty());
    CHECK_FALSE(ActionMap::FromYaml("Actions:\n  bad name: [key:a]\n").has_value());
    CHECK_FALSE(ActionMap::FromYaml("Actions:\n  fire: [nope]\n").has_value());
    CHECK_FALSE(ActionMap::FromYaml("Actions: [1]\n").has_value());
    CHECK(ActionMap::FromYaml("{}")->Empty());
}

TEST_CASE("An overlay replaces whole actions and keeps the rest", "[input][actions]")
{
    auto base = ActionMap::Defaults();
    ActionMap user;
    user.Set("fire", {{"key:F", 1.0f}});
    user.Set("extra", {{"key:x", 1.0f}});
    base.Overlay(user);
    CHECK(base.Find("fire")->Bindings == std::vector<InputBinding>{{"key:f", 1.0f}});
    CHECK(base.Find("extra") != nullptr);
    CHECK(base.Find("move_x")->Bindings.size() == 2);
    CHECK(base.Remove("extra"));
    CHECK_FALSE(base.Remove("extra"));
}

TEST_CASE("The router turns sources into clamped action values", "[input][actions]")
{
    auto target = std::make_shared<SimulationInput>();
    ActionRouter router;
    router.SetTarget(target);
    router.SetMap(ActionMap::Defaults());

    router.SetSource("key:W", 1.0f);
    router.SetSource("key:d", 1.0f);
    router.SetSource("key:a", 1.0f);
    target->Begin(target->Sample());
    CHECK(target->Axis("move_z") == 1.0f);
    CHECK(target->Axis("move_x") == 0.0f);
    CHECK(target->Pressed("move_z"));

    router.SetSource("key:a", 0.0f);
    router.SetSource("key:Shift", 1.0f);
    router.SetSource("mouse:left", 1.0f);
    target->Begin(target->Sample());
    CHECK(target->Axis("move_x") == 1.0f);
    CHECK(target->Held("boost"));
    CHECK(target->Held("fire"));

    router.AddMouse({4.0f, 1.0f});
    router.Release();
    target->Begin(target->Sample());
    CHECK(target->Released("move_z"));
    CHECK_FALSE(target->Held("move_z"));
    CHECK(target->MouseDelta() == glm::vec2(4.0f, 1.0f));

    router.SetSource("key:w", 1.0f);
    auto rebound = ActionMap::Defaults();
    rebound.Set("move_z", {{"key:up", 1.0f}});
    router.SetMap(rebound);
    target->Begin(target->Sample());
    CHECK_FALSE(target->Held("move_z"));
    router.SetSource("key:up", 1.0f);
    target->Begin(target->Sample());
    CHECK(target->Held("move_z"));
    CHECK(router.Map() == rebound);
}

TEST_CASE("The runner records sampled input and replays it to the same state", "[input][replay]")
{
    std::uint64_t target = 0;
    auto module = std::make_shared<EchoModule>();
    SimulationRunner runner(MakeScene(target), module);
    module->Target = target;
    module->Input = runner.Input();

    runner.Input()->Set("move_x", 1.0f);
    runner.StepAndWait(2);
    runner.BeginRecording(4);
    runner.StepAndWait(3);
    runner.Input()->Set("jump", 1.0f);
    runner.Input()->AddMouse({0.0f, 2.0f});
    runner.StepAndWait(2);
    runner.Input()->Set("jump", 0.0f);
    runner.Input()->Set("move_x", 0.0f);
    runner.StepAndWait(5);
    const auto recording = runner.EndRecording();

    REQUIRE(recording.Ticks() == 10);
    REQUIRE(recording.Inputs.size() == 3);
    CHECK(recording.Inputs[0].Tick == 2);
    CHECK(recording.Inputs[0].Frame.Values.at("move_x") == 1.0f);
    CHECK(recording.Inputs[1].Tick == 5);
    CHECK(recording.Inputs[1].Frame.Pressed.contains("jump"));
    CHECK(recording.Inputs[1].Frame.Mouse == glm::vec2(0.0f, 2.0f));
    CHECK(recording.Inputs[2].Tick == 7);
    CHECK(recording.Inputs[2].Frame.Released.contains("jump"));
    CHECK(recording.FinalHash.has_value());

    const auto verify = [&](const SimulationRecording& which)
    {
        auto replayed = std::make_shared<EchoModule>();
        replayed->Target = target;
        ReplayOptions options;
        options.OnRunnerCreated = [replayed](SimulationRunner& replayRunner) { replayed->Input = replayRunner.Input(); };
        return std::pair{VerifyReplay(which, {replayed}, options), replayed};
    };
    const auto [ok, replayed] = verify(recording);
    INFO(ok.Message);
    CHECK(ok.Ok);
    REQUIRE(replayed->Seen.size() == 10);
    CHECK(replayed->Seen[0].Values.at("move_x") == 1.0f);
    CHECK(replayed->Seen[3].Pressed.contains("jump"));
    CHECK(replayed->Seen[4].Pressed.empty());
    CHECK(replayed->Seen[4].Values.contains("jump"));
    CHECK(replayed->Seen[9].Values.empty());

    auto tampered = recording;
    tampered.Inputs.erase(tampered.Inputs.begin() + 1);
    CHECK_FALSE(verify(tampered).first.Ok);

    auto withoutInput = recording;
    withoutInput.Inputs.clear();
    CHECK_FALSE(verify(withoutInput).first.Ok);
}

TEST_CASE("Recorded input survives the file and keeps verifying", "[input][replay]")
{
    std::uint64_t target = 0;
    auto module = std::make_shared<EchoModule>();
    SimulationRunner runner(MakeScene(target), module);
    module->Target = target;
    module->Input = runner.Input();
    runner.BeginRecording(3);
    runner.Input()->Set("move_x", -0.75f);
    runner.Input()->Set("fire", 1.0f);
    runner.Input()->AddMouse({1.5f, -2.25f});
    runner.StepAndWait(4);
    runner.Input()->Set("fire", 0.0f);
    runner.StepAndWait(4);
    const auto recording = runner.EndRecording();
    REQUIRE(recording.Inputs.size() == 2);

    const auto text = RecordingToYaml(recording);
    CHECK(text.contains("Inputs:"));
    const auto loaded = RecordingFromYaml(text);
    REQUIRE(loaded.Inputs.size() == 2);
    CHECK(loaded.Inputs[0].Frame == recording.Inputs[0].Frame);
    CHECK(loaded.Inputs[1].Frame == recording.Inputs[1].Frame);
    CHECK(RecordingToYaml(loaded) == text);

    auto replayed = std::make_shared<EchoModule>();
    replayed->Target = target;
    ReplayOptions options;
    options.OnRunnerCreated = [replayed](SimulationRunner& replayRunner) { replayed->Input = replayRunner.Input(); };
    CHECK(VerifyReplay(loaded, {replayed}, options).Ok);
}

TEST_CASE("Scroll round trips through the recording and old frames load without it", "[input][replay]")
{
    std::uint64_t target = 0;
    auto module = std::make_shared<EchoModule>();
    SimulationRunner runner(MakeScene(target), module);
    module->Target = target;
    module->Input = runner.Input();
    runner.BeginRecording(3);
    runner.Input()->AddScroll(-3.5f);
    runner.StepAndWait(2);
    const auto recording = runner.EndRecording();
    REQUIRE(recording.Inputs.size() == 1);
    CHECK(recording.Inputs[0].Frame.Scroll == Approx(-3.5f));

    const auto text = RecordingToYaml(recording);
    CHECK(text.contains("Scroll:"));
    const auto loaded = RecordingFromYaml(text);
    REQUIRE(loaded.Inputs.size() == 1);
    CHECK(loaded.Inputs[0].Frame == recording.Inputs[0].Frame);

    auto old = text;
    const auto key = old.find("Scroll:");
    REQUIRE(key != std::string::npos);
    const auto at = old.rfind('\n', key) + 1;
    old.erase(at, old.find('\n', key) - at + 1);
    const auto legacy = RecordingFromYaml(old);
    REQUIRE(legacy.Inputs.size() == 1);
    CHECK(legacy.Inputs[0].Frame.Scroll == 0.0f);
}

TEST_CASE("Version 1 recordings without input still load", "[input][replay]")
{
    std::uint64_t target = 0;
    auto module = std::make_shared<EchoModule>();
    SimulationRunner runner(MakeScene(target), module);
    module->Input = runner.Input();
    runner.BeginRecording(2);
    runner.StepAndWait(4);
    auto yaml = RecordingToYaml(runner.EndRecording());
    const auto at = yaml.find("Version: 2");
    REQUIRE(at != std::string::npos);
    yaml.replace(at, 10, "Version: 1");
    const auto inputs = yaml.find("\nInputs:");
    REQUIRE(inputs != std::string::npos);
    const auto next = yaml.find("\nInitial:");
    REQUIRE(next != std::string::npos);
    yaml.erase(inputs, next - inputs);
    CHECK(RecordingFromYaml(yaml).Inputs.empty());
}

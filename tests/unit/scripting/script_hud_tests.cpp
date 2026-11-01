#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

import GPP;
import std;

using namespace GPP;
using Catch::Approx;
namespace fs = std::filesystem;

namespace
{
    constexpr const char* kSceneYaml = R"(Scene: Hud
SchemaVersion: 2
Entities:
  - Guid: 5
    Components:
      Metadata: {Guid: 5, Name: Pen, TypeTag: Entity}
      Transform: {Position: [1, 0, 0]}
      Scripts:
        - Name: pen
)";

    struct Rig
    {
        fs::path Root = fs::temp_directory_path() / "gpp_script_hud_tests";
        std::unique_ptr<AssetDirectories> Assets;

        explicit Rig(const std::string& script)
        {
            fs::remove_all(Root);
            fs::create_directories(Root);
            std::ofstream(Root / "pen.luau") << script;
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
            return RunHeadless(std::move(scene), {module}, {.Ticks = ticks, .Dt = 0.01, .Seed = 3,
                                                          .OnRunnerCreated = [&](SimulationRunner& runner) { module->SetRandom(runner.Random()); }});
        }
    };

    struct Live
    {
        Rig Files;
        Scene World;
        std::shared_ptr<ScriptSimulationModule> Module;

        explicit Live(const std::string& script) : Files(script)
        {
            RegisterBaseComponents();
            RegisterScriptComponents();
            World.DeserializeFromYaml(kSceneYaml);
            Module = std::make_shared<ScriptSimulationModule>(*Files.Assets);
            Module->OnInit(World);
        }

        void Tick(const int count = 1)
        {
            for (int i = 0; i < count; ++i)
            {
                Module->OnTick(World, 0.01f);
                Module->OnLateTick(World, 0.01f);
            }
        }
    };

    constexpr const char* kPen = R"(
return {
    OnTick = function(self, dt)
        ui.hud.text(0.5, 0.25, "speed", { size = 20, align = "center", color = "#ff8000" })
        ui.hud.line(0, 0, 1, 1, { thickness = 3, color = { r = 0.1, g = 0.2, b = 0.3, a = 0.4 } })
        ui.hud.at("left").circle(0.1, 0.2, 0.05, { filled = true })
        ui.hud.arc(0.5, 0.5, 0.1, -90, 90)
        ui.hud.rect(0.1, 0.1, 0.2, 0.3)
    end,
})";

    constexpr const char* kFlood = R"(
return {
    OnTick = function(self, dt)
        for i = 1, 5000 do
            ui.hud.line(0, 0, 1, 1)
        end
    end,
})";
}

TEST_CASE("Script HUD commands reach the published frame", "[scripting][hud]")
{
    Live live(kPen);
    live.Tick(3);
    CHECK(live.Module->TakeErrors().empty());
    const auto frame = live.Module->HudSnapshot();
    REQUIRE(frame->Commands.size() == 5);
    const auto& text = frame->Commands[0];
    CHECK(text.Kind == HudKind::Text);
    CHECK(text.Text == "speed");
    CHECK(text.Align == HudAlign::Center);
    CHECK(text.Size == Approx(20.0f));
    CHECK(text.Color[0] == Approx(1.0f));
    CHECK(text.Color[1] == Approx(128.0f / 255.0f));
    CHECK(text.Color[2] == Approx(0.0f));
    const auto& line = frame->Commands[1];
    CHECK(line.Kind == HudKind::Line);
    CHECK(line.Thickness == Approx(3.0f));
    CHECK(line.Color[3] == Approx(0.4f));
    const auto& circle = frame->Commands[2];
    CHECK(circle.Kind == HudKind::Circle);
    CHECK(circle.Space == HudSpace::Left);
    CHECK(circle.Filled);
    CHECK(frame->Commands[3].Kind == HudKind::Arc);
    CHECK(frame->Commands[4].Kind == HudKind::Rect);
}


TEST_CASE("Script HUD commands do not change the state hash", "[scripting][hud][determinism]")
{
    Rig drawing(kPen);
    std::shared_ptr<ScriptSimulationModule> first;
    const auto with = drawing.Run(first, 20);
    Rig quiet("return { OnTick = function(self, dt) end }");
    std::shared_ptr<ScriptSimulationModule> second;
    const auto without = quiet.Run(second, 20);
    REQUIRE(with.has_value());
    REQUIRE(without.has_value());
    CHECK(with->Hash == without->Hash);
}

TEST_CASE("More than the command limit in one frame is a script error", "[scripting][hud]")
{
    Rig rig(kFlood);
    std::shared_ptr<ScriptSimulationModule> module;
    const auto result = rig.Run(module, 2);
    REQUIRE(result.has_value());
    const auto errors = module->TakeErrors();
    REQUIRE_FALSE(errors.empty());
    CHECK(errors.front().Message.find("more than 4096 commands") != std::string::npos);
    CHECK(module->HudSnapshot()->Commands.size() <= kHudCommandLimit);
}

TEST_CASE("The HUD is cleared when the scripts stop and each tick starts empty", "[scripting][hud]")
{
    Rig rig(kPen);
    std::shared_ptr<ScriptSimulationModule> module;
    REQUIRE(rig.Run(module, 5).has_value());
    CHECK(module->HudSnapshot()->Commands.empty());

    Live live(kPen);
    live.Tick();
    CHECK(live.Module->HudSnapshot()->Commands.size() == 5);
    live.Tick();
    CHECK(live.Module->HudSnapshot()->Commands.size() == 5);
    live.Module->Stop(live.World);
    CHECK(live.Module->HudSnapshot()->Commands.empty());
}

TEST_CASE("Script HUD bad arguments are readable script errors", "[scripting][hud]")
{
    for (const char* call : {"ui.hud.text(0, 0, 'x', { align = 'middle' })", "ui.hud.line(0, 0, 1, 0/0)",
                             "ui.hud.circle(0, 0, 1, { color = '#12' })", "ui.hud.rect(0, 0, 1, 1, { space = 'up' })"})
    {
        Rig rig(std::string("return { OnTick = function(self, dt) ") + call + " end }");
        std::shared_ptr<ScriptSimulationModule> module;
        REQUIRE(rig.Run(module, 1).has_value());
        const auto errors = module->TakeErrors();
        INFO(call);
        REQUIRE_FALSE(errors.empty());
        CHECK(errors.front().Message.find("ui.hud.") != std::string::npos);
    }
}

TEST_CASE("The HUD snapshot is safe to read while the sim thread publishes", "[scripting][hud][threading]")
{
    HudCanvas canvas;
    std::atomic<bool> done{false};
    std::atomic<std::size_t> torn{0};
    std::thread reader([&]
    {
        while (!done.load())
        {
            const auto frame = canvas.Snapshot();
            for (const auto& command : frame->Commands)
            {
                if (command.X != frame->Commands.front().X || frame->Commands.size() != 32) torn.fetch_add(1);
            }
        }
    });
    for (int tick = 1; tick <= 3000; ++tick)
    {
        canvas.Reset();
        for (int i = 0; i < 32; ++i)
        {
            HudCommand command;
            command.X = static_cast<float>(tick);
            canvas.Add(std::move(command));
        }
        canvas.Publish();
    }
    done.store(true);
    reader.join();
    CHECK(torn.load() == 0);
    CHECK(canvas.Snapshot()->Commands.size() == 32);
    canvas.Clear();
    CHECK(canvas.Snapshot()->Commands.empty());
}

TEST_CASE("The script test runner exposes the HUD as sim.hud()", "[scripting][hud][test-runner]")
{
    Rig rig("return {}");
    ScriptTestOptions options;
    options.Assets = rig.Assets.get();
    const auto file = RunScriptTestSource(R"(
test("hud commands come back as tables", function()
    sim.define_script("pen", "return { OnTick = function(self, dt) ui.hud.text(0.5, 0.1, 'hi', { size = 30 }) ui.hud.circle(0.2, 0.3, 0.1, { filled = true, color = 'red' }) end }")
    local e = sim.create("Pen")
    sim.attach(e, "pen")
    assert_eq(#sim.hud(), 0)
    sim.step(2)
    local hud = sim.hud()
    assert_eq(#hud, 2)
    assert_eq(hud[1].kind, "text")
    assert_eq(hud[1].text, "hi")
    assert_eq(hud[1].size, 30)
    assert_near(hud[1].y, 0.1, 1e-6)
    assert_eq(hud[2].kind, "circle")
    assert(hud[2].filled)
    assert_near(hud[2].color.g, 0.25, 1e-6)
    sim.stop()
    assert_eq(#sim.hud(), 0)
end)
)", "hud.luau", options);
    INFO(FormatScriptTestReport({.Files = {file}}));
    CHECK(file.Ok());
}

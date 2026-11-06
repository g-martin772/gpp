#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

import GPP;
import std;

using namespace GPP;
using Catch::Approx;
namespace fs = std::filesystem;

namespace
{
    struct ScriptRoot
    {
        fs::path Root;
        AssetOptions Options;
        std::unique_ptr<AssetDirectories> Assets;

        explicit ScriptRoot(const std::string& name)
        {
            Root = fs::temp_directory_path() / ("gpp_script_tests_" + name);
            fs::remove_all(Root);
            fs::create_directories(Root / "lib");
            Options.Kinds["Scripts"] = AssetKindOptions{{Root}, {".luau"}};
            Assets = std::make_unique<AssetDirectories>(Options);
        }

        ~ScriptRoot() { fs::remove_all(Root); }

        void Write(const std::string& script, const std::string& source) const
        {
            const auto path = Root / (script + ".luau");
            fs::create_directories(path.parent_path());
            std::ofstream(path, std::ios::trunc) << source;
        }

        [[nodiscard]] ScriptTestOptions Run(const LuauLimits limits = {.MaxSafepoints = 100'000'000, .MaxSeconds = 10.0}) const
        {
            ScriptTestOptions options;
            options.Assets = Assets.get();
            options.Limits = limits;
            return options;
        }
    };

    const ScriptTestResult& Find(const ScriptTestFile& file, const std::string& name)
    {
        const auto it = std::ranges::find(file.Tests, name, &ScriptTestResult::Name);
        REQUIRE(it != file.Tests.end());
        return *it;
    }

    constexpr const char* kMover = R"(
return {
    properties = { { name = "Speed", type = "float", default = 2 } },
    OnTick = function(self, dt)
        local x, y, z = scene.get_xyz(self.entity, "Transform", "Position")
        scene.set_xyz(self.entity, "Transform", "Position", x + self.props.Speed * dt, y, z)
    end,
})";
}

TEST_CASE("A passing file passes every test", "[scripting][test-runner]")
{
    ScriptRoot root("pass");
    const auto file = RunScriptTestSource(R"(
test("arithmetic", function()
    assert_eq(1 + 1, 2)
    assert_near(0.1 + 0.2, 0.3, 1e-9)
    assert_rel(1e9 + 1, 1e9, 1e-6)
    assert_eq({1, {a = 2}}, {1, {a = 2}})
    assert_near(vector.create(1, 2, 3), vector.create(1, 2, 3.0000001), 1e-4)
    assert(true)
end)
test("errors", function()
    local message = expect_error(function() error("boom") end, "boom")
    assert_eq(type(message), "string")
    expect_error(function() local x = nil; return x.y end)
end)
)", "pass.luau", root.Run());

    INFO(FormatScriptTestReport({.Files = {file}}));
    CHECK(file.Ok());
    CHECK(file.Tests.size() == 2);
    CHECK(file.Tests[0].Name == "arithmetic");
}

TEST_CASE("A failing assertion is reported with file and line", "[scripting][test-runner]")
{
    ScriptRoot root("fail");
    const auto file = RunScriptTestSource(R"(
test("good", function() assert_eq(1, 1) end)
test("eq", function()
    assert_eq(1 + 1, 3)
end)
test("near", function()
    assert_near(1, 2, 0.5, "distance")
end)
test("no error", function()
    expect_error(function() end)
end)
test("wrong error", function()
    expect_error(function() error("apples") end, "pears")
end)
test("plain assert", function()
    assert(false, "custom text")
end)
local function helper()
    error("kaboom")
end
test("raised", function()
    helper()
end)
)", "tests/fail.luau", root.Run());

    CHECK_FALSE(file.Ok());
    CHECK(Find(file, "good").Passed);

    const auto& eq = Find(file, "eq");
    CHECK_FALSE(eq.Passed);
    CHECK(eq.Line == 4);
    CHECK(eq.Trace.empty());
    CHECK(eq.Message == "tests/fail.luau:4: expected 3, got 2");

    CHECK(Find(file, "near").Message == "tests/fail.luau:7: distance: expected 2 within 0.5, got 1 (off by 1)");
    CHECK(Find(file, "no error").Message.find("tests/fail.luau:10: expected an error") == 0);
    CHECK(Find(file, "wrong error").Message.find("tests/fail.luau:13: expected an error containing \"pears\"") == 0);
    CHECK(Find(file, "plain assert").Message == "tests/fail.luau:16: custom text");
    CHECK(Find(file, "raised").Message == "tests/fail.luau:19: kaboom");
    const auto& trace = Find(file, "raised").Trace;
    CHECK(std::ranges::any_of(trace, [](const std::string& line) { return line.starts_with("tests/fail.luau:22"); }));

    const auto report = FormatScriptTestReport({.Files = {file}, .Passed = 1, .Failed = 6});
    CHECK(report.find("tests/fail.luau:4: expected 3, got 2\n    in test 'eq' of tests/fail.luau") != std::string::npos);
    CHECK(report.find("1 passed, 6 failed in 1 file") != std::string::npos);
}

TEST_CASE("Load problems are reported for the file", "[scripting][test-runner]")
{
    ScriptRoot root("load");
    const auto syntax = RunScriptTestSource("test('x', function()\n  local = 1\nend)", "syntax.luau", root.Run());
    CHECK_FALSE(syntax.Ok());
    CHECK(syntax.LoadLine == 2);
    CHECK(syntax.LoadError.starts_with("syntax.luau:2:"));

    const auto top = RunScriptTestSource("local x = nil\nx.y = 1", "top.luau", root.Run());
    CHECK(top.LoadError.starts_with("top.luau:2:"));

    const auto none = RunScriptTestSource("local x = 1", "none.luau", root.Run());
    CHECK(none.LoadError.find("no tests registered") != std::string::npos);

    const auto missing = RunScriptTestFile(root.Root / "nothing.luau", root.Run());
    CHECK(missing.LoadError.find("cannot read") != std::string::npos);
}

TEST_CASE("A runaway test is stopped and the next one still runs", "[scripting][test-runner][limits]")
{
    ScriptRoot root("limits");
    const auto file = RunScriptTestSource(R"(
test("spin", function() while true do end end)
test("after", function() assert_eq(1, 1) end)
)", "spin.luau", root.Run({.MaxSafepoints = 100'000, .MaxSeconds = 5.0}));

    CHECK_FALSE(Find(file, "spin").Passed);
    CHECK(Find(file, "spin").Message.find("instruction limit") != std::string::npos);
    CHECK(Find(file, "after").Passed);
}

TEST_CASE("Tests step attached scripts and assert on component values", "[scripting][test-runner][sim]")
{
    ScriptRoot root("sim");
    root.Write("mover", kMover);
    const auto file = RunScriptTestSource(R"(
test("moves with the sim clock", function()
    local e = sim.spawn("Mover", { Transform = { Position = vector.create(1, 0, 0) } })
    sim.attach(e, "mover", { Speed = 3 })
    sim.step(60)
    assert_eq(sim.ticks(), 60)
    assert_near(time.elapsed(), 1, 1e-9)
    assert_near(scene.get(e, "Transform", "Position"), vector.create(4, 0, 0), 1e-4)
    sim.set_dt(0.5)
    sim.step(2)
    assert_near(time.elapsed(), 2, 1e-9)
    assert_near(scene.get(e, "Transform", "Position"), vector.create(7, 0, 0), 1e-4)
end)
test("worlds do not leak", function()
    assert_eq(#scene.query("Scripts"), 0)
    assert_eq(sim.ticks(), 0)
    assert_eq(time.elapsed(), 0)
end)
test("unknown component", function()
    expect_error(function() sim.add_component(sim.create("x"), "Nope") end, "unknown component 'Nope'")
end)
)", "sim.luau", root.Run());
    INFO(FormatScriptTestReport({.Files = {file}}));
    CHECK(file.Ok());
}

TEST_CASE("Script errors during a step fail the test with the script location", "[scripting][test-runner][sim]")
{
    ScriptRoot root("scripterror");
    root.Write("broken", "return {\n  OnTick = function(self, dt)\n    local x = nil\n    return x.field\n  end,\n}");
    const auto file = RunScriptTestSource(R"(
test("broken script", function()
    local e = sim.spawn("Cam")
    sim.attach(e, "broken")
    sim.step(1)
end)
test("expected", function()
    sim.attach(sim.spawn("Cam"), "broken")
    expect_error(function() sim.step(1) end, "broken:4:")
end)
)", "scripterror.luau", root.Run());

    const auto& broken = Find(file, "broken script");
    CHECK_FALSE(broken.Passed);
    CHECK(broken.Message.starts_with("scripterror.luau:5: broken:4:"));
    CHECK(broken.Message.find("[script 'broken' on entity 'Cam']") != std::string::npos);
    CHECK(Find(file, "expected").Passed);
}

TEST_CASE("The seeded RNG and sim clock make a test repeatable", "[scripting][test-runner][determinism]")
{
    ScriptRoot root("rng");
    root.Write("lib/dice", "local dice = {}\nfunction dice.roll() return math.random(1, 1000) end\nreturn dice");
    root.Write("roller", R"(
return {
    OnTick = function(self, dt)
        scene.set(self.entity, "Transform", "Position", vector.create(math.random(), time.elapsed(), 0))
    end,
})");
    std::vector<double> captured;
    const auto run = [&](const std::string& source, const std::uint64_t seed)
    {
        captured.clear();
        auto options = root.Run();
        options.Seed = seed;
        options.Extend = [&captured](LuauVm& vm)
        {
            vm.Register("probe", "record", [&captured](LuauNativeCall& call)
            {
                captured.push_back(call.Number(1));
                return 0;
            });
        };
        const auto file = RunScriptTestSource(source, "rng.luau", options);
        INFO(FormatScriptTestReport({.Files = {file}}));
        CHECK(file.Ok());
        return captured;
    };

    const std::string scripts = R"(
test("scripts draw from the seed", function()
    local e = sim.spawn("Roller")
    sim.attach(e, "roller")
    sim.step(5)
    local p = scene.get(e, "Transform", "Position")
    probe.record(p.x)
    probe.record(p.y)
    probe.record(sim.state_hash() == sim.state_hash() and 1 or 0)
end)
)";
    const auto first = run(scripts, 5);
    REQUIRE(first.size() == 3);
    CHECK(first[1] == Approx(4.0 / 60.0).margin(1e-6));
    CHECK(run(scripts, 5) == first);
    CHECK(run(scripts, 6)[0] != first[0]);

    const auto reseed = run(R"(
local dice = require("lib/dice")
test("sim.seed restarts the stream", function()
    sim.seed(7)
    local a = { dice.roll(), dice.roll() }
    sim.seed(7)
    assert_eq({ dice.roll(), dice.roll() }, a)
    sim.seed(8)
    assert_ne({ dice.roll(), dice.roll() }, a)
end)
test("options.seed seeds every world", function()
    local first = dice.roll()
    sim.seed(5)
    assert_eq(dice.roll(), first)
end)
)", 5);
}

TEST_CASE("Test files are discovered recursively and in order", "[scripting][test-runner]")
{
    ScriptRoot root("find");
    root.Write("b_second", "test('b', function() end)");
    root.Write("a_first", "test('a', function() end)");
    root.Write("sub/c_third", "test('c', function() end)");
    std::ofstream(root.Root / "notes.txt") << "x";

    const auto files = FindScriptTests(root.Root);
    REQUIRE(files.size() == 3);
    CHECK(files[0].filename() == "a_first.luau");
    CHECK(files[2].filename() == "c_third.luau");
    CHECK(FindScriptTests(files[0]) == std::vector<fs::path>{files[0]});

    auto options = root.Run();
    options.DisplayRoot = root.Root;
    const auto report = RunScriptTests(files, options);
    CHECK(report.Ok());
    CHECK(report.Passed == 3);
    CHECK(report.Files[2].Path == "sub/c_third.luau");
    CHECK(FormatScriptTestReport(report, true).find("PASS sub/c_third.luau: c") != std::string::npos);
    CHECK_FALSE(ScriptTestReport{}.Ok());
}

TEST_CASE("Script tests extend the scene bindings for the test world and script components", "[scripting][script-test]")
{
    ScriptRoot root("extend_scene");
    root.Write("probe", R"(
return {
    OnTick = function(self, dt)
        scene.set(self.entity, "Transform", "Position", vector.create(time.rate(self.entity), 0, 0))
    end,
})");
    auto options = root.Run();
    options.ExtendScene = [](LuauVm& vm, const std::shared_ptr<SceneBindings>& bindings)
    {
        vm.Register("time", "rate", [bindings](LuauNativeCall& call)
        {
            call.PushNumber(bindings->Read && bindings->Read->IsValid(bindings->Read->FindByGuid(call.Entity(1))) ? 0.5 : -1.0);
            return 1;
        });
    };
    const auto file = RunScriptTestSource(R"(
test("both vms see the extension", function()
    local e = sim.spawn("Probe")
    assert(time.rate(e) == 0.5)
    sim.attach(e, "probe")
    sim.step(2)
    assert(scene.get(e, "Transform", "Position").x == 0.5)
end)
)", "extend.luau", options);
    INFO(FormatScriptTestReport({.Files = {file}}, true));
    CHECK(file.Ok());
}

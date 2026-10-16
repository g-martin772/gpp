#include <catch2/catch_test_macros.hpp>
#include <yaml-cpp/yaml.h>

import GPP;
import std;

using namespace GPP;

namespace
{
    enum class Quality : std::uint8_t { Draft, Final };

    struct Demo
    {
        bool Enabled{true};
        int Steps{100};
        float Tolerance{0.1f};
        Quality Level{Quality::Draft};
    };

    SettingRegistry<Demo> MakeRegistry()
    {
        SettingRegistry<Demo> registry;
        registry.Add(Setting("a.enabled", &Demo::Enabled, {.Label = "Enabled", .Group = "A"}));
        registry.Add(Setting("a.steps", &Demo::Steps,
                             {.Label = "Steps", .Group = "A", .Min = 10, .Max = 1000, .Cost = 3}));
        registry.Add(Setting("b.tolerance", &Demo::Tolerance,
                             {.Label = "Tolerance", .Group = "B", .Min = 0.01, .Max = 1.0,
                              .Scale = SettingScale::Log, .VisibleWhen = "a.enabled"}));
        registry.Add(Setting("b.level", &Demo::Level,
                             {.Label = "Level", .Group = "B", .Options = {"Draft", "Final"},
                              .OptionRequires = {"", "fp64"}, .OptionCosts = {1, 4}}));
        return registry;
    }
}

TEST_CASE("Settings are found by key and listed per group in declaration order", "[settings]")
{
    const auto registry = MakeRegistry();
    CHECK(registry.All().size() == 4);
    CHECK(registry.Groups() == std::vector<std::string>{"A", "B"});
    CHECK(registry.InGroup("B").size() == 2);
    REQUIRE(registry.Find("a.steps"));
    CHECK(registry.Find("a.steps")->Meta.Cost == 3);
    CHECK_FALSE(registry.Find("nope"));
}

TEST_CASE("Duplicate keys are rejected", "[settings]")
{
    auto registry = MakeRegistry();
    CHECK_THROWS_AS(registry.Add(Setting("a.enabled", &Demo::Enabled)), std::logic_error);
}

TEST_CASE("Settings round-trip through YAML and only differing keys are written against a baseline", "[settings]")
{
    const auto registry = MakeRegistry();
    Demo value;
    value.Steps = 250;
    value.Level = Quality::Final;

    const auto full = registry.Encode(value);
    CHECK(full.size() == 4);
    CHECK(full["b.level"].as<std::string>() == "Final");

    const Demo baseline;
    const auto sparse = registry.Encode(value, &baseline);
    CHECK(sparse.size() == 2);
    CHECK(sparse["a.steps"].as<int>() == 250);

    Demo loaded;
    const auto unknown = registry.Decode(loaded, YAML::Load(YAML::Dump(sparse)));
    CHECK(unknown.empty());
    CHECK(loaded.Steps == 250);
    CHECK(loaded.Level == Quality::Final);
    CHECK(loaded.Enabled);
}

TEST_CASE("Decoding reports unknown keys and clamps out-of-range values", "[settings]")
{
    const auto registry = MakeRegistry();
    Demo value;
    const auto unknown = registry.Decode(value, YAML::Load("{a.steps: 1000000, b.tolerance: 0.0, junk: 1}"));
    CHECK(unknown == std::vector<std::string>{"junk"});
    CHECK(value.Steps == 1000);
    CHECK(value.Tolerance == 0.01f);
}

TEST_CASE("SetFromText parses by type and reports errors", "[settings]")
{
    const auto registry = MakeRegistry();
    Demo value;
    CHECK(registry.SetFromText(value, "a.enabled", "off"));
    CHECK_FALSE(value.Enabled);
    CHECK(registry.SetFromText(value, "a.steps", "5"));
    CHECK(value.Steps == 10);
    CHECK(registry.SetFromText(value, "b.tolerance", "0.25"));
    CHECK(value.Tolerance == 0.25f);
    CHECK(registry.SetFromText(value, "b.level", "final"));
    CHECK(value.Level == Quality::Final);
    CHECK(registry.SetFromText(value, "b.level", "0"));
    CHECK(value.Level == Quality::Draft);

    CHECK_FALSE(registry.SetFromText(value, "a.enabled", "maybe"));
    CHECK_FALSE(registry.SetFromText(value, "a.steps", "1.5"));
    CHECK_FALSE(registry.SetFromText(value, "b.level", "ultra"));
    const auto unknown = registry.SetFromText(value, "x.y", "1");
    REQUIRE_FALSE(unknown);
    CHECK(unknown.error().find("unknown setting") != std::string::npos);
}

TEST_CASE("Per-option cost and capability follow the selected option", "[settings]")
{
    const auto registry = MakeRegistry();
    const auto* level = registry.Find("b.level");
    REQUIRE(level);
    Demo value;
    CHECK(level->CostFor(value) == 1);
    CHECK(level->RequiresFor(value).empty());
    value.Level = Quality::Final;
    CHECK(level->CostFor(value) == 4);
    CHECK(level->RequiresFor(value) == "fp64");
    CHECK(registry.Find("a.steps")->CostFor(value) == 3);
}

TEST_CASE("Diff lists the keys that differ", "[settings]")
{
    const auto registry = MakeRegistry();
    Demo a;
    Demo b = a;
    CHECK(registry.Diff(a, b).empty());
    b.Tolerance = 0.5f;
    b.Enabled = false;
    CHECK(registry.Diff(a, b) == std::vector<std::string>{"a.enabled", "b.tolerance"});
}

TEST_CASE("ToText renders enum labels and plain numbers", "[settings]")
{
    const auto registry = MakeRegistry();
    Demo value;
    value.Level = Quality::Final;
    CHECK(registry.ToText(value, *registry.Find("b.level")) == "Final");
    CHECK(registry.ToText(value, *registry.Find("a.steps")) == "100");
    CHECK(registry.ToText(value, *registry.Find("a.enabled")) == "true");
}

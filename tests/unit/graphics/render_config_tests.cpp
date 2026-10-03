#include <catch2/catch_test_macros.hpp>

import GPP;
import std;

using namespace GPP;

namespace
{
    std::filesystem::path MakeTempJsonPath(const std::string& prefix)
    {
        const auto ts = std::chrono::steady_clock::now().time_since_epoch().count();
        return std::filesystem::temp_directory_path() / (prefix + std::to_string(ts) + ".json");
    }

    std::unique_ptr<IConfiguration> LoadJson(const std::filesystem::path& path, const std::string& json)
    {
        std::ofstream out(path);
        out << json;
        out.close();
        return ConfigurationBuilder().AddJsonFile(path.string()).Build();
    }
}

TEST_CASE("ImGuiOptions::FromConfig parses config flags and dockspace", "[graphics][config][imgui]")
{
    const auto path = MakeTempJsonPath("gpp_imgui_config_");
    const auto config = LoadJson(path, R"({
  "ImGui": {
    "ConfigFlags": ["DockingEnable", "ViewportsEnable"],
    "EnableDockSpace": true
  }
})");

    const auto options = ImGuiOptions::FromConfig(*config->GetSection("ImGui"));
    REQUIRE(options.ConfigFlags.size() == 2);
    CHECK(options.ConfigFlags[0] == "DockingEnable");
    CHECK(options.ConfigFlags[1] == "ViewportsEnable");
    CHECK(options.EnableDockSpace);

    CHECK(ParseImGuiConfigFlags(options.ConfigFlags) ==
          (ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_ViewportsEnable));
    CHECK(ParseImGuiConfigFlags({"NotARealFlag"}) == ImGuiConfigFlags_None);

    std::filesystem::remove(path);
}

TEST_CASE("ImGuiOptions::FromConfig defaults to no flags and no dockspace", "[graphics][config][imgui]")
{
    const auto path = MakeTempJsonPath("gpp_imgui_defaults_");
    const auto config = LoadJson(path, R"({"Other": {}})");

    const auto options = ImGuiOptions::FromConfig(*config->GetSection("ImGui"));
    CHECK(options.ConfigFlags.empty());
    CHECK_FALSE(options.EnableDockSpace);

    std::filesystem::remove(path);
}

TEST_CASE("ThemeOptions::FromConfig parses the hot-reload .so registration path", "[graphics][config][theme]")
{
    const auto path = MakeTempJsonPath("gpp_theme_config_");
    const auto config = LoadJson(path, R"({
  "Theme": {
    "LibraryPath": "themes/my_theme.so",
    "EnableHotReload": true,
    "PollingIntervalMilliseconds": 500
  }
})");

    const auto options = ThemeOptions::FromConfig(*config->GetSection("Theme"));
    CHECK(options.LibraryPath == "themes/my_theme.so");
    CHECK(options.EnableHotReload);
    CHECK(options.PollingInterval == std::chrono::milliseconds(500));

    std::filesystem::remove(path);
}

TEST_CASE("FontOptions::FromConfig parses font directories, default font and UI scale",
          "[graphics][config][fonts]")
{
    const auto path = MakeTempJsonPath("gpp_fonts_config_");
    const auto config = LoadJson(path, R"({
  "Fonts": {
    "FontDirectories": ["fonts", "more-fonts"],
    "DefaultFont": "Roboto-Regular",
    "DefaultFontSize": 18.0,
    "UiScale": 1.5
  }
})");

    const auto options = FontOptions::FromConfig(*config->GetSection("Fonts"));
    REQUIRE(options.FontDirectories.size() == 2);
    CHECK(options.FontDirectories[0] == "fonts");
    CHECK(options.FontDirectories[1] == "more-fonts");
    CHECK(options.DefaultFont == "Roboto-Regular");
    CHECK(options.DefaultFontSize == 18.0f);
    CHECK(options.UiScale == 1.5f);

    std::filesystem::remove(path);
}

TEST_CASE("FontOptions::FromConfig defaults to a single \"fonts\" directory", "[graphics][config][fonts]")
{
    const auto path = MakeTempJsonPath("gpp_fonts_defaults_");
    const auto config = LoadJson(path, R"({"Other": {}})");

    const auto options = FontOptions::FromConfig(*config->GetSection("Fonts"));
    REQUIRE(options.FontDirectories.size() == 1);
    CHECK(options.FontDirectories[0] == "fonts");
    CHECK(options.DefaultFont.empty());
    CHECK(options.DefaultFontSize == 16.0f);
    CHECK(options.UiScale == 1.0f);

    std::filesystem::remove(path);
}

TEST_CASE("WindowOptions::FromConfig parses a per-window ImGui override", "[graphics][config][window]")
{
    const auto path = MakeTempJsonPath("gpp_window_imgui_config_");
    const auto config = LoadJson(path, R"({
  "Window": {
    "Width": 800,
    "Height": 600,
    "ImGui": {
      "ConfigFlags": ["DockingEnable"],
      "EnableDockSpace": true
    }
  }
})");

    const auto options = WindowOptions::FromConfig(*config->GetSection("Window"));
    REQUIRE(options.ImGuiConfigFlags.has_value());
    REQUIRE(options.ImGuiConfigFlags->size() == 1);
    CHECK((*options.ImGuiConfigFlags)[0] == "DockingEnable");
    REQUIRE(options.ImGuiDockSpace.has_value());
    CHECK(*options.ImGuiDockSpace);

    std::filesystem::remove(path);
}

TEST_CASE("WindowOptions::FromConfig leaves ImGui overrides unset when absent", "[graphics][config][window]")
{
    const auto path = MakeTempJsonPath("gpp_window_no_imgui_config_");
    const auto config = LoadJson(path, R"({"Window": {"Width": 800, "Height": 600}})");

    const auto options = WindowOptions::FromConfig(*config->GetSection("Window"));
    CHECK_FALSE(options.ImGuiConfigFlags.has_value());
    CHECK_FALSE(options.ImGuiDockSpace.has_value());

    std::filesystem::remove(path);
}

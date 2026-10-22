#include <catch2/catch_test_macros.hpp>
#include <cstdlib>

import GPP;
import std;

using namespace GPP;
namespace fs = std::filesystem;

namespace
{
    EnvLookup MakeEnv(std::unordered_map<std::string, std::string> values)
    {
        return [values = std::move(values)](std::string_view name) -> std::optional<std::string>
        {
            if (const auto it = values.find(std::string(name)); it != values.end())
            {
                return it->second;
            }
            return std::nullopt;
        };
    }

    struct TempTree
    {
        fs::path Root;

        TempTree()
        {
            Root = fs::temp_directory_path() /
                ("gpp_desktop_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            fs::create_directories(Root);
        }

        ~TempTree()
        {
            std::error_code error;
            fs::remove_all(Root, error);
        }
    };

    struct FakePrompter final : IWmPrompter
    {
        bool Interactive{true};
        bool Answer{true};
        int Asked{0};
        std::string LastQuestion;

        bool IsInteractive() const override { return Interactive; }

        bool Confirm(const std::string_view question) override
        {
            ++Asked;
            LastQuestion = question;
            return Answer;
        }
    };

    AppInfo MakeInfo()
    {
        AppInfo info;
        info.Id = "demo";
        info.Name = "Demo App";
        info.Summary = "A demo";
        info.Categories = "Science;Education";
        info.IconFile = "demo.svg";
        return info;
    }

    void Write(const fs::path& path, const std::string& text)
    {
        fs::create_directories(path.parent_path());
        std::ofstream(path) << text;
    }

    std::string Read(const fs::path& path)
    {
        std::ifstream file(path);
        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }

    std::size_t Count(const std::string& text, const std::string& needle)
    {
        std::size_t count = 0;
        for (auto pos = text.find(needle); pos != std::string::npos; pos = text.find(needle, pos + needle.size()))
            ++count;
        return count;
    }

    constexpr std::string_view Svg = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"8\" height=\"8\"/>";
}

TEST_CASE("Desktop entry contains the app metadata", "[platform][desktop]")
{
    const auto text = DesktopEntry::Build(MakeInfo(), "/home/u/.local/bin/demo");
    CHECK(text.starts_with("[Desktop Entry]\nType=Application\n"));
    CHECK(text.contains("Name=Demo App\n"));
    CHECK(text.contains("Comment=A demo\n"));
    CHECK(text.contains("Exec=/home/u/.local/bin/demo %f\n"));
    CHECK(text.contains("Icon=demo\n"));
    CHECK(text.contains("Terminal=false\n"));
    CHECK(text.contains("Categories=Science;Education;\n"));
    CHECK(text.contains("StartupWMClass=demo\n"));
    CHECK(text.contains("StartupNotify=true\n"));
    CHECK(text.contains("Actions=Launcher;\n"));
    CHECK(text.contains("[Desktop Action Launcher]\nName=Open Launcher\nExec=/home/u/.local/bin/demo --launcher\n"));
}

TEST_CASE("Desktop entry quotes executables with spaces", "[platform][desktop]")
{
    const auto text = DesktopEntry::Build(MakeInfo(), "/opt/my app/demo");
    CHECK(text.contains("Exec=\"/opt/my app/demo\" %f\n"));
}

TEST_CASE("Install and uninstall write and remove the desktop files", "[platform][desktop]")
{
    TempTree tree;
    Write(tree.Root / "res" / "demo.svg", std::string(Svg));
    const auto env = MakeEnv({{"GPP_SKIP_DESKTOP_CACHE", "1"}});
    const auto prefix = tree.Root / "prefix";

    const auto result = DesktopInstaller::Install({
        .Prefix = prefix,
        .Executable = prefix / "bin" / "demo",
        .IconSource = tree.Root / "res" / "demo.svg",
        .Info = MakeInfo(),
        .Env = env
    });
    REQUIRE(result.Success);
    CHECK(result.Warnings.empty());
    const auto desktop = prefix / "share" / "applications" / "demo.desktop";
    const auto icon = prefix / "share" / "icons" / "hicolor" / "scalable" / "apps" / "demo.svg";
    CHECK(fs::exists(desktop));
    CHECK(fs::exists(icon));
    CHECK(result.Paths.size() == 2);
    CHECK(Read(desktop).contains("Exec=" + (prefix / "bin" / "demo").string() + " %f"));

    const auto removed = DesktopInstaller::Uninstall(prefix, "demo", env);
    CHECK(removed.Success);
    CHECK_FALSE(fs::exists(desktop));
    CHECK_FALSE(fs::exists(icon));
}

TEST_CASE("Install warns about a missing icon but still writes the entry", "[platform][desktop]")
{
    TempTree tree;
    const auto result = DesktopInstaller::Install({
        .Prefix = tree.Root,
        .Executable = tree.Root / "bin" / "demo",
        .IconSource = tree.Root / "missing.svg",
        .Info = MakeInfo(),
        .Env = MakeEnv({{"GPP_SKIP_DESKTOP_CACHE", "1"}})
    });
    CHECK(result.Success);
    CHECK(result.Warnings.size() == 1);
    CHECK(fs::exists(tree.Root / "share" / "applications" / "demo.desktop"));
}

TEST_CASE("Install places square PNG icons in the matching size directory", "[platform][desktop]")
{
    TempTree tree;
    std::string png = "\x89PNG\r\n\x1a\n";
    png += std::string("\0\0\0\r", 4) + "IHDR" + std::string("\0\0\0\x40\0\0\0\x40", 8) + std::string(8, '\0');
    Write(tree.Root / "icon.png", png);
    const auto result = DesktopInstaller::Install({
        .Prefix = tree.Root / "p",
        .Executable = "/bin/demo",
        .IconSource = tree.Root / "icon.png",
        .Info = MakeInfo(),
        .Env = MakeEnv({{"GPP_SKIP_DESKTOP_CACHE", "1"}})
    });
    CHECK(fs::exists(tree.Root / "p" / "share" / "icons" / "hicolor" / "64x64" / "apps" / "demo.png"));
}

TEST_CASE("Generated desktop file passes desktop-file-validate", "[platform][desktop]")
{
    if (!fs::exists("/usr/bin/desktop-file-validate"))
    {
        SKIP("desktop-file-validate not installed");
    }
    TempTree tree;
    const auto file = tree.Root / "demo.desktop";
    Write(file, DesktopEntry::Build(MakeInfo(), "/home/u/.local/bin/demo"));
    const auto command = "/usr/bin/desktop-file-validate '" + file.string() + "' > /dev/null 2>&1";
    CHECK(std::system(command.c_str()) == 0);
}

TEST_CASE("Window manager detection", "[platform][wm]")
{
    CHECK(WmRules::Detect(MakeEnv({{"XDG_CURRENT_DESKTOP", "Hyprland"}})) == WmKind::Hyprland);
    CHECK(WmRules::Detect(MakeEnv({{"HYPRLAND_INSTANCE_SIGNATURE", "abc"}})) == WmKind::Hyprland);
    CHECK(WmRules::Detect(MakeEnv({{"XDG_CURRENT_DESKTOP", "sway"}})) == WmKind::Sway);
    CHECK(WmRules::Detect(MakeEnv({{"SWAYSOCK", "/run/sway"}})) == WmKind::Sway);
    CHECK(WmRules::Detect(MakeEnv({{"I3SOCK", "/run/i3"}})) == WmKind::I3);
    CHECK(WmRules::Detect(MakeEnv({{"DESKTOP_SESSION", "i3"}})) == WmKind::I3);
    CHECK(WmRules::Detect(MakeEnv({{"XDG_CURRENT_DESKTOP", "GNOME"}, {"DESKTOP_SESSION", "gnome"}})) ==
        WmKind::Other);
    CHECK(WmRules::Detect(MakeEnv({})) == WmKind::Other);
}

TEST_CASE("Window rule snippets per window manager", "[platform][wm]")
{
    CHECK(WmRules::Snippet(WmKind::Hyprland, "demo", true) ==
        "hl.window_rule({ match = { class = \"demo\" }, float = true, center = true })\n");
    const auto conf = WmRules::Snippet(WmKind::Hyprland, "demo", false);
    CHECK(conf.contains("windowrule = float on, match:class ^(demo)$"));
    CHECK(conf.contains("windowrule = center on, match:class ^(demo)$"));
    CHECK(WmRules::Snippet(WmKind::Sway, "demo", false) == "for_window [app_id=\"demo\"] floating enable\n");
    CHECK(WmRules::Snippet(WmKind::I3, "demo", false) == "for_window [class=\"demo\"] floating enable\n");
    CHECK(WmRules::Snippet(WmKind::Other, "demo", false).empty());
    CHECK(WmRules::CommentPrefix(WmKind::Hyprland, true) == "--");
    CHECK(WmRules::CommentPrefix(WmKind::Hyprland, false) == "#");
    CHECK(WmRules::CommentPrefix(WmKind::Sway, false) == "#");
}

TEST_CASE("Window manager config file lookup", "[platform][wm]")
{
    TempTree tree;
    const auto env = MakeEnv({{"XDG_CONFIG_HOME", tree.Root.string()}});
    CHECK_FALSE(WmRules::ConfigFile(WmKind::Hyprland, env));
    Write(tree.Root / "hypr" / "hyprland.conf", "");
    CHECK(WmRules::ConfigFile(WmKind::Hyprland, env) == tree.Root / "hypr" / "hyprland.conf");
    Write(tree.Root / "hypr" / "hyprland.lua", "");
    CHECK(WmRules::ConfigFile(WmKind::Hyprland, env) == tree.Root / "hypr" / "hyprland.lua");
    Write(tree.Root / "sway" / "config", "");
    CHECK(WmRules::ConfigFile(WmKind::Sway, env) == tree.Root / "sway" / "config");
    CHECK_FALSE(WmRules::ConfigFile(WmKind::I3, env));
}

namespace
{
    struct WmFixture
    {
        TempTree Tree;
        EnvLookup Env;
        fs::path Config;
        fs::path AppConfig;
        FakePrompter Prompter;

        WmFixture()
        {
            Env = MakeEnv({{"XDG_CONFIG_HOME", (Tree.Root / "xdg").string()}, {"SWAYSOCK", "/run/sway"}});
            Config = Tree.Root / "xdg" / "sway" / "config";
            AppConfig = Tree.Root / "app";
            Write(Config, "set $mod Mod4\n");
        }

        WmRulesResult Apply(const WmRulePolicy policy)
        {
            return WmRules::Apply({
                .Info = MakeInfo(),
                .ConfigDir = AppConfig,
                .Policy = policy,
                .Env = Env,
                .Prompter = &Prompter
            });
        }
    };
}

TEST_CASE("Always applies once: one marker block, one backup", "[platform][wm]")
{
    WmFixture f;
    const auto first = f.Apply(WmRulePolicy::Always);
    CHECK(first.Outcome == WmRuleOutcome::Applied);
    CHECK(first.Wm == WmKind::Sway);
    CHECK(fs::exists(f.AppConfig / "wm" / "demo.sway"));
    CHECK(Read(f.AppConfig / "wm" / "demo.sway") == "for_window [app_id=\"demo\"] floating enable\n");

    const auto backup = fs::path(f.Config.string() + ".bak-demo");
    REQUIRE(fs::exists(backup));
    CHECK(Read(backup) == "set $mod Mod4\n");

    const auto second = f.Apply(WmRulePolicy::Always);
    CHECK(second.Outcome == WmRuleOutcome::AlreadyDecided);
    const auto text = Read(f.Config);
    CHECK(Count(text, "# demo:begin") == 1);
    CHECK(Count(text, "# demo:end") == 1);
    CHECK(Count(text, "for_window [app_id=\"demo\"]") == 1);
    CHECK(Read(backup) == "set $mod Mod4\n");
}

TEST_CASE("Existing marker block is not duplicated even without a stored decision", "[platform][wm]")
{
    WmFixture f;
    REQUIRE(f.Apply(WmRulePolicy::Always).Outcome == WmRuleOutcome::Applied);
    fs::remove(f.AppConfig / "install.json");
    CHECK(f.Apply(WmRulePolicy::Always).Outcome == WmRuleOutcome::Applied);
    CHECK(Count(Read(f.Config), "# demo:begin") == 1);
}

TEST_CASE("Ask without a TTY skips and records nothing", "[platform][wm]")
{
    WmFixture f;
    f.Prompter.Interactive = false;
    const auto result = f.Apply(WmRulePolicy::Ask);
    CHECK(result.Outcome == WmRuleOutcome::Skipped);
    CHECK(f.Prompter.Asked == 0);
    CHECK_FALSE(fs::exists(f.AppConfig / "install.json"));
    CHECK(Read(f.Config) == "set $mod Mod4\n");
    CHECK(fs::exists(f.AppConfig / "wm" / "demo.sway"));
}

TEST_CASE("Ask with yes applies and the decision is persisted", "[platform][wm]")
{
    WmFixture f;
    f.Prompter.Answer = true;
    CHECK(f.Apply(WmRulePolicy::Ask).Outcome == WmRuleOutcome::Applied);
    CHECK(f.Prompter.LastQuestion == "Add a floating rule for Demo App to " + f.Config.string() + "?");
    CHECK(Read(f.AppConfig / "install.json").contains("\"applied\""));
    CHECK(f.Apply(WmRulePolicy::Ask).Outcome == WmRuleOutcome::AlreadyDecided);
    CHECK(f.Prompter.Asked == 1);
}

TEST_CASE("Ask with no is remembered and never prompts again", "[platform][wm]")
{
    WmFixture f;
    f.Prompter.Answer = false;
    CHECK(f.Apply(WmRulePolicy::Ask).Outcome == WmRuleOutcome::Declined);
    CHECK(Read(f.Config) == "set $mod Mod4\n");
    CHECK(Read(f.AppConfig / "install.json").contains("\"declined\""));

    f.Prompter.Answer = true;
    CHECK(f.Apply(WmRulePolicy::Ask).Outcome == WmRuleOutcome::AlreadyDecided);
    CHECK(f.Prompter.Asked == 1);
    CHECK(Read(f.Config) == "set $mod Mod4\n");
}

TEST_CASE("Never skips without recording", "[platform][wm]")
{
    WmFixture f;
    CHECK(f.Apply(WmRulePolicy::Never).Outcome == WmRuleOutcome::Skipped);
    CHECK_FALSE(fs::exists(f.AppConfig / "install.json"));
    CHECK(Read(f.Config) == "set $mod Mod4\n");
}

TEST_CASE("Hyprland Lua config gets Lua syntax and comment markers", "[platform][wm]")
{
    TempTree tree;
    const auto config = tree.Root / "xdg" / "hypr" / "hyprland.lua";
    Write(config, "-- user config");
    FakePrompter prompter;
    const auto result = WmRules::Apply({
        .Info = MakeInfo(),
        .ConfigDir = tree.Root / "app",
        .Policy = WmRulePolicy::Always,
        .Env = MakeEnv({{"XDG_CONFIG_HOME", (tree.Root / "xdg").string()}, {"XDG_CURRENT_DESKTOP", "Hyprland"}}),
        .Prompter = &prompter
    });
    CHECK(result.Outcome == WmRuleOutcome::Applied);
    const auto text = Read(config);
    CHECK(text.contains("\n-- demo:begin\nhl.window_rule("));
    CHECK(text.contains("-- demo:end\n"));
    CHECK(fs::exists(tree.Root / "app" / "wm" / "demo.lua"));
}

TEST_CASE("Missing window manager config still writes the snippet", "[platform][wm]")
{
    TempTree tree;
    const auto result = WmRules::Apply({
        .Info = MakeInfo(),
        .ConfigDir = tree.Root / "app",
        .Policy = WmRulePolicy::Always,
        .Env = MakeEnv({{"XDG_CONFIG_HOME", (tree.Root / "xdg").string()}, {"I3SOCK", "/x"}})
    });
    CHECK(result.Outcome == WmRuleOutcome::NoConfigFile);
    CHECK(fs::exists(tree.Root / "app" / "wm" / "demo.i3"));
}

TEST_CASE("Desktop CLI ignores unrelated arguments", "[platform][desktop]")
{
    const std::vector<std::string> args{"--scene", "x.yaml"};
    CHECK_FALSE(HandleDesktopArgs(args, {.Info = MakeInfo()}).has_value());
}

TEST_CASE("Desktop CLI installs and uninstalls into an explicit prefix", "[platform][desktop]")
{
    TempTree tree;
    Write(tree.Root / "share" / "demo" / "demo.svg", std::string(Svg));
    std::ostringstream out;
    FakePrompter prompter;
    prompter.Interactive = false;
    const DesktopCliContext context{
        .Info = MakeInfo(),
        .ResourceDir = tree.Root / "share" / "demo",
        .ConfigDir = tree.Root / "cfg",
        .BinaryPath = tree.Root / "build" / "Demo",
        .Env = MakeEnv({{"GPP_SKIP_DESKTOP_CACHE", "1"}, {"XDG_CONFIG_HOME", (tree.Root / "xdg").string()}}),
        .Prompter = &prompter,
        .Out = &out
    };
    const auto prefix = (tree.Root / "prefix").string();

    const std::vector<std::string> install{"--install-desktop", "--prefix", prefix, "--no-wm-rules"};
    CHECK(HandleDesktopArgs(install, context) == 0);
    CHECK(fs::exists(tree.Root / "prefix" / "share" / "applications" / "demo.desktop"));
    CHECK(Read(tree.Root / "prefix" / "share" / "applications" / "demo.desktop")
              .contains("Exec=" + (tree.Root / "build" / "Demo").string() + " %f"));
    CHECK(out.str().contains("Wrote "));

    const std::vector<std::string> uninstall{"--uninstall-desktop", "--prefix", prefix};
    CHECK(HandleDesktopArgs(uninstall, context) == 0);
    CHECK_FALSE(fs::exists(tree.Root / "prefix" / "share" / "applications" / "demo.desktop"));
}

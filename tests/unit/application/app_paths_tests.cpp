#include <catch2/catch_test_macros.hpp>

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
                ("gpp_app_paths_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            fs::create_directories(Root);
        }

        ~TempTree()
        {
            std::error_code error;
            fs::remove_all(Root, error);
        }
    };

    AppInfo MakeInfo()
    {
        AppInfo info;
        info.Id = "demo";
        info.Name = "Demo App";
        return info;
    }
}

TEST_CASE("XDG directories fall back to HOME defaults", "[application][paths]")
{
    const auto dirs = AppPaths::ResolveXdgDirs(MakeEnv({{"HOME", "/home/u"}}));
    CHECK(dirs.ConfigHome == "/home/u/.config");
    CHECK(dirs.StateHome == "/home/u/.local/state");
    CHECK(dirs.CacheHome == "/home/u/.cache");
    CHECK(dirs.DataHome == "/home/u/.local/share");
    CHECK(dirs.Documents == "/home/u/Documents");
}

TEST_CASE("XDG environment variables override defaults and relative values are ignored", "[application][paths]")
{
    const auto dirs = AppPaths::ResolveXdgDirs(MakeEnv({
        {"HOME", "/home/u"},
        {"XDG_CONFIG_HOME", "/cfg"},
        {"XDG_CACHE_HOME", "relative/cache"},
        {"XDG_STATE_HOME", "/state"},
        {"XDG_DOCUMENTS_DIR", "/docs"}
    }));
    CHECK(dirs.ConfigHome == "/cfg");
    CHECK(dirs.StateHome == "/state");
    CHECK(dirs.CacheHome == "/home/u/.cache");
    CHECK(dirs.Documents == "/docs");
}

TEST_CASE("user-dirs.dirs is parsed with HOME expansion", "[application][paths]")
{
    const std::string content =
        "# comment\n"
        "XDG_DESKTOP_DIR=\"$HOME/Desktop\"\n"
        "XDG_DOCUMENTS_DIR=\"$HOME/Papers\"\n";
    CHECK(AppPaths::ParseUserDir(content, "XDG_DOCUMENTS_DIR", "/home/u") == fs::path("/home/u/Papers"));
    CHECK(AppPaths::ParseUserDir(content, "XDG_DESKTOP_DIR", "/home/u") == fs::path("/home/u/Desktop"));
    CHECK_FALSE(AppPaths::ParseUserDir(content, "XDG_MUSIC_DIR", "/home/u").has_value());
    CHECK(AppPaths::ParseUserDir("XDG_DOCUMENTS_DIR=\"/data/docs/\"\n", "XDG_DOCUMENTS_DIR", "/home/u") ==
          fs::path("/data/docs"));
    CHECK_FALSE(AppPaths::ParseUserDir("XDG_DOCUMENTS_DIR=\"$HOME/\"\n", "XDG_DOCUMENTS_DIR", "/home/u").has_value());
}

TEST_CASE("Documents dir is read from user-dirs.dirs under XDG_CONFIG_HOME", "[application][paths]")
{
    TempTree tree;
    std::ofstream(tree.Root / "user-dirs.dirs") << "XDG_DOCUMENTS_DIR=\"$HOME/Dokumente\"\n";
    const auto dirs = AppPaths::ResolveXdgDirs(MakeEnv({
        {"HOME", "/home/u"},
        {"XDG_CONFIG_HOME", tree.Root.string()}
    }));
    CHECK(dirs.Documents == "/home/u/Dokumente");
}

TEST_CASE("Mode detection uses share/<id> next to the binary and GPP_APP_MODE", "[application][paths]")
{
    TempTree tree;
    const auto binaryDir = tree.Root / "lib" / "demo";
    fs::create_directories(binaryDir);
    CHECK(AppPaths::DetectMode(binaryDir, "demo", MakeEnv({})) == AppPaths::Mode::Dev);

    fs::create_directories(tree.Root / "share" / "demo");
    CHECK(AppPaths::DetectMode(binaryDir, "demo", MakeEnv({})) == AppPaths::Mode::Installed);
    CHECK(AppPaths::DetectMode(binaryDir, "other", MakeEnv({})) == AppPaths::Mode::Dev);
    CHECK(AppPaths::DetectMode(binaryDir, "demo", MakeEnv({{"GPP_APP_MODE", "dev"}})) == AppPaths::Mode::Dev);
    CHECK(AppPaths::DetectMode(tree.Root / "elsewhere", "demo", MakeEnv({{"GPP_APP_MODE", "installed"}})) ==
          AppPaths::Mode::Installed);
}

TEST_CASE("Dev mode paths reproduce the .gpp layout", "[application][paths]")
{
    TempTree tree;
    const auto paths = AppPaths::Resolve({
        .BinaryDir = tree.Root / "build",
        .WorkingDir = tree.Root / "repo",
        .Info = MakeInfo(),
        .Env = MakeEnv({{"HOME", "/home/u"}})
    });
    CHECK(paths.GetMode() == AppPaths::Mode::Dev);
    CHECK(paths.ResourceDir() == tree.Root / "repo");
    CHECK(paths.ConfigDir().string() == ".gpp");
    CHECK(paths.StateDir().string() == ".gpp");
    CHECK(paths.LogDir().string() == ".gpp/logs");
    CHECK((paths.CacheDir() / "shader-cache").string() == ".gpp/shader-cache");
    CHECK((paths.CacheDir() / "hot-reload-cache").string() == ".gpp/hot-reload-cache");
    CHECK((paths.ConfigDir() / "imgui_main.ini").string() == ".gpp/imgui_main.ini");
    CHECK(paths.UserDataDir().string() == ".gpp");
    CHECK(paths.UserSubdir("exports").string() == ".gpp/exports");
    CHECK(paths.TempDir().string() == ".gpp/tmp");
}

TEST_CASE("Installed mode paths follow XDG and the documents dir", "[application][paths]")
{
    TempTree tree;
    fs::create_directories(tree.Root / "lib" / "demo");
    fs::create_directories(tree.Root / "share" / "demo");
    const auto paths = AppPaths::Resolve({
        .BinaryDir = tree.Root / "lib" / "demo",
        .WorkingDir = "/somewhere",
        .Info = MakeInfo(),
        .Env = MakeEnv({
            {"HOME", "/home/u"},
            {"XDG_CONFIG_HOME", "/cfg"},
            {"XDG_CACHE_HOME", "/cache"},
            {"XDG_STATE_HOME", "/state"},
            {"XDG_DOCUMENTS_DIR", "/docs"}
        })
    });
    CHECK(paths.IsInstalled());
    CHECK(paths.LogDir() == "/state/demo/logs");
    CHECK(paths.ResourceDir() == tree.Root / "share" / "demo");
    CHECK(paths.ConfigDir() == "/cfg/demo");
    CHECK(paths.StateDir() == "/cfg/demo");
    CHECK(paths.CacheDir() == "/cache/demo");
    CHECK(paths.TempDir() == "/cache/demo/tmp");
    CHECK(paths.UserDataDir() == "/docs/Demo App");
    CHECK(paths.UserSubdir("scenes") == "/docs/Demo App/scenes");
}

TEST_CASE("AppPaths service pushes resource and cache dirs into the file system", "[application][paths]")
{
    auto fileSystem = std::make_shared<FileSystem>();
    auto info = std::make_shared<AppInfo>(MakeInfo());
    const AppPaths paths(fileSystem, info);
    CHECK(fileSystem->CacheDir() == paths.CacheDir());
    CHECK(fileSystem->ResourceDir() == paths.ResourceDir());
}

TEST_CASE("FileSystem Auto resolution falls back from working dir to resource dir to binary dir", "[application][paths]")
{
    TempTree tree;
    fs::create_directories(tree.Root / "res");
    std::ofstream(tree.Root / "res" / "only_in_resources.txt") << "x";

    FileSystem fileSystem;
    fileSystem.SetResourceDir(tree.Root / "res");
    CHECK(fileSystem.ResolvePath("only_in_resources.txt") == tree.Root / "res" / "only_in_resources.txt");
    CHECK(fileSystem.ResolvePath("missing_everywhere.txt") == fileSystem.GetBinaryDirectory() / "missing_everywhere.txt");
}

TEST_CASE("AppInfo reads GPP:App and builder defaults are overridden by config", "[application][paths]")
{
    auto app = App::CreateBuilder();
    AppInfo defaults;
    defaults.Id = "from-code";
    defaults.Name = "From Code";
    defaults.Version = "1.2.3";
    app.SetAppInfo(defaults);
    app.Configuration.AddInMemory({{"GPP:App:Name", "From Config"}, {"GPP:App:Categories", "Graphics;"}});
    auto application = app.Build();
    const auto info = application->GetServiceProvider().GetRequiredService<AppInfo>();
    CHECK(info->Id == "from-code");
    CHECK(info->Name == "From Config");
    CHECK(info->Version == "1.2.3");
    CHECK(info->Categories == "Graphics;");
}

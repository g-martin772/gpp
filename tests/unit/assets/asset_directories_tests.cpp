#include <catch2/catch_test_macros.hpp>

import GPP;
import std;

using namespace GPP;
namespace fs = std::filesystem;

namespace
{
    struct TempTree
    {
        fs::path Root;

        TempTree()
            : Root(fs::temp_directory_path() /
                ("gpp_assets_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())))
        {
            fs::create_directories(Root);
        }

        ~TempTree()
        {
            std::error_code ec;
            fs::remove_all(Root, ec);
        }

        fs::path Write(const std::string& relative, const std::string& text) const
        {
            const auto path = Root / relative;
            fs::create_directories(path.parent_path());
            std::ofstream(path) << text;
            return path;
        }
    };

    AssetOptions ScriptOptions(const std::vector<fs::path>& dirs)
    {
        AssetOptions options;
        options.Kinds["Scripts"] = AssetKindOptions{dirs, {".lua"}};
        return options;
    }
}

TEST_CASE("AssetDirectories resolves first match across roots", "[assets]")
{
    TempTree tree;
    tree.Write("a/shared.lua", "from a");
    tree.Write("b/shared.lua", "from b");
    tree.Write("b/only_b.lua", "only b");
    AssetDirectories assets(ScriptOptions({tree.Root / "a", tree.Root / "b"}));

    CHECK(assets.Read("Scripts", "shared") == "from a");
    CHECK(assets.Read("Scripts", "only_b") == "only b");
    CHECK_FALSE(assets.Resolve("Scripts", "missing").has_value());
    CHECK_FALSE(assets.Resolve("Nope", "shared").has_value());
    CHECK_FALSE(assets.Resolve("Scripts", "../escape").has_value());
}

TEST_CASE("AssetDirectories lists subfolders and honours extensions", "[assets]")
{
    TempTree tree;
    tree.Write("s/main.lua", "");
    tree.Write("s/sub/deep.lua", "");
    tree.Write("s/notes.txt", "");
    tree.Write("t/main.lua", "");
    tree.Write("t/extra.lua", "");
    AssetDirectories assets(ScriptOptions({tree.Root / "s", tree.Root / "t"}));

    CHECK(assets.List("Scripts") == std::vector<std::string>{"extra", "main", "sub/deep"});
    CHECK(assets.Resolve("Scripts", "sub/deep").has_value());
    CHECK(assets.Resolve("Scripts", "main.lua") == tree.Root / "s" / "main.lua");
    CHECK_FALSE(assets.Resolve("Scripts", "notes").has_value());
}

TEST_CASE("AssetDirectories Poll reports changes, additions and removals", "[assets]")
{
    TempTree tree;
    const auto file = tree.Write("s/a.lua", "one");
    AssetDirectories assets(ScriptOptions({tree.Root / "s"}));

    std::vector<std::string> seen;
    assets.Subscribe([&](const AssetChange& change) { seen.push_back(change.Kind + ":" + change.Name); });

    CHECK(assets.Poll().empty());
    CHECK(assets.Version() == 0);

    fs::last_write_time(file, fs::last_write_time(file) + std::chrono::seconds(5));
    auto changes = assets.Poll();
    REQUIRE(changes.size() == 1);
    CHECK(changes[0].Name == "a");
    CHECK(assets.Version() == 1);

    tree.Write("s/b.lua", "new");
    CHECK(assets.Poll().size() == 1);
    fs::remove(file);
    changes = assets.Poll();
    REQUIRE(changes.size() == 1);
    CHECK(changes[0].Name == "a");
    CHECK(assets.Poll().empty());
    CHECK(seen.size() == 3);
    CHECK(seen[0] == "Scripts:a");
}

TEST_CASE("AssetOptions defaults and config parsing", "[assets]")
{
    const AssetOptions defaults;
    REQUIRE(defaults.Kinds.contains("Scripts"));
    CHECK(defaults.Kinds.at("Combos").Directories == std::vector<fs::path>{"combos"});

    auto data = std::make_shared<std::unordered_map<std::string, std::string>>();
    (*data)["GPP:Assets:Scripts:Directories:0"] = "mine";
    (*data)["GPP:Assets:Scripts:Directories:1"] = "other";
    (*data)["GPP:Assets:Scripts:Extensions:0"] = ".luau";
    (*data)["GPP:Assets:Kinds:0"] = "Mods";
    (*data)["GPP:Assets:Mods:Extensions:0"] = ".json";
    Configuration config(data);

    const auto options = AssetOptions::FromConfig(*config.GetSection("GPP:Assets"));
    CHECK(options.Kinds.at("Scripts").Directories == std::vector<fs::path>{"mine", "other"});
    CHECK(options.Kinds.at("Scripts").Extensions == std::vector<std::string>{".luau"});
    CHECK(options.Kinds.at("Graphs").Directories == std::vector<fs::path>{"graphs"});
    CHECK(options.Kinds.at("Mods").Directories == std::vector<fs::path>{"mods"});
    CHECK(options.Kinds.at("Mods").Extensions == std::vector<std::string>{".json"});
}

TEST_CASE("AssetDirectories is resolvable from the application container", "[assets]")
{
    auto builder = App::CreateBuilder();
    auto app = builder.Build();
    CHECK(app->GetServiceProvider().GetService<AssetDirectories>() != nullptr);
}

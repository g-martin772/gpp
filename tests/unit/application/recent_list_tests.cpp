#include <catch2/catch_test_macros.hpp>

import GPP;
import std;

using namespace GPP;
namespace fs = std::filesystem;

namespace
{
    struct TempDir
    {
        fs::path Root;

        TempDir()
        {
            Root = fs::temp_directory_path() / std::format("gpp_recent_{}", std::chrono::steady_clock::now().time_since_epoch().count());
            fs::create_directories(Root);
        }

        ~TempDir()
        {
            std::error_code ec;
            fs::remove_all(Root, ec);
        }

        fs::path Touch(const std::string& name) const
        {
            const fs::path file = Root / name;
            std::ofstream(file) << "x";
            return file;
        }
    };
}

TEST_CASE("RecentList moves re-added entries to the front and dedupes", "[recent]")
{
    RecentList list;
    list.Add("/a/one.yaml", {}, 1);
    list.Add("/a/two.yaml", {}, 2);
    list.Add("/a/../a/one.yaml", {}, 3);

    REQUIRE(list.Entries().size() == 2);
    CHECK(list.Entries()[0].Path == "/a/one.yaml");
    CHECK(list.Entries()[0].LastOpened == 3);
    CHECK(list.Entries()[1].Path == "/a/two.yaml");
}

TEST_CASE("RecentList caps entries and keeps newest", "[recent]")
{
    RecentList list(3);
    for (int i = 0; i < 6; ++i) list.Add(std::format("/s/{}.yaml", i), {}, i);

    REQUIRE(list.Entries().size() == 3);
    CHECK(list.Entries()[0].Path == "/s/5.yaml");
    CHECK(list.Entries()[2].Path == "/s/3.yaml");

    list.SetMaxEntries(2);
    CHECK(list.Entries().size() == 2);
    CHECK(RecentList().MaxEntries() == 8);
}

TEST_CASE("RecentList display name falls back to the file stem", "[recent]")
{
    RecentList list;
    list.Add("/s/black_hole.yaml");
    list.Add("/s/named.yaml", "Pretty");
    CHECK(list.Entries()[0].DisplayName() == "Pretty");
    CHECK(list.Entries()[1].DisplayName() == "black_hole");

    list.Add("/s/named.yaml");
    CHECK(list.Entries()[0].Name == "Pretty");
}

TEST_CASE("RecentList removes entries", "[recent]")
{
    RecentList list;
    list.Add("/s/a.yaml");
    list.Add("/s/b.yaml");
    CHECK(list.Remove("/s/./a.yaml"));
    CHECK_FALSE(list.Remove("/s/missing.yaml"));
    REQUIRE(list.Entries().size() == 1);
    CHECK(list.Entries()[0].Path == "/s/b.yaml");
}

TEST_CASE("RecentList prunes missing files", "[recent]")
{
    TempDir dir;
    const auto keep = dir.Touch("keep.yaml");
    const auto gone = dir.Touch("gone.yaml");

    RecentList list;
    list.Add(keep);
    list.Add(gone);
    fs::remove(gone);

    CHECK(list.Prune() == 1);
    REQUIRE(list.Entries().size() == 1);
    CHECK(list.Entries()[0].Path == RecentList::Normalize(keep));
}

TEST_CASE("RecentList round trips through a file", "[recent]")
{
    TempDir dir;
    RecentList list;
    list.Add("/s/a.yaml", "Alpha", 100);
    list.Add("/s/b.yaml", {}, 200);

    const auto file = dir.Root / "nested" / "recent.yaml";
    REQUIRE(list.Save(file));

    RecentList loaded;
    REQUIRE(loaded.Load(file));
    REQUIRE(loaded.Entries().size() == 2);
    CHECK(loaded.Entries()[0].Path == "/s/b.yaml");
    CHECK(loaded.Entries()[0].LastOpened == 200);
    CHECK(loaded.Entries()[1].Name == "Alpha");

    CHECK_FALSE(RecentList().Load(dir.Root / "nope.yaml"));
}

TEST_CASE("RecentList reads legacy scalar entries and respects the cap", "[recent]")
{
    const auto node = YAML::Load("[/s/a.yaml, {Path: /s/b.yaml, Name: B, LastOpened: 5}, /s/a.yaml, /s/c.yaml]");

    RecentList list(3);
    list.FromYaml(node);
    REQUIRE(list.Entries().size() == 3);
    CHECK(list.Entries()[0].Path == "/s/a.yaml");
    CHECK(list.Entries()[0].LastOpened == 0);
    CHECK(list.Entries()[1].Name == "B");
    CHECK(list.Entries()[1].LastOpened == 5);
    CHECK(list.Entries()[2].Path == "/s/c.yaml");

    RecentList embedded;
    embedded.FromYaml(list.ToYaml());
    CHECK(embedded.Entries().size() == 3);

    embedded.FromYaml(YAML::Node());
    CHECK(embedded.Empty());
}

TEST_CASE("RecentList merge unions by path, newest timestamp wins and exclusions stay out", "[recent]")
{
    RecentList mine;
    mine.Add("/a/shared.yaml", {}, 10);
    mine.Add("/a/mine.yaml", {}, 30);

    RecentList disk;
    disk.Add("/a/shared.yaml", "Shared", 20);
    disk.Add("/a/theirs.yaml", {}, 25);
    disk.Add("/a/removed.yaml", {}, 40);

    const std::vector<std::string> removed{"/a/removed.yaml"};
    mine.Merge(disk, removed);

    REQUIRE(mine.Entries().size() == 3);
    CHECK(mine.Entries()[0].Path == "/a/mine.yaml");
    CHECK(mine.Entries()[1].Path == "/a/theirs.yaml");
    CHECK(mine.Entries()[2].Path == "/a/shared.yaml");
    CHECK(mine.Entries()[2].LastOpened == 20);
    CHECK(mine.Entries()[2].Name == "Shared");
}

TEST_CASE("RecentList merge respects the entry cap", "[recent]")
{
    RecentList mine(2);
    mine.Add("/a/1.yaml", {}, 1);
    RecentList disk(5);
    disk.Add("/a/2.yaml", {}, 2);
    disk.Add("/a/3.yaml", {}, 3);
    mine.Merge(disk);

    REQUIRE(mine.Entries().size() == 2);
    CHECK(mine.Entries()[0].Path == "/a/3.yaml");
    CHECK(mine.Entries()[1].Path == "/a/2.yaml");
}

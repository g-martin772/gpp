#include <catch2/catch_test_macros.hpp>

import GPP;
import std;

using namespace GPP;

namespace
{
    struct WatchDir
    {
        std::filesystem::path Directory = std::filesystem::temp_directory_path() /
            ("gpp_asset_watch_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::shared_ptr<FileSystem> Files = std::make_shared<FileSystem>();
        std::shared_ptr<Logger> Log = std::make_shared<Logger>();

        WatchDir() { std::filesystem::create_directories(Directory); }

        ~WatchDir()
        {
            std::error_code error;
            std::filesystem::remove_all(Directory, error);
        }

        std::filesystem::path Write(const std::string& name, const std::string& content) const
        {
            const auto path = Directory / name;
            std::ofstream(path, std::ios::binary) << content;
            return path;
        }
    };

    void Bump(const std::filesystem::path& path)
    {
        std::filesystem::last_write_time(path, std::filesystem::last_write_time(path) + std::chrono::seconds(5));
    }

    std::vector<std::filesystem::path> WaitForChange(AssetFileWatch& watch,
                                                      const std::chrono::milliseconds timeout = std::chrono::milliseconds(3000))
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (auto changed = watch.TakeChanged(); !changed.empty()) return changed;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return {};
    }
}

TEST_CASE("Asset path index maps a path to every key and forgets removed keys", "[asset][watch]")
{
    AssetPathIndex<std::string> index;
    index.Add("/a/b/../tex.png", "one");
    index.Add("/a/tex.png", "two");
    index.Add("/a/other.png", "two");

    CHECK(index.Find("/a/tex.png") == std::vector<std::string>{"one", "two"});
    CHECK(index.Find("/a/other.png") == std::vector<std::string>{"two"});
    CHECK(index.Find("/a/none.png").empty());

    index.Remove("two");
    CHECK(index.Find("/a/tex.png") == std::vector<std::string>{"one"});
    CHECK(index.Find("/a/other.png").empty());
}

TEST_CASE("Asset watch reports a rewritten file once", "[asset][watch]")
{
    WatchDir dir;
    const auto file = dir.Write("a.png", "one");
    AssetFileWatch watch(dir.Files, dir.Log);
    const auto resolved = watch.Watch(file);
    CHECK(watch.Watch(file) == resolved);

    dir.Write("a.png", "two");
    Bump(file);
    const auto changed = WaitForChange(watch);
    REQUIRE(changed.size() == 1);
    CHECK(changed.front() == resolved);
    CHECK(watch.TakeChanged().empty());
}

TEST_CASE("Asset watch coalesces a burst of writes", "[asset][watch]")
{
    WatchDir dir;
    const auto file = dir.Write("a.png", "one");
    AssetFileWatch watch(dir.Files, dir.Log);
    watch.Watch(file);

    for (int i = 0; i < 5; ++i)
    {
        dir.Write("a.png", "burst" + std::to_string(i));
        Bump(file);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    REQUIRE(!WaitForChange(watch).empty());
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    CHECK(watch.TakeChanged().size() <= 1);
}

TEST_CASE("Asset watch sees an editor's write-then-rename", "[asset][watch]")
{
    WatchDir dir;
    const auto file = dir.Write("a.png", "one");
    AssetFileWatch watch(dir.Files, dir.Log);
    const auto resolved = watch.Watch(file);

    const auto temp = dir.Write(".a.png.tmp", "two");
    Bump(temp);
    std::filesystem::rename(temp, file);
    const auto changed = WaitForChange(watch);
    REQUIRE(changed.size() == 1);
    CHECK(changed.front() == resolved);
}

TEST_CASE("Asset watch ignores unrelated files", "[asset][watch]")
{
    WatchDir dir;
    const auto file = dir.Write("a.png", "one");
    const auto other = dir.Write("b.png", "one");
    AssetFileWatch watch(dir.Files, dir.Log);
    watch.Watch(file);

    dir.Write("b.png", "two");
    Bump(other);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    CHECK(watch.TakeChanged().empty());
}

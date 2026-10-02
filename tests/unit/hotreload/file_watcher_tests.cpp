#include <catch2/catch_test_macros.hpp>

import GPP;
import std;

using namespace GPP;

namespace
{
    std::filesystem::path MakeTempFile(const std::string& prefix, const std::string& contents = "initial")
    {
        const auto path = std::filesystem::temp_directory_path() /
            (prefix + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".txt");
        std::ofstream(path) << contents;
        return path;
    }
}

TEST_CASE("FileWatcher detects a modified watched file on PollNow", "[hotreload][file_watcher]")
{
    auto fileSystem = std::make_shared<FileSystem>();
    auto dispatcher = std::make_shared<EventDispatcher>();
    auto logger = std::make_shared<Logger>();
    FileWatcher watcher(fileSystem, dispatcher, logger);

    const auto path = MakeTempFile("gpp_filewatcher_");
    watcher.Watch(path);
    watcher.PollNow(); // establishes the baseline write-time so the next write registers as a change

    std::vector<std::filesystem::path> changed;
    auto subscription = dispatcher->Subscribe<FileChangedEvent>(
        [&](const FileChangedEvent& event) { changed.push_back(event.path); });

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    std::ofstream(path) << "updated contents";
    watcher.PollNow();

    REQUIRE(changed.size() == 1);
    CHECK(changed.front().lexically_normal() == path.lexically_normal());

    std::filesystem::remove(path);
}

TEST_CASE("FileWatcher reports a path it was watching before it existed", "[hotreload][file_watcher]")
{
    auto fileSystem = std::make_shared<FileSystem>();
    auto dispatcher = std::make_shared<EventDispatcher>();
    auto logger = std::make_shared<Logger>();
    FileWatcher watcher(fileSystem, dispatcher, logger);

    const auto path = std::filesystem::temp_directory_path() /
        ("gpp_filewatcher_missing_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".txt");
    std::filesystem::remove(path);

    watcher.Watch(path);
    watcher.PollNow();

    int changeCount = 0;
    auto subscription = dispatcher->Subscribe<FileChangedEvent>(
        [&](const FileChangedEvent&) { ++changeCount; });

    std::ofstream(path) << "now it exists";
    watcher.PollNow();

    CHECK(changeCount == 1);
    std::filesystem::remove(path);
}

TEST_CASE("FileWatcher stops reporting a path after Unwatch", "[hotreload][file_watcher]")
{
    auto fileSystem = std::make_shared<FileSystem>();
    auto dispatcher = std::make_shared<EventDispatcher>();
    auto logger = std::make_shared<Logger>();
    FileWatcher watcher(fileSystem, dispatcher, logger);

    const auto path = MakeTempFile("gpp_filewatcher_unwatch_");
    const auto id = watcher.Watch(path);
    watcher.PollNow();
    watcher.Unwatch(id);

    int changeCount = 0;
    auto subscription = dispatcher->Subscribe<FileChangedEvent>(
        [&](const FileChangedEvent&) { ++changeCount; });

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    std::ofstream(path) << "should not be observed";
    watcher.PollNow();

    CHECK(changeCount == 0);
    std::filesystem::remove(path);
}

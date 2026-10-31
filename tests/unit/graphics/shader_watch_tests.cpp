#include <catch2/catch_test_macros.hpp>

import GPP;
import std;

using namespace GPP;

namespace
{
    struct WatchFixture
    {
        std::filesystem::path Directory = std::filesystem::temp_directory_path() /
            ("gpp_shader_watch_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::shared_ptr<FileSystem> Files = std::make_shared<FileSystem>();
        std::shared_ptr<EventDispatcher> Dispatcher = std::make_shared<EventDispatcher>();
        std::shared_ptr<Logger> Log = std::make_shared<Logger>();
        std::atomic<int> Events{0};
        EventSubscription Subscription;

        WatchFixture()
        {
            std::filesystem::create_directories(Directory);
            Subscription = Dispatcher->Subscribe<ShaderFileChangedEvent>(
                [this](const ShaderFileChangedEvent&) { ++Events; }, EventDelivery::Immediate);
        }

        ~WatchFixture()
        {
            Subscription.Reset();
            std::error_code error;
            std::filesystem::remove_all(Directory, error);
        }

        std::filesystem::path Write(const std::string& name, const std::string& content)
        {
            const auto path = Directory / name;
            std::ofstream(path, std::ios::binary) << content;
            return path;
        }

        bool WaitFor(const int count, const std::chrono::milliseconds timeout = std::chrono::milliseconds(3000)) const
        {
            const auto deadline = std::chrono::steady_clock::now() + timeout;
            while (Events.load() < count && std::chrono::steady_clock::now() < deadline)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            return Events.load() >= count;
        }
    };

    void Bump(const std::filesystem::path& path)
    {
        std::filesystem::last_write_time(path, std::filesystem::last_write_time(path) + std::chrono::seconds(5));
    }
}

TEST_CASE("Shader watcher coalesces a burst of writes", "[graphics][shader][watch]")
{
    WatchFixture fixture;
    const auto file = fixture.Write("a.glsl", "one");
    ShaderFileWatcher watcher(fixture.Files, fixture.Dispatcher, fixture.Log);
    watcher.Watch(file);
    watcher.StartAsync(std::chrono::milliseconds(250)).get();

    for (int i = 0; i < 5; ++i)
    {
        fixture.Write("a.glsl", "burst" + std::to_string(i));
        Bump(file);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    REQUIRE(fixture.WaitFor(1));
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    CHECK(fixture.Events.load() <= 2);
    watcher.StopAsync().get();
}

TEST_CASE("Several watchers of one file share a single event", "[graphics][shader][watch]")
{
    WatchFixture fixture;
    const auto file = fixture.Write("shared.glsl", "x");
    std::vector<std::unique_ptr<ShaderFileWatcher>> watchers;
    for (int i = 0; i < 6; ++i)
    {
        watchers.push_back(std::make_unique<ShaderFileWatcher>(fixture.Files, fixture.Dispatcher, fixture.Log));
        watchers.back()->Watch(file);
        watchers.back()->StartAsync(std::chrono::milliseconds(250)).get();
    }
    Bump(file);
    REQUIRE(fixture.WaitFor(1));
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    CHECK(fixture.Events.load() == 1);
}

TEST_CASE("Shader watcher ignores unwatched files and stopped watchers", "[graphics][shader][watch]")
{
    WatchFixture fixture;
    const auto watched = fixture.Write("watched.glsl", "x");
    const auto other = fixture.Write("other.glsl", "x");
    ShaderFileWatcher watcher(fixture.Files, fixture.Dispatcher, fixture.Log);
    watcher.Watch(watched);
    watcher.StartAsync(std::chrono::milliseconds(250)).get();

    Bump(other);
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    CHECK(fixture.Events.load() == 0);

    watcher.StopAsync().get();
    Bump(watched);
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    CHECK(fixture.Events.load() == 0);
}

TEST_CASE("Shader watcher sees a file replaced by rename", "[graphics][shader][watch]")
{
    WatchFixture fixture;
    const auto file = fixture.Write("swap.glsl", "old");
    ShaderFileWatcher watcher(fixture.Files, fixture.Dispatcher, fixture.Log);
    watcher.Watch(file);
    watcher.StartAsync(std::chrono::milliseconds(250)).get();

    const auto replacement = fixture.Write("swap.tmp", "new");
    Bump(replacement);
    std::filesystem::rename(replacement, file);
    CHECK(fixture.WaitFor(1));
    watcher.StopAsync().get();
}

TEST_CASE("Retired objects outlive their grace period and then release", "[graphics][shader][retire][gpu]")
{
    const auto device = CreateHeadlessDevice(std::make_shared<Logger>());
    if (!device)
    {
        SKIP("no Vulkan device");
    }
    RetiredObjects retired(device, std::chrono::milliseconds(200));
    auto object = std::make_shared<int>(7);
    const std::weak_ptr<int> weak = object;
    retired.Retire(std::move(object));
    CHECK(retired.Pending() == 1);

    retired.Collect();
    CHECK_FALSE(weak.expired());

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!weak.expired() && std::chrono::steady_clock::now() < deadline)
    {
        retired.Collect();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    CHECK(weak.expired());
    CHECK(retired.Pending() == 0);
}

TEST_CASE("Retired objects are held while queue work is outstanding", "[graphics][shader][retire][gpu]")
{
    const auto device = CreateHeadlessDevice(std::make_shared<Logger>());
    if (!device)
    {
        SKIP("no Vulkan device");
    }
    RetiredObjects retired(device, std::chrono::milliseconds(0));
    auto object = std::make_shared<int>(1);
    const std::weak_ptr<int> weak = object;
    retired.Retire(std::move(object));
    const auto afterGrace = std::chrono::steady_clock::now() + std::chrono::milliseconds(1);
    for (int i = 0; i < 1000 && !weak.expired(); ++i)
    {
        retired.Collect(afterGrace);
        std::this_thread::yield();
    }
    CHECK(weak.expired());
}

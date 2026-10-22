#include <catch2/catch_test_macros.hpp>

#include <signal.h>

import GPP;
import std;

using namespace GPP;
namespace fs = std::filesystem;

namespace
{
    struct TempDir
    {
        fs::path Root = fs::temp_directory_path() / std::format("gpp_shadow_{}_{}", CurrentProcessId(), std::chrono::steady_clock::now().time_since_epoch().count());
        TempDir() { fs::create_directories(Root); }
        ~TempDir()
        {
            std::error_code ec;
            fs::remove_all(Root, ec);
        }
    };

    void Touch(const fs::path& path)
    {
        fs::create_directories(path.parent_path());
        std::ofstream(path) << "x";
    }
}

TEST_CASE("Sweep removes only directories of dead processes", "[hotreload][shadow]")
{
    TempDir dir;
    Touch(dir.Root / "100" / "a_1.so");
    Touch(dir.Root / "200" / "a_1.so");
    Touch(dir.Root / "300" / "a_1.so");
    Touch(dir.Root / "notapid" / "keep.txt");

    const auto removed = SweepStaleShadowDirectories(dir.Root, 300, [](const long pid) { return pid == 200; });

    CHECK(removed == 1);
    CHECK_FALSE(fs::exists(dir.Root / "100"));
    CHECK(fs::exists(dir.Root / "200" / "a_1.so"));
    CHECK(fs::exists(dir.Root / "300" / "a_1.so"));
    CHECK(fs::exists(dir.Root / "notapid" / "keep.txt"));
}

TEST_CASE("Sweep removes legacy flat shadow libraries", "[hotreload][shadow]")
{
    TempDir dir;
    Touch(dir.Root / "topbar_3.so");
    Touch(dir.Root / "notes.txt");

    CHECK(SweepStaleShadowDirectories(dir.Root, 1, [](long) { return true; }) == 1);
    CHECK_FALSE(fs::exists(dir.Root / "topbar_3.so"));
    CHECK(fs::exists(dir.Root / "notes.txt"));
}

TEST_CASE("Sweep tolerates a missing root", "[hotreload][shadow]")
{
    CHECK(SweepStaleShadowDirectories("/nonexistent/gpp_shadow_root", 1, [](long) { return false; }) == 0);
}

TEST_CASE("ProcessShadowDirectory is per pid, sweeps dead pids and is released on request", "[hotreload][shadow]")
{
    TempDir dir;
    const auto root = dir.Root / "cache";
    Touch(root / "999999998" / "x_1.so");
    Touch(root / "old_2.so");

    const auto mine = ProcessShadowDirectory(root);
    CHECK(mine == root / std::to_string(CurrentProcessId()));
    CHECK(fs::is_directory(mine));
    CHECK_FALSE(fs::exists(root / "999999998"));
    CHECK_FALSE(fs::exists(root / "old_2.so"));

    Touch(mine / "keep_1.so");
    CHECK(ProcessShadowDirectory(root) == mine);
    CHECK(fs::exists(mine / "keep_1.so"));

    ReleaseProcessShadowDirectories();
    CHECK_FALSE(fs::exists(mine));
}

TEST_CASE("Sweep treats a live process running another executable as stale (pid reuse)", "[hotreload][shadow]")
{
    TempDir dir;
    const auto root = dir.Root / "cache";
    const auto foreign = SpawnExe("/bin/sleep", std::vector<std::string>{"5"});
    REQUIRE(foreign.has_value());
    Touch(root / std::to_string(*foreign) / "x_1.so");

    ProcessShadowDirectory(root);
    CHECK_FALSE(IsInstanceOfSelf(*foreign));
    CHECK_FALSE(fs::exists(root / std::to_string(*foreign)));
    ReleaseProcessShadowDirectories();
    ::kill(static_cast<pid_t>(*foreign), SIGKILL);
}

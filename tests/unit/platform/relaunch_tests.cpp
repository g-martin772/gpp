#include <catch2/catch_test_macros.hpp>

#include <sys/wait.h>
#include <unistd.h>

import GPP;
import std;

using namespace GPP;

TEST_CASE("SelfPath resolves the running executable", "[relaunch]")
{
    const std::string self = SelfPath();
    REQUIRE_FALSE(self.empty());
    CHECK(std::filesystem::exists(self));
}

TEST_CASE("RelaunchExe reports a missing executable", "[relaunch]")
{
    const std::vector<std::string> args{"--x"};
    const auto result = RelaunchExe("/nonexistent/gpp_relaunch_target", args);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().contains("execv"));

    CHECK_FALSE(RelaunchExe("", args).has_value());
}

TEST_CASE("RelaunchExe replaces the process and forwards arguments", "[relaunch]")
{
    const pid_t child = ::fork();
    REQUIRE(child >= 0);
    if (child == 0)
    {
        const std::vector<std::string> args{"-c", "[ \"$1\" = hello ] && exit 7 || exit 9", "sh", "hello"};
        (void)RelaunchExe("/bin/sh", args);
        ::_exit(111);
    }

    int status = 0;
    REQUIRE(::waitpid(child, &status, 0) == child);
    REQUIRE(WIFEXITED(status));
    CHECK(WEXITSTATUS(status) == 7);
}

TEST_CASE("SpawnExe starts a detached process and returns its pid", "[relaunch][spawn]")
{
    const auto marker = std::filesystem::temp_directory_path() / std::format("gpp_spawn_{}", CurrentProcessId());
    std::filesystem::remove(marker);

    const std::vector<std::string> args{"-c", "echo $$ > \"$1\"", "sh", marker.string()};
    const auto pid = SpawnExe("/bin/sh", args);
    REQUIRE(pid.has_value());
    CHECK(*pid > 0);
    CHECK(*pid != CurrentProcessId());

    for (int i = 0; i < 200 && !std::filesystem::exists(marker); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    std::string written;
    for (int i = 0; i < 200 && written.empty(); ++i)
    {
        std::ifstream(marker) >> written;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(written == std::to_string(*pid));
    std::filesystem::remove(marker);

    int status = 0;
    CHECK(::waitpid(static_cast<pid_t>(*pid), &status, WNOHANG) == -1);
    CHECK(errno == ECHILD);
}

TEST_CASE("SpawnExe reports a missing executable", "[relaunch][spawn]")
{
    const std::vector<std::string> args;
    const auto result = SpawnExe("/nonexistent/gpp_spawn_target", args);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().contains("execv"));
    CHECK_FALSE(SpawnExe("", args).has_value());
}

TEST_CASE("IsInstanceOfSelf matches this process only", "[relaunch]")
{
    CHECK(IsInstanceOfSelf(CurrentProcessId()));
    CHECK(IsInstanceOfSelf(::getppid()) == (std::filesystem::read_symlink(std::format("/proc/{}/exe", ::getppid())) == SelfPath()));
    CHECK_FALSE(IsInstanceOfSelf(999999999));
}

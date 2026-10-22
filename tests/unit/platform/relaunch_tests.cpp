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

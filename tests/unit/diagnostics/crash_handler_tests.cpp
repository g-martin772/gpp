#include <catch2/catch_test_macros.hpp>

#if defined(__linux__)
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

import GPP;
import std;

using namespace GPP;
namespace fs = std::filesystem;

#if defined(__linux__)
namespace
{
    std::string ReadFile(const fs::path& path)
    {
        std::ifstream stream(path);
        return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    }

    fs::path RunCrashingChild(const fs::path& dir, const int sig)
    {
        const pid_t child = ::fork();
        if (child == 0)
        {
            auto buffer = std::make_shared<LogBuffer>();
            buffer->Append({.Source = "test", .Message = "last words"});
            InstallCrashHandler({.ReportDir = dir, .AppName = "crashtest", .LogFile = "/logs/x.log", .Buffer = buffer});
            ::raise(sig);
            ::_exit(0);
        }
        int status = 0;
        ::waitpid(child, &status, 0);
        REQUIRE(WIFSIGNALED(status));
        CHECK(WTERMSIG(status) == sig);
        const auto pending = FindPendingCrashReport(dir);
        REQUIRE(pending);
        return *pending;
    }
}

TEST_CASE("A crashing child writes a report and still dies from the original signal", "[diagnostics][crash][fork]")
{
    if (CrashHandlerDisabled()) SKIP("crash handler disabled by environment");
    const auto dir = fs::temp_directory_path() / std::format("gpp_crash_child_{}", std::chrono::steady_clock::now().time_since_epoch().count());
    fs::create_directories(dir);

    const auto report = RunCrashingChild(dir, SIGSEGV);
    const auto text = ReadFile(report);
    CHECK(text.starts_with("GPP crash report\nReason: signal 11 (SIGSEGV)\n"));
    CHECK(text.contains("Process: crashtest (pid "));
    CHECK(text.contains("Log file: /logs/x.log\n"));
    CHECK(text.contains("Backtrace (raw"));
    CHECK(text.contains("Stacktrace (symbolized):"));
    CHECK(text.contains("[test] [info] last words"));

    fs::remove_all(dir);
}
#endif

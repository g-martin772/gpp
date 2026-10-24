#include <catch2/catch_test_macros.hpp>

import GPP;
import std;

using namespace GPP;
namespace fs = std::filesystem;

TEST_CASE("Unix time converts to UTC civil time", "[diagnostics][crash]")
{
    CHECK(FormatUtc(0) == "1970-01-01 00:00:00 UTC");
    CHECK(FormatUtc(951782400) == "2000-02-29 00:00:00 UTC");
    CHECK(FormatUtc(1792773661) == "2026-10-23 16:41:01 UTC");
    CHECK(CrashReportFileName(1792773661, 77) == "crash-20261023-164101-77.txt");
}

TEST_CASE("Crash report file names are recognised", "[diagnostics][crash]")
{
    CHECK(IsCrashReportFileName("crash-20261023-164101-77.txt"));
    CHECK(IsCrashReportFileName("crash-20261023-164101-77.seen.txt"));
    CHECK(IsSeenCrashReportFileName("crash-20261023-164101-77.seen.txt"));
    CHECK_FALSE(IsSeenCrashReportFileName("crash-20261023-164101-77.txt"));
    CHECK_FALSE(IsCrashReportFileName("molehole-77.log"));
}

TEST_CASE("Crash reports list the reason, thread, stacktrace and log tail", "[diagnostics][crash]")
{
    CrashReportInfo info;
    info.Reason = "uncaught C++ exception";
    info.Detail = "std::runtime_error: boom";
    info.Process = "molehole";
    info.Pid = 1234;
    info.Thread = "sim";
    info.LogFile = "/logs/molehole-1234.log";
    info.UnixTime = 1792773661;
    info.Stacktrace = "0# f() at x.cpp:1";
    info.LogTail.push_back({1, std::chrono::system_clock::time_point(std::chrono::seconds(1792773660)), LogLevel::Warn, "GPP APP", "low on memory"});

    const auto text = FormatCrashReport(info);
    CHECK(text.starts_with("GPP crash report\n"));
    CHECK(text.contains("Reason: uncaught C++ exception\n"));
    CHECK(text.contains("Detail: std::runtime_error: boom\n"));
    CHECK(text.contains("Time: 2026-10-23 16:41:01 UTC\n"));
    CHECK(text.contains("Process: molehole (pid 1234)\n"));
    CHECK(text.contains("Thread: sim\n"));
    CHECK(text.contains("Log file: /logs/molehole-1234.log\n"));
    CHECK(text.contains("Stacktrace:\n0# f() at x.cpp:1\n"));
    CHECK(text.contains("Last 1 log entries:\n[16:41:00] [GPP APP] [warn] low on memory\n"));
}

TEST_CASE("The newest unseen crash report is found and marking it seen hides it", "[diagnostics][crash]")
{
    const auto dir = fs::temp_directory_path() / std::format("gpp_crash_pending_{}", std::chrono::steady_clock::now().time_since_epoch().count());
    fs::create_directories(dir);
    const auto write = [&](const std::string& name, const int minutesAgo)
    {
        std::ofstream(dir / name) << "x";
        fs::last_write_time(dir / name, fs::file_time_type::clock::now() - std::chrono::minutes(minutesAgo));
    };
    CHECK_FALSE(FindPendingCrashReport(dir));
    write("crash-20260101-000000-1.txt", 30);
    write("crash-20260102-000000-2.txt", 10);
    write("crash-20260103-000000-3.seen.txt", 1);
    write("molehole-9.log", 0);

    const auto pending = FindPendingCrashReport(dir);
    REQUIRE(pending);
    CHECK(pending->filename() == "crash-20260102-000000-2.txt");

    const auto seen = MarkCrashReportSeen(*pending);
    CHECK(seen.filename() == "crash-20260102-000000-2.seen.txt");
    CHECK(fs::exists(seen));
    CHECK_FALSE(fs::exists(*pending));
    CHECK(FindPendingCrashReport(dir)->filename() == "crash-20260101-000000-1.txt");
    fs::remove_all(dir);
}

TEST_CASE("LogBuffer tail copies the newest entries without blocking", "[diagnostics][crash]")
{
    LogBuffer buffer(10);
    for (int i = 0; i < 5; ++i) buffer.Append({.Message = std::to_string(i)});
    std::vector<LogEntry> tail;
    REQUIRE(buffer.TryCopyTail(3, tail));
    REQUIRE(tail.size() == 3);
    CHECK(tail.front().Message == "2");
    CHECK(tail.back().Message == "4");
    REQUIRE(buffer.TryCopyTail(100, tail));
    CHECK(tail.size() == 5);
}

#include <catch2/catch_test_macros.hpp>

import GPP;
import std;

using namespace GPP;
namespace fs = std::filesystem;

namespace
{
    struct TempDir
    {
        fs::path Path = fs::temp_directory_path() / std::format("gpp_logfiles_{}", std::chrono::steady_clock::now().time_since_epoch().count());
        TempDir() { fs::create_directories(Path); }
        ~TempDir() { std::error_code error; fs::remove_all(Path, error); }

        void Touch(const std::string& name, const int minutesAgo) const
        {
            std::ofstream(Path / name) << "x";
            fs::last_write_time(Path / name, fs::file_time_type::clock::now() - std::chrono::minutes(minutesAgo));
        }

        [[nodiscard]] std::set<std::string> Names() const
        {
            std::set<std::string> names;
            for (const auto& entry : fs::directory_iterator(Path)) names.insert(entry.path().filename().string());
            return names;
        }
    };
}

TEST_CASE("Log file names carry the process id and parse back", "[diagnostics][logs]")
{
    CHECK(LogFilePath("/logs", "molehole", 4242) == fs::path("/logs/molehole-4242.log"));
    CHECK(ParseLogFileRun("molehole-4242.log", "molehole") == 4242);
    CHECK(ParseLogFileRun("molehole-4242.3.log", "molehole") == 4242);
    CHECK_FALSE(ParseLogFileRun("molehole.log", "molehole"));
    CHECK_FALSE(ParseLogFileRun("molehole-abc.log", "molehole"));
    CHECK_FALSE(ParseLogFileRun("molehole-12.x.log", "molehole"));
    CHECK_FALSE(ParseLogFileRun("other-12.log", "molehole"));
    CHECK_FALSE(ParseLogFileRun("molehole-12.txt", "molehole"));
}

TEST_CASE("Pruning keeps the newest runs with their rotated files", "[diagnostics][logs]")
{
    TempDir dir;
    dir.Touch("molehole-1.log", 50);
    dir.Touch("molehole-1.1.log", 60);
    dir.Touch("molehole-2.log", 30);
    dir.Touch("molehole-3.log", 10);
    dir.Touch("molehole-3.1.log", 20);
    dir.Touch("notes.txt", 500);

    const auto removed = PruneLogFiles(dir.Path, "molehole", {.MaxRuns = 2, .MaxCrashReports = 10});
    CHECK(removed == 2);
    CHECK(dir.Names() == std::set<std::string>{"molehole-2.log", "molehole-3.log", "molehole-3.1.log", "notes.txt"});
}

TEST_CASE("Pruning caps crash reports and keeps the newest", "[diagnostics][logs]")
{
    TempDir dir;
    dir.Touch("crash-20260101-000000-1.txt", 40);
    dir.Touch("crash-20260102-000000-2.seen.txt", 30);
    dir.Touch("crash-20260103-000000-3.txt", 20);

    CHECK(PruneLogFiles(dir.Path, "molehole", {.MaxRuns = 10, .MaxCrashReports = 2}) == 1);
    CHECK(dir.Names() == std::set<std::string>{"crash-20260102-000000-2.seen.txt", "crash-20260103-000000-3.txt"});
}

TEST_CASE("Pruning a missing directory is harmless", "[diagnostics][logs]")
{
    CHECK(PruneLogFiles(fs::temp_directory_path() / "gpp_logfiles_missing_dir", "molehole") == 0);
}

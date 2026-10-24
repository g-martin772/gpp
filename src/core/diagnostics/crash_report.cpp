module GPP.Core;

import :Diagnostics.CrashReport;
import std;

namespace GPP
{
    CivilTime UnixToCivil(const std::int64_t unixTime) noexcept
    {
        std::int64_t days = unixTime / 86400;
        std::int64_t seconds = unixTime % 86400;
        if (seconds < 0)
        {
            seconds += 86400;
            --days;
        }
        days += 719468;
        const std::int64_t era = (days >= 0 ? days : days - 146096) / 146097;
        const auto dayOfEra = static_cast<unsigned>(days - era * 146097);
        const unsigned yearOfEra = (dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 - dayOfEra / 146096) / 365;
        const unsigned dayOfYear = dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
        const unsigned mp = (5 * dayOfYear + 2) / 153;
        const unsigned day = dayOfYear - (153 * mp + 2) / 5 + 1;
        const unsigned month = mp < 10 ? mp + 3 : mp - 9;
        const auto year = static_cast<int>(yearOfEra) + static_cast<int>(era) * 400 + (month <= 2 ? 1 : 0);
        return {year, static_cast<int>(month), static_cast<int>(day), static_cast<int>(seconds / 3600),
                static_cast<int>(seconds % 3600 / 60), static_cast<int>(seconds % 60)};
    }

    bool IsCrashReportFileName(const std::string_view fileName)
    {
        return fileName.starts_with("crash-") && fileName.ends_with(".txt");
    }

    bool IsSeenCrashReportFileName(const std::string_view fileName)
    {
        return IsCrashReportFileName(fileName) && fileName.ends_with(".seen.txt");
    }

    std::string FormatUtc(const std::int64_t unixTime)
    {
        const auto t = UnixToCivil(unixTime);
        return std::format("{:04}-{:02}-{:02} {:02}:{:02}:{:02} UTC", t.Year, t.Month, t.Day, t.Hour, t.Minute, t.Second);
    }

    std::string CrashReportFileName(const std::int64_t unixTime, const long pid)
    {
        const auto t = UnixToCivil(unixTime);
        return std::format("crash-{:04}{:02}{:02}-{:02}{:02}{:02}-{}.txt", t.Year, t.Month, t.Day, t.Hour, t.Minute, t.Second, pid);
    }

    std::string FormatLogTail(const std::vector<LogEntry>& entries)
    {
        static constexpr std::array names{"trace", "debug", "info", "warn", "error", "critical", "off"};
        std::string text;
        for (const auto& entry : entries)
        {
            const auto level = names[std::min<std::size_t>(static_cast<std::size_t>(entry.Level), names.size() - 1)];
            const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(entry.Time.time_since_epoch()).count();
            text += std::format("[{}] [{}] [{}] {}\n", FormatUtc(seconds).substr(11, 8), entry.Source, level, entry.Message);
        }
        return text;
    }

    std::string FormatCrashReport(const CrashReportInfo& info)
    {
        std::string text = "GPP crash report\n";
        text += std::format("Reason: {}\n", info.Reason);
        if (!info.Detail.empty()) text += std::format("Detail: {}\n", info.Detail);
        text += std::format("Time: {}\n", FormatUtc(info.UnixTime));
        text += std::format("Process: {} (pid {})\n", info.Process, info.Pid);
        text += std::format("Thread: {}\n", info.Thread);
        if (!info.LogFile.empty()) text += std::format("Log file: {}\n", info.LogFile);
        if (!info.Stacktrace.empty()) text += std::format("\nStacktrace:\n{}\n", info.Stacktrace);
        if (!info.LogTail.empty()) text += std::format("\nLast {} log entries:\n{}", info.LogTail.size(), FormatLogTail(info.LogTail));
        return text;
    }

    std::optional<std::filesystem::path> FindPendingCrashReport(const std::filesystem::path& directory)
    {
        namespace fs = std::filesystem;
        std::error_code error;
        std::optional<std::pair<fs::file_time_type, fs::path>> best;
        for (const auto& entry : fs::directory_iterator(directory, error))
        {
            const auto fileName = entry.path().filename().string();
            if (!IsCrashReportFileName(fileName) || IsSeenCrashReportFileName(fileName) || !entry.is_regular_file(error)) continue;
            const auto time = entry.last_write_time(error);
            if (!best || time > best->first) best = {time, entry.path()};
        }
        if (!best) return std::nullopt;
        return best->second;
    }

    std::filesystem::path MarkCrashReportSeen(const std::filesystem::path& report)
    {
        auto seen = report;
        seen.replace_extension(".seen.txt");
        std::error_code error;
        std::filesystem::rename(report, seen, error);
        return error ? report : seen;
    }
}

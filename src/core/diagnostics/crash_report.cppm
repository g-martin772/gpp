export module GPP.Core:Diagnostics.CrashReport;

import std;
import :LogBuffer;

namespace GPP
{
    export struct CrashReportInfo
    {
        std::string Reason;
        std::string Detail;
        std::string Process;
        long Pid = 0;
        std::string Thread;
        std::string LogFile;
        std::int64_t UnixTime = 0;
        std::string Stacktrace;
        std::vector<LogEntry> LogTail;
    };

    export struct CivilTime
    {
        int Year, Month, Day, Hour, Minute, Second;
    };

    export [[nodiscard]] bool IsCrashReportFileName(std::string_view fileName);
    export [[nodiscard]] bool IsSeenCrashReportFileName(std::string_view fileName);

    // crash-YYYYMMDD-HHMMSS-<pid>.txt (UTC)
    export [[nodiscard]] std::string CrashReportFileName(std::int64_t unixTime, long pid);
    export [[nodiscard]] std::string FormatUtc(std::int64_t unixTime);
    export [[nodiscard]] std::string FormatCrashReport(const CrashReportInfo& info);
    export [[nodiscard]] std::string FormatLogTail(const std::vector<LogEntry>& entries);

    export [[nodiscard]] std::optional<std::filesystem::path> FindPendingCrashReport(const std::filesystem::path& directory);
    // Renames crash-*.txt to crash-*.seen.txt; returns the new path.
    export std::filesystem::path MarkCrashReportSeen(const std::filesystem::path& report);

    export CivilTime UnixToCivil(std::int64_t unixTime) noexcept;
}

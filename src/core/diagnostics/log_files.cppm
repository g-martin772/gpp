export module GPP.Core:Diagnostics.LogFiles;

import std;

namespace GPP
{
    export struct LogRetention
    {
        std::size_t MaxRuns = 10;
        std::size_t MaxCrashReports = 20;
    };

    // <dir>/<name>-<pid>.log; spdlog rotation adds <name>-<pid>.<n>.log
    export [[nodiscard]] std::filesystem::path LogFilePath(
        const std::filesystem::path& directory, std::string_view name, long pid);

    export [[nodiscard]] std::optional<long> ParseLogFileRun(std::string_view fileName, std::string_view name);

    // Keeps the newest runs (by last write) and the newest crash reports; returns the removed file count.
    export std::size_t PruneLogFiles(const std::filesystem::path& directory, std::string_view name, const LogRetention& retention = {});
}

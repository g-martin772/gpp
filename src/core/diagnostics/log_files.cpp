module GPP.Core;

import :Diagnostics.LogFiles;
import :Diagnostics.CrashReport;
import std;

namespace GPP
{
    std::filesystem::path LogFilePath(const std::filesystem::path& directory, const std::string_view name, const long pid)
    {
        return directory / std::format("{}-{}.log", name, pid);
    }

    std::optional<long> ParseLogFileRun(const std::string_view fileName, const std::string_view name)
    {
        if (!fileName.starts_with(name) || !fileName.ends_with(".log")) return std::nullopt;
        auto rest = fileName.substr(name.size());
        if (rest.size() < 6 || rest.front() != '-') return std::nullopt;
        rest = rest.substr(1, rest.size() - 5);
        const auto digits = rest.substr(0, rest.find('.'));
        if (digits.empty() || !std::ranges::all_of(digits, [](const char c) { return c >= '0' && c <= '9'; })) return std::nullopt;
        const auto tail = rest.substr(digits.size());
        if (!tail.empty())
        {
            if (tail.size() < 2 || tail.front() != '.' || !std::ranges::all_of(tail.substr(1), [](const char c) { return c >= '0' && c <= '9'; }))
                return std::nullopt;
        }
        long pid = 0;
        std::from_chars(digits.data(), digits.data() + digits.size(), pid);
        return pid;
    }

    std::size_t PruneLogFiles(const std::filesystem::path& directory, const std::string_view name, const LogRetention& retention)
    {
        namespace fs = std::filesystem;
        std::error_code error;
        std::map<long, std::vector<fs::path>> runs;
        std::map<long, fs::file_time_type> newest;
        std::vector<std::pair<fs::file_time_type, fs::path>> reports;
        for (const auto& entry : fs::directory_iterator(directory, error))
        {
            if (!entry.is_regular_file(error)) continue;
            const auto fileName = entry.path().filename().string();
            const auto time = entry.last_write_time(error);
            if (const auto pid = ParseLogFileRun(fileName, name))
            {
                runs[*pid].push_back(entry.path());
                auto& latest = newest[*pid];
                latest = std::max(latest, time);
            }
            else if (IsCrashReportFileName(fileName))
            {
                reports.emplace_back(time, entry.path());
            }
        }

        std::size_t removed = 0;
        const auto removeFile = [&](const fs::path& path)
        {
            std::error_code removeError;
            if (fs::remove(path, removeError)) ++removed;
        };

        std::vector<std::pair<fs::file_time_type, long>> order;
        for (const auto& [pid, time] : newest) order.emplace_back(time, pid);
        std::ranges::sort(order, std::greater<>{});
        for (std::size_t i = retention.MaxRuns; i < order.size(); ++i)
        {
            for (const auto& path : runs[order[i].second]) removeFile(path);
        }

        std::ranges::sort(reports, std::greater<>{});
        for (std::size_t i = retention.MaxCrashReports; i < reports.size(); ++i) removeFile(reports[i].second);
        return removed;
    }
}

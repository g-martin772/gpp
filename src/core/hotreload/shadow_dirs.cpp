module GPP.Core;

import std;
import :HotReload.ShadowDirs;
import :Platform.Relaunch;

namespace GPP
{
    namespace
    {
        std::mutex g_Mutex;
        std::vector<std::filesystem::path> g_Prepared;
        bool g_AtExitRegistered = false;

        bool AllDigits(const std::string& text)
        {
            return !text.empty() && text.size() < 12 && std::ranges::all_of(text, [](const char c) { return c >= '0' && c <= '9'; });
        }

        bool IsLegacyShadowFile(const std::filesystem::path& path)
        {
            if (path.extension() != ".so") return false;
            const auto stem = path.stem().string();
            const auto underscore = stem.rfind('_');
            return underscore != std::string::npos && AllDigits(stem.substr(underscore + 1));
        }
    }

    std::size_t SweepStaleShadowDirectories(
        const std::filesystem::path& root, const long selfPid, const std::function<bool(long)>& isLive)
    {
        std::size_t removed = 0;
        std::error_code ec;
        for (std::filesystem::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec))
        {
            const auto name = it->path().filename().string();
            std::error_code entryError;
            if (it->is_directory(entryError) && AllDigits(name))
            {
                const long pid = std::stol(name);
                if (pid == selfPid || isLive(pid)) continue;
                std::filesystem::remove_all(it->path(), entryError);
                if (!entryError) ++removed;
            }
            else if (it->is_regular_file(entryError) && IsLegacyShadowFile(it->path()))
            {
                if (std::filesystem::remove(it->path(), entryError)) ++removed;
            }
        }
        return removed;
    }

    std::filesystem::path ProcessShadowDirectory(const std::filesystem::path& root)
    {
        const long pid = CurrentProcessId();
        const auto directory = root / std::to_string(pid);

        std::scoped_lock lock(g_Mutex);
        std::error_code ec;
        if (!std::ranges::contains(g_Prepared, root))
        {
            SweepStaleShadowDirectories(root, pid, IsInstanceOfSelf);
            std::filesystem::remove_all(directory, ec);
            g_Prepared.push_back(root);
            if (!g_AtExitRegistered)
            {
                g_AtExitRegistered = true;
                std::atexit(ReleaseProcessShadowDirectories);
            }
        }
        std::filesystem::create_directories(directory, ec);
        return directory;
    }

    void ReleaseProcessShadowDirectories()
    {
        std::scoped_lock lock(g_Mutex);
        const auto pid = std::to_string(CurrentProcessId());
        for (const auto& root : g_Prepared)
        {
            std::error_code ec;
            std::filesystem::remove_all(root / pid, ec);
        }
        g_Prepared.clear();
    }
}

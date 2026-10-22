export module GPP.Core:HotReload.ShadowDirs;

import std;

namespace GPP
{
    // `<root>/<pid>` The first use per root sweeps directories of dead processes.
    // Removed again at process exit
    export [[nodiscard]] std::filesystem::path ProcessShadowDirectory(const std::filesystem::path& root);

    export void ReleaseProcessShadowDirectories();

    // Removes numeric `<pid>` subdirectories whose process is no longer alive
    export std::size_t SweepStaleShadowDirectories(
        const std::filesystem::path& root, long selfPid, const std::function<bool(long)>& isLive);
}

export module GPP.Core:HotReload.ShadowDirs;

import std;

namespace GPP
{
    // `<root>/<pid>`, created on first use. The first use per root also sweeps the directories of dead processes.
    // Removed again at process exit, so parallel instances never overwrite libraries another one has mapped.
    export [[nodiscard]] std::filesystem::path ProcessShadowDirectory(const std::filesystem::path& root);

    export void ReleaseProcessShadowDirectories();

    // Removes numeric `<pid>` subdirectories whose process is not live (and legacy flat `*_<n>.so` files).
    export std::size_t SweepStaleShadowDirectories(
        const std::filesystem::path& root, long selfPid, const std::function<bool(long)>& isLive);
}

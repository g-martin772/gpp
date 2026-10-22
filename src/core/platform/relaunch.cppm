export module GPP.Core:Platform.Relaunch;

import std;

namespace GPP
{
    export [[nodiscard]] std::string SelfPath();

    export [[nodiscard]] long CurrentProcessId();

    // True when `pid` is alive and runs the same executable as this process.
    export [[nodiscard]] bool IsInstanceOfSelf(long pid);

    // Replaces the process image; returns only on failure.
    export [[nodiscard]] std::expected<void, std::string> RelaunchExe(
        const std::filesystem::path& executable, std::span<const std::string> args);

    export [[nodiscard]] std::expected<void, std::string> RelaunchSelf(std::span<const std::string> args);

    // Starts a detached process (own session, stdio on /dev/null, reparented, never a zombie); returns its pid.
    export [[nodiscard]] std::expected<long, std::string> SpawnExe(
        const std::filesystem::path& executable, std::span<const std::string> args);

    export [[nodiscard]] std::expected<long, std::string> SpawnSelf(std::span<const std::string> args);
}

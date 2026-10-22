export module GPP.Core:Platform.Relaunch;

import std;

namespace GPP
{
    export [[nodiscard]] std::string SelfPath();

    // Replaces the process image; returns only on failure.
    export [[nodiscard]] std::expected<void, std::string> RelaunchExe(
        const std::filesystem::path& executable, std::span<const std::string> args);

    export [[nodiscard]] std::expected<void, std::string> RelaunchSelf(std::span<const std::string> args);
}

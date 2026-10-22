export module GPP.Core:Platform.Relaunch;

import std;

namespace GPP
{
    export [[nodiscard]] std::string SelfPath();

    export [[nodiscard]] long CurrentProcessId();

    export [[nodiscard]] bool IsInstanceOfSelf(long pid);

    export [[nodiscard]] std::expected<void, std::string> RelaunchExe(
        const std::filesystem::path& executable, std::span<const std::string> args);

    export [[nodiscard]] std::expected<void, std::string> RelaunchSelf(std::span<const std::string> args);

    export [[nodiscard]] std::expected<long, std::string> SpawnExe(
        const std::filesystem::path& executable, std::span<const std::string> args);

    export [[nodiscard]] std::expected<long, std::string> SpawnSelf(std::span<const std::string> args);
}

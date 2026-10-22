module;

#if defined(__linux__)
#include <unistd.h>
#include <cerrno>
#include <cstring>
#endif

module GPP.Core;

import std;
import :Platform.Relaunch;

namespace GPP
{
    std::string SelfPath()
    {
        std::error_code ec;
        const auto path = std::filesystem::read_symlink("/proc/self/exe", ec);
        return ec ? std::string{} : path.string();
    }

    std::expected<void, std::string> RelaunchExe(
        const std::filesystem::path& executable, const std::span<const std::string> args)
    {
#if defined(__linux__)
        if (executable.empty()) return std::unexpected("relaunch: empty executable path");

        const std::string exe = executable.string();
        std::vector<char*> argv;
        argv.reserve(args.size() + 2);
        argv.push_back(const_cast<char*>(exe.c_str()));
        for (const auto& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
        argv.push_back(nullptr);

        ::execv(exe.c_str(), argv.data());
        return std::unexpected(std::format("relaunch: execv({}) failed: {}", exe, std::strerror(errno)));
#else
        return std::unexpected("relaunch: unsupported platform");
#endif
    }

    std::expected<void, std::string> RelaunchSelf(const std::span<const std::string> args)
    {
        const std::string self = SelfPath();
        if (self.empty()) return std::unexpected("relaunch: cannot resolve /proc/self/exe");
        return RelaunchExe(self, args);
    }
}

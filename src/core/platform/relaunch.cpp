module;

#if defined(__linux__)
#include <fcntl.h>
#include <linux/close_range.h>
#include <sys/wait.h>
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

    long CurrentProcessId()
    {
#if defined(__linux__)
        return ::getpid();
#else
        return 0;
#endif
    }

    bool IsInstanceOfSelf([[maybe_unused]] const long pid)
    {
#if defined(__linux__)
        const auto exeOf = [](const std::string& link)
        {
            std::error_code ec;
            auto target = std::filesystem::read_symlink(link, ec).string();
            if (target.ends_with(" (deleted)")) target.resize(target.size() - 10);
            return ec ? std::string{} : target;
        };
        const std::string self = exeOf("/proc/self/exe");
        return !self.empty() && exeOf(std::format("/proc/{}/exe", pid)) == self;
#else
        return false;
#endif
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

namespace GPP
{
    std::expected<long, std::string> SpawnExe(const std::filesystem::path& executable, const std::span<const std::string> args)
    {
#if defined(__linux__)
        if (executable.empty()) return std::unexpected("spawn: empty executable path");

        const std::string exe = executable.string();
        std::vector<char*> argv;
        argv.reserve(args.size() + 2);
        argv.push_back(const_cast<char*>(exe.c_str()));
        for (const auto& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
        argv.push_back(nullptr);

        int pipeFds[2];
        if (::pipe2(pipeFds, O_CLOEXEC) != 0) return std::unexpected(std::format("spawn: pipe failed: {}", std::strerror(errno)));

        const pid_t child = ::fork();
        if (child < 0)
        {
            const int error = errno;
            ::close(pipeFds[0]);
            ::close(pipeFds[1]);
            return std::unexpected(std::format("spawn: fork failed: {}", std::strerror(error)));
        }
        if (child == 0)
        {
            ::setsid();
            if (::fork() != 0) ::_exit(0);

            const int devNull = ::open("/dev/null", O_RDWR);
            if (devNull >= 0)
            {
                ::dup2(devNull, 0);
                ::dup2(devNull, 1);
                ::dup2(devNull, 2);
                if (devNull > 2) ::close(devNull);
            }
            ::close_range(3, ~0U, CLOSE_RANGE_CLOEXEC);

            const int self = ::getpid();
            (void)!::write(pipeFds[1], &self, sizeof(self));
            ::execv(exe.c_str(), argv.data());
            const int error = errno;
            (void)!::write(pipeFds[1], &error, sizeof(error));
            ::_exit(127);
        }

        ::close(pipeFds[1]);
        int status = 0;
        while (::waitpid(child, &status, 0) < 0 && errno == EINTR) {}

        int values[2]{};
        std::size_t received = 0;
        while (received < sizeof(values))
        {
            const auto count = ::read(pipeFds[0], reinterpret_cast<char*>(values) + received, sizeof(values) - received);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) break;
            received += static_cast<std::size_t>(count);
        }
        ::close(pipeFds[0]);

        if (received < sizeof(int)) return std::unexpected("spawn: child did not start");
        if (received >= sizeof(values)) return std::unexpected(std::format("spawn: execv({}) failed: {}", exe, std::strerror(values[1])));
        return values[0];
#else
        return std::unexpected("spawn: unsupported platform");
#endif
    }

    std::expected<long, std::string> SpawnSelf(const std::span<const std::string> args)
    {
        const std::string self = SelfPath();
        if (self.empty()) return std::unexpected("spawn: cannot resolve /proc/self/exe");
        return SpawnExe(self, args);
    }
}

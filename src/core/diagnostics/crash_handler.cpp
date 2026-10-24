module;

#if defined(__linux__)
#include <cerrno>
#include <cstdlib>
#include <errno.h>
#include <execinfo.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

module GPP.Core;

import :Diagnostics.CrashHandler;
import :Diagnostics.CrashReport;
import std;

#if defined(__linux__)
namespace GPP
{
    namespace
    {
        struct CrashState
        {
            char ReportDir[1024]{};
            char AppName[64]{};
            char LogFile[1024]{};
            std::shared_ptr<LogBuffer> Buffer;
            std::function<void()> Flush;
            std::size_t Tail = 200;
            long Pid = 0;
            std::atomic<bool> Reporting{false};
        };

        CrashState* g_State = nullptr;

        struct Out
        {
            char* Data;
            std::size_t Left;

            void Str(const char* text) noexcept
            {
                while (*text && Left > 1)
                {
                    *Data++ = *text++;
                    --Left;
                }
            }

            void Num(std::uint64_t value, const int width = 0) noexcept
            {
                char digits[24];
                int count = 0;
                do
                {
                    digits[count++] = static_cast<char>('0' + value % 10);
                    value /= 10;
                } while (value != 0);
                for (int pad = count; pad < width && Left > 1; ++pad)
                {
                    *Data++ = '0';
                    --Left;
                }
                while (count > 0 && Left > 1)
                {
                    *Data++ = digits[--count];
                    --Left;
                }
            }

            void Hex(std::uint64_t value) noexcept
            {
                Str("0x");
                bool started = false;
                for (int shift = 60; shift >= 0; shift -= 4)
                {
                    const auto nibble = static_cast<unsigned>((value >> shift) & 0xF);
                    if (nibble == 0 && !started && shift != 0) continue;
                    started = true;
                    if (Left <= 1) return;
                    *Data++ = static_cast<char>(nibble < 10 ? '0' + nibble : 'a' + nibble - 10);
                    --Left;
                }
            }
        };

        void WriteAll(const int fd, const char* data, std::size_t size) noexcept
        {
            while (size > 0)
            {
                const auto written = ::write(fd, data, size);
                if (written <= 0) return;
                data += written;
                size -= static_cast<std::size_t>(written);
            }
        }

        void WriteOut(const int fd, const Out& out, const char* begin) noexcept
        {
            WriteAll(fd, begin, static_cast<std::size_t>(out.Data - begin));
        }

        const char* SignalName(const int sig) noexcept
        {
            switch (sig)
            {
                case SIGSEGV: return "SIGSEGV";
                case SIGABRT: return "SIGABRT";
                case SIGFPE: return "SIGFPE";
                case SIGILL: return "SIGILL";
                case SIGBUS: return "SIGBUS";
                default: return "signal";
            }
        }

        void ThreadName(char* out, const std::size_t size) noexcept
        {
            out[0] = '\0';
            char name[16]{};
            if (::prctl(PR_GET_NAME, reinterpret_cast<unsigned long>(name), 0, 0, 0) == 0)
            {
                std::size_t i = 0;
                while (name[i] && i + 1 < size)
                {
                    out[i] = name[i];
                    ++i;
                }
                out[i] = '\0';
            }
        }

        void AppendFileName(Out& out, const std::int64_t now, const long pid) noexcept
        {
            const auto t = UnixToCivil(now);
            out.Str("crash-");
            out.Num(static_cast<std::uint64_t>(t.Year), 4);
            out.Num(static_cast<std::uint64_t>(t.Month), 2);
            out.Num(static_cast<std::uint64_t>(t.Day), 2);
            out.Str("-");
            out.Num(static_cast<std::uint64_t>(t.Hour), 2);
            out.Num(static_cast<std::uint64_t>(t.Minute), 2);
            out.Num(static_cast<std::uint64_t>(t.Second), 2);
            out.Str("-");
            out.Num(static_cast<std::uint64_t>(pid));
            out.Str(".txt");
        }

        void AppendUtc(Out& out, const std::int64_t now) noexcept
        {
            const auto t = UnixToCivil(now);
            out.Num(static_cast<std::uint64_t>(t.Year), 4);
            out.Str("-");
            out.Num(static_cast<std::uint64_t>(t.Month), 2);
            out.Str("-");
            out.Num(static_cast<std::uint64_t>(t.Day), 2);
            out.Str(" ");
            out.Num(static_cast<std::uint64_t>(t.Hour), 2);
            out.Str(":");
            out.Num(static_cast<std::uint64_t>(t.Minute), 2);
            out.Str(":");
            out.Num(static_cast<std::uint64_t>(t.Second), 2);
            out.Str(" UTC");
        }

        void Reraise(const int sig) noexcept
        {
            struct sigaction action{};
            action.sa_handler = SIG_DFL;
            sigemptyset(&action.sa_mask);
            ::sigaction(sig, &action, nullptr);
            ::raise(sig);
        }

        void AlarmHandler(int) noexcept
        {
            struct sigaction action{};
            action.sa_handler = SIG_DFL;
            sigemptyset(&action.sa_mask);
            ::sigaction(SIGABRT, &action, nullptr);
            ::abort();
        }

        void SignalHandler(const int sig, siginfo_t* info, void*)
        {
            const int savedErrno = errno;
            CrashState* state = g_State;
            if (!state || state->Reporting.exchange(true))
            {
                Reraise(sig);
                return;
            }

            const std::int64_t now = ::time(nullptr);
            char path[1400];
            {
                Out out{path, sizeof(path)};
                out.Str(state->ReportDir);
                out.Str("/");
                AppendFileName(out, now, state->Pid);
                *out.Data = '\0';
            }
            const int fd = ::open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
            if (fd >= 0)
            {
                char buffer[3072];
                Out out{buffer, sizeof(buffer)};
                out.Str("GPP crash report\nReason: signal ");
                out.Num(static_cast<std::uint64_t>(sig));
                out.Str(" (");
                out.Str(SignalName(sig));
                out.Str(")\n");
                if (info)
                {
                    if (info->si_code == SI_USER || info->si_code == SI_TKILL)
                    {
                        out.Str("Detail: sent by pid ");
                        out.Num(static_cast<std::uint64_t>(info->si_pid));
                        out.Str("\n");
                    }
                    else if (sig == SIGSEGV || sig == SIGBUS || sig == SIGILL || sig == SIGFPE)
                    {
                        out.Str("Detail: fault address ");
                        out.Hex(reinterpret_cast<std::uint64_t>(info->si_addr));
                        out.Str("\n");
                    }
                }
                out.Str("Time: ");
                AppendUtc(out, now);
                out.Str("\nProcess: ");
                out.Str(state->AppName);
                out.Str(" (pid ");
                out.Num(static_cast<std::uint64_t>(state->Pid));
                out.Str(")\nThread: ");
                char thread[32];
                ThreadName(thread, sizeof(thread));
                out.Str(thread);
                out.Str(" (tid ");
                out.Num(static_cast<std::uint64_t>(::syscall(SYS_gettid)));
                out.Str(")\n");
                if (state->LogFile[0])
                {
                    out.Str("Log file: ");
                    out.Str(state->LogFile);
                    out.Str("\n");
                }
                out.Str("\nBacktrace (raw, resolve offsets with addr2line -e <module>):\n");
                WriteOut(fd, out, buffer);
                void* frames[64];
                const int count = ::backtrace(frames, 64);
                ::backtrace_symbols_fd(frames, count, fd);

                static const char notice[] = "GPP crashed, report written: ";
                WriteAll(2, notice, sizeof(notice) - 1);
                WriteAll(2, path, std::strlen(path));
                WriteAll(2, "\n", 1);
            }

            // Second phase allocates and takes locks; the alarm keeps a deadlock from hanging the process.
            struct sigaction alarm{};
            alarm.sa_handler = AlarmHandler;
            sigemptyset(&alarm.sa_mask);
            ::sigaction(SIGALRM, &alarm, nullptr);
            ::alarm(5);
            if (fd >= 0)
            {
                try
                {
                    const std::string trace = "\nStacktrace (symbolized):\n" + std::to_string(std::stacktrace::current()) + "\n";
                    WriteAll(fd, trace.data(), trace.size());
                    if (state->Buffer)
                    {
                        std::vector<LogEntry> tail;
                        std::string text;
                        if (state->Buffer->TryCopyTail(state->Tail, tail))
                        {
                            text = std::format("\nLast {} log entries:\n{}", tail.size(), FormatLogTail(tail));
                        }
                        else
                        {
                            text = "\nLog buffer busy, entries skipped.\n";
                        }
                        WriteAll(fd, text.data(), text.size());
                    }
                    if (state->Flush) state->Flush();
                }
                catch (...)
                {
                }
                ::close(fd);
            }
            ::alarm(0);
            errno = savedErrno;
            Reraise(sig);
        }

        void TerminateHandler()
        {
            CrashState* state = g_State;
            if (state && !state->Reporting.exchange(true))
            {
                try
                {
                    CrashReportInfo info;
                    info.Reason = "std::terminate";
                    if (const auto current = std::current_exception())
                    {
                        try
                        {
                            std::rethrow_exception(current);
                        }
                        catch (const std::exception& error)
                        {
                            info.Reason = "uncaught C++ exception";
                            info.Detail = std::format("{}: {}", typeid(error).name(), error.what());
                        }
                        catch (...)
                        {
                            info.Reason = "uncaught C++ exception";
                            info.Detail = "non-standard exception type";
                        }
                    }
                    else
                    {
                        info.Detail = "terminate called without an active exception";
                    }
                    info.UnixTime = ::time(nullptr);
                    info.Process = state->AppName;
                    info.Pid = state->Pid;
                    char thread[32];
                    ThreadName(thread, sizeof(thread));
                    info.Thread = thread;
                    info.LogFile = state->LogFile;
                    info.Stacktrace = std::to_string(std::stacktrace::current());
                    if (state->Buffer) state->Buffer->TryCopyTail(state->Tail, info.LogTail);

                    const auto path = std::filesystem::path(state->ReportDir) / CrashReportFileName(info.UnixTime, info.Pid);
                    std::ofstream(path) << FormatCrashReport(info);
                    if (state->Flush) state->Flush();
                    std::cerr << "GPP crashed, report written: " << path.string() << '\n';
                }
                catch (...)
                {
                }
            }
            std::abort();
        }

        struct ThreadStack
        {
            static constexpr std::size_t kSize = 256 * 1024;
            void* Memory = nullptr;

            ~ThreadStack()
            {
                if (!Memory) return;
                stack_t disable{};
                disable.ss_flags = SS_DISABLE;
                ::sigaltstack(&disable, nullptr);
                ::munmap(Memory, kSize);
            }
        };
    }

    bool CrashHandlerDisabled()
    {
        // Lets debuggers and core dumps see the original signal.
        const char* value = std::getenv("GPP_NO_CRASH_HANDLER");
        return value && *value && std::string_view(value) != "0";
    }

    void RegisterCrashThread()
    {
        thread_local ThreadStack stack;
        if (stack.Memory) return;
        stack_t current{};
        if (::sigaltstack(nullptr, &current) == 0 && !(current.ss_flags & SS_DISABLE)) return;
        void* memory = ::mmap(nullptr, ThreadStack::kSize, PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        if (memory == MAP_FAILED) return;
        stack_t created{};
        created.ss_sp = memory;
        created.ss_size = ThreadStack::kSize;
        if (::sigaltstack(&created, nullptr) == 0) stack.Memory = memory;
        else ::munmap(memory, ThreadStack::kSize);
    }

    bool InstallCrashHandler(CrashHandlerOptions options)
    {
        if (CrashHandlerDisabled() || g_State) return false;

        auto* state = new CrashState;
        const auto copy = [](char* destination, const std::size_t capacity, const std::string& text)
        {
            std::memcpy(destination, text.data(), std::min(text.size(), capacity - 1));
        };
        copy(state->ReportDir, sizeof(state->ReportDir), options.ReportDir.string());
        copy(state->AppName, sizeof(state->AppName), options.AppName);
        copy(state->LogFile, sizeof(state->LogFile), options.LogFile);
        state->Buffer = std::move(options.Buffer);
        state->Flush = std::move(options.Flush);
        state->Tail = options.LogTail;
        state->Pid = ::getpid();

        std::error_code error;
        std::filesystem::create_directories(options.ReportDir, error);

        void* warmup[4];
        ::backtrace(warmup, 4);
        g_State = state;
        std::set_terminate(TerminateHandler);

#if !defined(__SANITIZE_ADDRESS__) && !defined(__SANITIZE_THREAD__)
        RegisterCrashThread();
        for (const int sig : {SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS})
        {
            struct sigaction action{};
            action.sa_sigaction = SignalHandler;
            action.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
            sigemptyset(&action.sa_mask);
            ::sigaction(sig, &action, nullptr);
        }
#endif
        return true;
    }
}
#else
namespace GPP
{
    bool CrashHandlerDisabled() { return true; }
    void RegisterCrashThread() {}
    bool InstallCrashHandler(CrashHandlerOptions) { return false; }
}
#endif

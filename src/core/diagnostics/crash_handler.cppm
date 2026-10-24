export module GPP.Core:Diagnostics.CrashHandler;

import std;
import :LogBuffer;

namespace GPP
{
    export struct CrashHandlerOptions
    {
        std::filesystem::path ReportDir;
        std::string AppName;
        std::string LogFile;
        std::shared_ptr<LogBuffer> Buffer;
        std::function<void()> Flush;
        std::size_t LogTail = 200;
    };

    // Returns false when disabled (GPP_NO_CRASH_HANDLER=1) or unsupported on this platform.
    export bool InstallCrashHandler(CrashHandlerOptions options);

    // Per-thread alternate signal stack so stack overflows on that thread are still reported.
    export void RegisterCrashThread();

    export [[nodiscard]] bool CrashHandlerDisabled();
}

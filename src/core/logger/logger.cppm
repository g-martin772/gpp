export module Core:Logger;
import :Types;

import fmt;
import spdlog;

export enum class $LogLevel {
    Trace,
    Debug,
    Info,
    Warn,
    Error,
    Critical,
    Off
};

export class $Logger {
    spdlog::logger m_logger;
    static std::unique_ptr<$Logger> s_instance;
public:
    $Logger();
    static $Logger* GetInstance();
    spdlog::logger& GetLogger();
    void SetLogger(spdlog::logger&& logger);
    void CreateConsoleLogger(const $string& name = "console");
    void CreateFileLogger(const $string& name, const $string& filename, bool truncate = false);
    void CreateMultiLogger(const $string& name, const $string& filename, bool truncate = false);
    void SetPattern(const $string& pattern);
    void SetLevel($LogLevel level);
    void Flush();
    void SetFlushOn($LogLevel level);

    template <typename... Args>
    void LogMessage(const $LogLevel level, $format_string<Args...> format, Args &&... args) {
        auto msg = fmt::format(format, std::forward<Args>(args)...);
        switch (level) {
            case $LogLevel::Trace:    m_logger.trace(msg); break;
            case $LogLevel::Debug:    m_logger.debug(msg); break;
            case $LogLevel::Info:     m_logger.info(msg); break;
            case $LogLevel::Warn:     m_logger.warn(msg); break;
            case $LogLevel::Error:    m_logger.error(msg); break;
            case $LogLevel::Critical: m_logger.critical(msg); break;
            case $LogLevel::Off:      break;
        }
    }

    template <typename... Args>
    void Trace($format_string<Args...> format, Args &&... args) {
        m_logger.trace(fmt::format(format, std::forward<Args>(args)...));
    }

    template <typename... Args>
    void Debug($format_string<Args...> format, Args &&... args) {
        m_logger.debug(fmt::format(format, std::forward<Args>(args)...));
    }

    template <typename... Args>
    void Info($format_string<Args...> format, Args &&... args) {
        m_logger.info(fmt::format(format, std::forward<Args>(args)...));
    }

    template <typename... Args>
    void Warn($format_string<Args...> format, Args &&... args) {
        m_logger.warn(fmt::format(format, std::forward<Args>(args)...));
    }

    template <typename... Args>
    void Error($format_string<Args...> format, Args &&... args) {
        m_logger.error(fmt::format(format, std::forward<Args>(args)...));
    }

    template <typename... Args>
    void Critical($format_string<Args...> format, Args &&... args) {
        m_logger.critical(fmt::format(format, std::forward<Args>(args)...));
    }

    template <typename... Args>
    static void Log($LogLevel level, $format_string<Args...> format, Args &&... args) {
        GetInstance()->LogMessage(level, format, std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void LogTrace($format_string<Args...> format, Args &&... args) {
        GetInstance()->Trace(format, std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void LogDebug($format_string<Args...> format, Args &&... args) {
        GetInstance()->Debug(format, std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void LogInfo($format_string<Args...> format, Args &&... args) {
        GetInstance()->Info(format, std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void LogWarn($format_string<Args...> format, Args &&... args) {
        GetInstance()->Warn(format, std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void LogError($format_string<Args...> format, Args &&... args) {
        GetInstance()->Error(format, std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void LogCritical($format_string<Args...> format, Args &&... args) {
        GetInstance()->Critical(format, std::forward<Args>(args)...);
    }
};

module;

#include <spdlog/spdlog.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

module GPP.Core;
import :Logger;

import std;

namespace GPP
{
    namespace
    {
        LogLevel FromSpdlog(const spdlog::level::level_enum level) {
            switch (level) {
            case spdlog::level::trace: return LogLevel::Trace;
            case spdlog::level::debug: return LogLevel::Debug;
            case spdlog::level::info: return LogLevel::Info;
            case spdlog::level::warn: return LogLevel::Warn;
            case spdlog::level::err: return LogLevel::Error;
            case spdlog::level::critical: return LogLevel::Critical;
            default: return LogLevel::Off;
            }
        }

        class BufferSink final : public spdlog::sinks::base_sink<std::mutex> {
        public:
            explicit BufferSink(std::shared_ptr<LogBuffer> buffer) : m_Buffer(std::move(buffer)) {}

        protected:
            void sink_it_(const spdlog::details::log_msg& msg) override {
                LogEntry entry;
                entry.Time = msg.time;
                entry.Level = FromSpdlog(msg.level);
                entry.Source.assign(msg.logger_name.data(), msg.logger_name.size());
                entry.Message.assign(msg.payload.data(), msg.payload.size());
                m_Buffer->Append(std::move(entry));
            }

            void flush_() override {}

        private:
            std::shared_ptr<LogBuffer> m_Buffer;
        };
    }

    std::unique_ptr<Logger> Logger::s_instance = nullptr;

    Logger::Logger()
        : m_logger("default", std::make_shared<spdlog::sinks::stdout_color_sink_mt>()),
          m_Buffer(std::make_shared<LogBuffer>()), m_Notifications(std::make_shared<NotificationCenter>()) {
        AttachBuffer();
    }

    void Logger::AttachBuffer() {
        m_logger.sinks().push_back(std::make_shared<BufferSink>(m_Buffer));
    }

    Logger *Logger::GetInstance() {
        if (!s_instance) {
            s_instance = std::unique_ptr<Logger>(new Logger());
        }
        return s_instance.get();
    }

    spdlog::logger &Logger::GetLogger() {
        return m_logger;
    }

    void Logger::SetLogger(spdlog::logger &&logger) {
        m_logger = std::move(logger);
        AttachBuffer();
    }

    void Logger::CreateConsoleLogger(const $string &name) {
        const auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        m_logger = spdlog::logger(name, sink);
        AttachBuffer();
    }

    void Logger::CreateFileLogger(const $string &name, const $string &filename, bool truncate) {
        const auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(filename, truncate);
        m_logger = spdlog::logger(name, sink);
        AttachBuffer();
    }

    void Logger::CreateMultiLogger(const $string &name, const $string &filename, bool truncate) {
        const auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        const auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(filename, truncate);

        std::vector<spdlog::sink_ptr> sinks{console_sink, file_sink};
        m_logger = spdlog::logger(name, sinks.begin(), sinks.end());
        AttachBuffer();
    }

    void Logger::CreateRotatingMultiLogger(const $string &name, const $string &filename, const std::size_t maxBytes,
                                           const std::size_t maxFiles) {
        const auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        const auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(filename, maxBytes, maxFiles);

        std::vector<spdlog::sink_ptr> sinks{console_sink, file_sink};
        m_logger = spdlog::logger(name, sinks.begin(), sinks.end());
        m_logger.flush_on(spdlog::level::info);
        AttachBuffer();
    }

    void Logger::SetPattern(const $string &pattern) {
        m_logger.set_pattern(pattern);
    }

    void Logger::SetLevel(const LogLevel level) {
        switch (level) {
        case LogLevel::Trace: m_logger.set_level(spdlog::level::trace);
            break;
        case LogLevel::Debug: m_logger.set_level(spdlog::level::debug);
            break;
        case LogLevel::Info: m_logger.set_level(spdlog::level::info);
            break;
        case LogLevel::Warn: m_logger.set_level(spdlog::level::warn);
            break;
        case LogLevel::Error: m_logger.set_level(spdlog::level::err);
            break;
        case LogLevel::Critical: m_logger.set_level(spdlog::level::critical);
            break;
        case LogLevel::Off: m_logger.set_level(spdlog::level::off);
            break;
        }
    }

    void Logger::Flush() {
        m_logger.flush();
    }

    void Logger::SetFlushOn(const LogLevel level) {
        switch (level) {
        case LogLevel::Trace: m_logger.flush_on(spdlog::level::trace);
            break;
        case LogLevel::Debug: m_logger.flush_on(spdlog::level::debug);
            break;
        case LogLevel::Info: m_logger.flush_on(spdlog::level::info);
            break;
        case LogLevel::Warn: m_logger.flush_on(spdlog::level::warn);
            break;
        case LogLevel::Error: m_logger.flush_on(spdlog::level::err);
            break;
        case LogLevel::Critical: m_logger.flush_on(spdlog::level::critical);
            break;
        case LogLevel::Off: m_logger.flush_on(spdlog::level::off);
            break;
        }
    }
}

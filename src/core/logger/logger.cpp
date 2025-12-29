module Core;
import :Logger;

import spdlog;
import std;

std::unique_ptr<$Logger> $Logger::s_instance = nullptr;

$Logger::$Logger() : m_logger("default", std::make_shared<spdlog::sinks::stdout_color_sink_mt>()) {
}

$Logger *$Logger::GetInstance() {
    if (!s_instance) {
        s_instance = std::unique_ptr<$Logger>(new $Logger());
    }
    return s_instance.get();
}

spdlog::logger &$Logger::GetLogger() {
    return m_logger;
}

void $Logger::SetLogger(spdlog::logger &&logger) {
    m_logger = std::move(logger);
}

void $Logger::CreateConsoleLogger(const $string &name) {
    const auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    m_logger = spdlog::logger(name, sink);
}

void $Logger::CreateFileLogger(const $string &name, const $string &filename, bool truncate) {
    const auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(filename, truncate);
    m_logger = spdlog::logger(name, sink);
}

void $Logger::CreateMultiLogger(const $string &name, const $string &filename, bool truncate) {
    const auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    const auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(filename, truncate);

    std::vector<spdlog::sink_ptr> sinks{console_sink, file_sink};
    m_logger = spdlog::logger(name, sinks.begin(), sinks.end());
}

void $Logger::SetPattern(const $string &pattern) {
    m_logger.set_pattern(pattern);
}

void $Logger::SetLevel(const $LogLevel level) {
    switch (level) {
        case $LogLevel::Trace: m_logger.set_level(spdlog::level::trace);
            break;
        case $LogLevel::Debug: m_logger.set_level(spdlog::level::debug);
            break;
        case $LogLevel::Info: m_logger.set_level(spdlog::level::info);
            break;
        case $LogLevel::Warn: m_logger.set_level(spdlog::level::warn);
            break;
        case $LogLevel::Error: m_logger.set_level(spdlog::level::err);
            break;
        case $LogLevel::Critical: m_logger.set_level(spdlog::level::critical);
            break;
        case $LogLevel::Off: m_logger.set_level(spdlog::level::off);
            break;
    }
}

void $Logger::Flush() {
    m_logger.flush();
}

void $Logger::SetFlushOn(const $LogLevel level) {
    switch (level) {
        case $LogLevel::Trace: m_logger.flush_on(spdlog::level::trace);
            break;
        case $LogLevel::Debug: m_logger.flush_on(spdlog::level::debug);
            break;
        case $LogLevel::Info: m_logger.flush_on(spdlog::level::info);
            break;
        case $LogLevel::Warn: m_logger.flush_on(spdlog::level::warn);
            break;
        case $LogLevel::Error: m_logger.flush_on(spdlog::level::err);
            break;
        case $LogLevel::Critical: m_logger.flush_on(spdlog::level::critical);
            break;
        case $LogLevel::Off: m_logger.flush_on(spdlog::level::off);
            break;
    }
}

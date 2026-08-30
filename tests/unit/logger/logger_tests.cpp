#include <catch2/catch_test_macros.hpp>

import GPP;
import std;

using namespace GPP;

namespace {
std::string MakeTempLogPath(const std::string& prefix) {
    const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return (std::filesystem::temp_directory_path() / (prefix + std::to_string(timestamp) + ".log")).string();
}
}

TEST_CASE("Logger writes formatted messages to a file and preserves payload", "[logger][file]") {
    const auto filename = MakeTempLogPath("gpp_logger_");
    auto* logger = Logger::GetInstance();

    logger->CreateFileLogger("unit-test-logger", filename, true);
    logger->SetPattern("%v");
    logger->SetLevel(LogLevel::Info);

    constexpr auto payload = "logger-unit-test-message";
    logger->Info("hello {}!", payload);
    logger->Flush();

    std::ifstream input(filename);
    REQUIRE(input.is_open());

    std::ostringstream buffer;
    buffer << input.rdbuf();
    const auto contents = buffer.str();
    CHECK(contents.find("hello logger-unit-test-message!") != std::string::npos);

    std::filesystem::remove(filename);
}

TEST_CASE("Logger can switch sink types and update level configuration", "[logger][config]") {
    const auto filename = MakeTempLogPath("gpp_logger_multilog_");
    auto* logger = Logger::GetInstance();

    logger->CreateConsoleLogger("console-logger");
    CHECK(logger->GetLogger().name() == "console-logger");

    logger->CreateMultiLogger("multi-logger", filename, true);
    CHECK(logger->GetLogger().name() == "multi-logger");

    logger->SetLevel(LogLevel::Warn);
    logger->SetFlushOn(LogLevel::Error);
    logger->SetPattern("%v");
    logger->Warn("warn {}", 42);
    logger->Error("error {}", 99);
    logger->Flush();

    std::ifstream input(filename);
    REQUIRE(input.is_open());
    std::ostringstream buffer;
    buffer << input.rdbuf();
    const auto contents = buffer.str();
    CHECK(contents.find("warn 42") != std::string::npos);
    CHECK(contents.find("error 99") != std::string::npos);

    std::filesystem::remove(filename);
}

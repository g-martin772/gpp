#include <catch2/catch_test_macros.hpp>

import GPP;
import std;

using namespace GPP;

TEST_CASE("Logger mirrors every emitted message into its buffer", "[logger][buffer]") {
    Logger logger;
    logger.SetLevel(LogLevel::Info);
    logger.SetPattern("%v");

    logger.Debug("filtered out");
    logger.Info("first {}", 1);
    logger.Error("second\nline");

    std::vector<LogEntry> entries;
    const auto last = logger.Buffer()->CopySince(0, entries);
    REQUIRE(entries.size() == 2);
    CHECK(entries[0].Level == LogLevel::Info);
    CHECK(entries[0].Message == "first 1");
    CHECK(entries[1].Level == LogLevel::Error);
    CHECK(entries[1].Message == "second\nline");
    CHECK(last == entries[1].Id);
}

TEST_CASE("Logger keeps feeding the buffer after its sinks are replaced", "[logger][buffer]") {
    Logger logger;
    logger.CreateConsoleLogger("renamed");
    logger.Warn("after replace");

    std::vector<LogEntry> entries;
    logger.Buffer()->CopySince(0, entries);
    REQUIRE(entries.size() == 1);
    CHECK(entries[0].Source == "renamed");
}

TEST_CASE("LogBuffer drops the oldest entries and CopySince only returns newer ones", "[logger][buffer]") {
    LogBuffer buffer(3);
    for (int i = 0; i < 5; ++i) buffer.Append(LogEntry{.Message = std::to_string(i)});

    std::vector<LogEntry> all;
    buffer.CopySince(0, all);
    REQUIRE(all.size() == 3);
    CHECK(all.front().Message == "2");

    std::vector<LogEntry> newer;
    CHECK(buffer.CopySince(all[1].Id, newer) == all.back().Id);
    REQUIRE(newer.size() == 1);
    CHECK(newer.front().Message == "4");

    std::vector<LogEntry> none;
    CHECK(buffer.CopySince(buffer.LastId(), none) == buffer.LastId());
    CHECK(none.empty());
}

TEST_CASE("NotificationCenter ages toasts, holds one under the cursor and replaces keyed ones", "[notifications]") {
    NotificationCenter center;
    const auto kept = center.Post(NotificationKind::Info, "kept", {}, 1.0f);
    center.Post(NotificationKind::Success, "gone", {}, 0.5f);
    center.Post(NotificationKind::Warning, "keyed", "a", 5.0f, "k");
    center.Post(NotificationKind::Error, "keyed", "b", 5.0f, "k");

    auto toasts = center.Toasts();
    REQUIRE(toasts.size() == 3);
    CHECK(toasts.back().Message == "b");
    CHECK(toasts.back().Kind == NotificationKind::Error);

    center.Tick(0.6f, kept);
    toasts = center.Toasts();
    REQUIRE(toasts.size() == 2);
    CHECK(toasts.front().Id == kept);
    CHECK(toasts.front().AgeSeconds == 0.0f);

    center.Tick(1.1f);
    CHECK(center.Toasts().size() == 1);
    center.Dismiss(center.Toasts().front().Id);
    CHECK(center.Toasts().empty());
}

TEST_CASE("NotificationCenter default durations follow severity", "[notifications]") {
    NotificationCenter center;
    center.Post(NotificationKind::Error, "e");
    CHECK(center.Toasts().front().DurationSeconds == NotificationCenter::DefaultDuration(NotificationKind::Error));
    CHECK(NotificationCenter::DefaultDuration(NotificationKind::Error) > NotificationCenter::DefaultDuration(NotificationKind::Info));
}

TEST_CASE("ProgressTask reports counted and fractional progress until finished", "[notifications]") {
    NotificationCenter center;
    const auto task = center.BeginTask("work");
    CHECK(task->Read().Fraction < 0.0f);

    task->SetFraction(0.25f);
    CHECK(task->Read().Fraction == 0.25f);

    task->SetTotal(4);
    task->Advance(3);
    task->SetDetail("step 3");
    const auto snapshot = task->Read();
    CHECK(snapshot.Fraction == 0.75f);
    CHECK(snapshot.Detail == "step 3");
    CHECK_FALSE(snapshot.Finished);

    CHECK(center.Tasks().size() == 1);
    task->Finish(false);
    CHECK(task->Read().Finished);
    CHECK_FALSE(task->Read().Ok);
    center.Tick(0.016f);
    CHECK(center.Tasks().empty());
}

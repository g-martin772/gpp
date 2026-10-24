#include <catch2/catch_test_macros.hpp>
#include <gpp/profile.h>

import GPP;
import std;

using namespace GPP;

namespace
{
    ProfileRecord Rec(const char* name, std::uint64_t start, std::uint32_t duration, std::uint16_t depth = 0)
    {
        return ProfileRecord{name, start, duration, depth};
    }

    std::shared_ptr<ProfileRing> FindRing(const std::string& name)
    {
        for (const auto& ring : Profiler::Rings())
        {
            if (ring->CurrentName() == name) return ring;
        }
        return nullptr;
    }

    struct EnabledScope
    {
        explicit EnabledScope(bool on) { Profiler::SetEnabled(on); }
        ~EnabledScope() { Profiler::SetEnabled(false); }
    };
}

TEST_CASE("The ring keeps the newest records across wraparound", "[profiler][ring]")
{
    auto ring = std::make_unique<ProfileRing>("r", 1);
    const std::uint64_t total = ProfileRing::kCapacity * 3 + 5;
    for (std::uint64_t i = 0; i < total; ++i) ring->Push(Rec("z", i, static_cast<std::uint32_t>(i % 1000), static_cast<std::uint16_t>(i % 3)));

    std::vector<ProfileRecord> out;
    const auto cursor = ring->Read(0, out);
    CHECK(cursor == total);
    REQUIRE(out.size() == ProfileRing::kCapacity);
    CHECK(out.front().StartNs == total - ProfileRing::kCapacity);
    CHECK(out.back().StartNs == total - 1);
    CHECK(out.back().DurationNs == (total - 1) % 1000);
    CHECK(out.back().Depth == (total - 1) % 3);
    for (std::size_t i = 1; i < out.size(); ++i) REQUIRE(out[i].StartNs == out[i - 1].StartNs + 1);

    out.clear();
    ring->Push(Rec("z", total, 1));
    CHECK(ring->Read(cursor, out) == total + 1);
    REQUIRE(out.size() == 1);
    CHECK(out[0].StartNs == total);
}

TEST_CASE("A reader racing the writer only ever sees contiguous records", "[profiler][ring][threads]")
{
    auto ring = std::make_unique<ProfileRing>("race", 2);
    std::atomic<bool> done{false};
    std::thread writer([&]
    {
        for (std::uint64_t i = 0; i < 2'000'000; ++i) ring->Push(Rec("z", i, 1));
        done = true;
    });
    std::uint64_t cursor = 0;
    std::vector<ProfileRecord> out;
    bool contiguous = true;
    while (!done.load())
    {
        out.clear();
        cursor = ring->Read(cursor, out);
        for (std::size_t i = 1; i < out.size(); ++i) contiguous &= out[i].StartNs == out[i - 1].StartNs + 1;
    }
    writer.join();
    CHECK(contiguous);
    CHECK(ring->Head() == 2'000'000);
}

TEST_CASE("Aggregation counts, totals and maxima per zone", "[profiler][aggregate]")
{
    const std::vector<ProfileRecord> records{Rec("a", 0, 1'000'000), Rec("b", 10, 500'000, 1), Rec("a", 20, 3'000'000),
                                             Rec("a", 30, 2'000'000), Rec(nullptr, 40, 9)};
    const auto stats = AggregateZones(records);
    REQUIRE(stats.size() == 2);
    CHECK(std::string(stats[0].Name) == "a");
    CHECK(stats[0].Count == 3);
    CHECK(stats[0].TotalMs == 6.0);
    CHECK(stats[0].MaxMs == 3.0);
    CHECK(stats[1].Count == 1);
    CHECK(stats[1].TotalMs == 0.5);
    CHECK(stats[1].MinDepth == 1);
}

TEST_CASE("The collector reports last, average and max over its window", "[profiler][collector]")
{
    ProfileCollector collector(1.0);
    constexpr std::uint64_t ms = 1'000'000;
    const std::vector<ProfileRecord> first{Rec("tick", 0, 2 * ms), Rec("tick", 10 * ms, 4 * ms), Rec("io", 20 * ms, ms)};
    collector.Update(7, "sim", first, 100 * ms);
    REQUIRE(collector.Threads().size() == 1);
    const auto& rows = collector.Threads()[0].Zones;
    REQUIRE(rows.size() == 2);
    CHECK(rows[0].Name == "io"); // rows are ordered by name
    CHECK(rows[1].Name == "tick");
    CHECK(rows[1].Calls == 2);
    CHECK(rows[1].LastMs == 6.0);
    CHECK(rows[1].AvgMs == 3.0);
    CHECK(rows[1].MaxMs == 4.0);

    const std::vector<ProfileRecord> second{Rec("tick", 200 * ms, 8 * ms)};
    collector.Update(7, "sim", second, 300 * ms);
    const auto& updated = collector.Threads()[0].Zones;
    const auto tick = std::ranges::find(updated, "tick", &ZoneRow::Name);
    REQUIRE(tick != updated.end());
    CHECK(tick->Calls == 1);
    CHECK(tick->LastMs == 8.0);
    CHECK(tick->AvgMs == (2.0 + 4.0 + 8.0) / 3.0);
    const auto io = std::ranges::find(updated, "io", &ZoneRow::Name);
    REQUIRE(io != updated.end());
    CHECK(io->Calls == 0);
    CHECK(io->LastMs == 0.0);

    collector.Update(7, "sim", {}, 1100 * ms);
    CHECK(collector.Threads()[0].Zones.size() == 1); // only the 200 ms record is inside the 1 s window
}

TEST_CASE("Disabled zones record nothing", "[profiler][disabled]")
{
    Profiler::SetEnabled(false);
    std::thread worker([]
    {
        Profiler::SetThreadName("profiler-test-disabled");
        for (int i = 0; i < 10'000; ++i)
        {
            GPP_ZONE("never");
        }
    });
    worker.join();
    const auto ring = FindRing("profiler-test-disabled");
    REQUIRE(ring);
    CHECK(ring->Head() == 0);
}

TEST_CASE("Zones nest and record from many threads", "[profiler][threads]")
{
    EnabledScope enabled(true);
    constexpr int kThreads = 4;
    constexpr int kZones = 1000;
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t)
    {
        threads.emplace_back([t]
        {
            Profiler::SetThreadName(std::format("profiler-test-{}", t));
            for (int i = 0; i < kZones; ++i)
            {
                GPP_ZONE("outer");
                GPP_ZONE("inner");
            }
        });
    }
    for (auto& thread : threads) thread.join();
    Profiler::SetEnabled(false);

    for (int t = 0; t < kThreads; ++t)
    {
        const auto ring = FindRing(std::format("profiler-test-{}", t));
        REQUIRE(ring);
        CHECK(ring->Retired());
        std::vector<ProfileRecord> records;
        ring->Read(0, records);
        REQUIRE(records.size() == 2 * kZones);
        CHECK(std::string(records[0].Name) == "inner");
        CHECK(records[0].Depth == 1);
        CHECK(std::string(records[1].Name) == "outer");
        CHECK(records[1].Depth == 0);
        const auto stats = AggregateZones(records);
        REQUIRE(stats.size() == 2);
        CHECK(stats[0].Count == kZones);
    }
    CHECK(std::string_view(Profiler::Intern("abc")) == "abc");
    CHECK(Profiler::Intern("abc") == Profiler::Intern(std::string("abc")));
}

TEST_CASE("Zone overhead", "[profiler][bench]")
{
    constexpr int kIterations = 1'000'000;
    const auto run = [&](bool on)
    {
        Profiler::SetEnabled(on);
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < kIterations; ++i)
        {
            GPP_ZONE("bench");
        }
        const auto ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count();
        Profiler::SetEnabled(false);
        return ns / kIterations;
    };
    const double off = run(false);
    const double on = run(true);
    WARN(std::format("zone cost: disabled {:.1f} ns, enabled {:.1f} ns", off, on));
    CHECK(off < 100.0);
    CHECK(on < 2000.0);
}

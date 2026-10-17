#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

import GPP;
import std;

using namespace GPP;
using Catch::Approx;

namespace
{
    std::vector<GpuTimestampSample> Samples(std::initializer_list<std::uint64_t> ticks)
    {
        std::vector<GpuTimestampSample> out;
        for (const auto tick : ticks) out.push_back({tick, 1});
        return out;
    }
}

TEST_CASE("Timestamp ticks convert to milliseconds through the device period", "[graphics][gpu-profiler]")
{
    CHECK(TimestampTicksToMs(1000, 2'001'000, 64, 1.0) == Approx(2.0));
    CHECK(TimestampTicksToMs(0, 1000, 64, 0.5) == Approx(0.0005));
}

TEST_CASE("Timestamp counters narrower than 64 bits wrap", "[graphics][gpu-profiler]")
{
    CHECK(TimestampTicksToMs(0xFFFFFF00, 0x100, 32, 1.0) == Approx(512e-6));
}

TEST_CASE("Unsupported timestamps give no time and a disabled ledger", "[graphics][gpu-profiler]")
{
    CHECK(TimestampTicksToMs(0, 1000, 0, 1.0) == 0.0);
    CHECK(TimestampTicksToMs(0, 1000, 64, 0.0) == 0.0);

    GpuTimingLedger ledger(2, 8, 0, 1.0);
    CHECK_FALSE(ledger.Enabled());
    ledger.BeginFrame(0);
    CHECK_FALSE(ledger.OpenSegment(0, "Trace").has_value());
    CHECK_FALSE(ledger.Resolve(0, Samples({0, 1})));
    CHECK(ledger.Latest().empty());
}

TEST_CASE("Segments of one scope are summed in first-use order", "[graphics][gpu-profiler]")
{
    GpuTimingLedger ledger(1, 8, 64, 1000.0);
    ledger.BeginFrame(0);
    for (const auto* name : {"Trace", "Bloom", "Trace"})
    {
        const auto begin = ledger.OpenSegment(0, name);
        REQUIRE(begin.has_value());
        ledger.CloseSegment(0, *begin);
    }
    REQUIRE(ledger.Resolve(0, Samples({0, 1000, 2000, 3000, 4000, 7000})));
    const auto timings = ledger.Latest();
    REQUIRE(timings.size() == 2);
    CHECK(timings[0].Name == "Trace");
    CHECK(timings[0].Ms == Approx(4.0));
    CHECK(timings[1].Name == "Bloom");
    CHECK(timings[1].Ms == Approx(1.0));
}

TEST_CASE("Results that are not ready yet are skipped and keep the previous frame", "[graphics][gpu-profiler]")
{
    GpuTimingLedger ledger(2, 8, 64, 1000.0);
    ledger.BeginFrame(0);
    ledger.CloseSegment(0, *ledger.OpenSegment(0, "Trace"));
    REQUIRE(ledger.Resolve(0, Samples({0, 1000})));

    ledger.BeginFrame(1);
    ledger.CloseSegment(1, *ledger.OpenSegment(1, "Trace"));
    std::vector<GpuTimestampSample> pending{{0, 1}, {0, 0}};
    CHECK_FALSE(ledger.Resolve(1, pending));
    REQUIRE(ledger.Latest().size() == 1);
    CHECK(ledger.Latest()[0].Ms == Approx(1.0));
    CHECK(ledger.LatestFrame() == 1);
}

TEST_CASE("An older slot never overwrites a newer published frame", "[graphics][gpu-profiler]")
{
    GpuTimingLedger ledger(2, 8, 64, 1000.0);
    ledger.BeginFrame(0);
    ledger.CloseSegment(0, *ledger.OpenSegment(0, "Trace"));
    ledger.BeginFrame(1);
    ledger.CloseSegment(1, *ledger.OpenSegment(1, "Trace"));

    REQUIRE(ledger.Resolve(1, Samples({0, 5000})));
    CHECK_FALSE(ledger.Resolve(0, Samples({0, 1000})));
    CHECK(ledger.Latest()[0].Ms == Approx(5.0));
}

TEST_CASE("Segments past the capacity are dropped and an unclosed segment is ignored", "[graphics][gpu-profiler]")
{
    GpuTimingLedger ledger(1, 2, 64, 1000.0);
    ledger.BeginFrame(0);
    const auto a = ledger.OpenSegment(0, "A");
    const auto b = ledger.OpenSegment(0, "B");
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    CHECK_FALSE(ledger.OpenSegment(0, "C").has_value());
    CHECK(ledger.QueriesUsed(0) == 4);
    ledger.CloseSegment(0, *a);
    REQUIRE(ledger.Resolve(0, Samples({0, 2000, 0, 0})));
    REQUIRE(ledger.Latest().size() == 1);
    CHECK(ledger.Latest()[0].Name == "A");
}

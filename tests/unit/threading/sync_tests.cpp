#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

import GPP;
import std;

using namespace GPP;
using namespace std::chrono_literals;

TEST_CASE("LatestValue hands readers the newest value without blocking either side", "[sync][latest_value]")
{
    LatestValue<std::string> mailbox(std::string("first"));
    CHECK(*mailbox.Load() == "first");
    const auto v0 = mailbox.Version();

    // A reader that keeps an old value alive must not stop the writer from publishing new ones.
    const auto held = mailbox.Load();
    mailbox.Publish("second");
    mailbox.Publish("third");

    CHECK(*held == "first");
    CHECK(*mailbox.Load() == "third");
    CHECK(mailbox.Version() == v0 + 2);
}

TEST_CASE("LatestValue is safe under concurrent publish and load", "[sync][latest_value]")
{
    struct Pair
    {
        std::uint64_t A = 0;
        std::uint64_t B = 0;
    };
    LatestValue<Pair> mailbox;
    std::atomic<bool> stop{false};
    std::atomic<bool> torn{false};

    std::vector<std::jthread> readers;
    for (int i = 0; i < 3; ++i)
    {
        readers.emplace_back([&]
        {
            while (!stop.load())
            {
                const auto value = mailbox.Load();
                if (value->A != value->B) torn = true;
            }
        });
    }
    for (std::uint64_t i = 1; i <= 20000; ++i) mailbox.Publish(Pair{i, i});
    stop = true;
    readers.clear();

    CHECK_FALSE(torn.load());
    CHECK(mailbox.Load()->A == 20000);
}

TEST_CASE("RateMeter reports the producer's rate and decays when the producer stalls", "[sync][rate_meter]")
{
    RateMeter meter(100ms);
    const auto start = RateMeter::Clock::now();

    // 50 ticks, one every 10 ms of (simulated) time => 100 Hz.
    for (int i = 0; i < 50; ++i)
    {
        meter.Tick(2.0, start + std::chrono::milliseconds(10 * i));
    }
    const auto last = start + 490ms;
    CHECK(meter.PerSecond(last) == Catch::Approx(100.0).epsilon(0.1));
    CHECK(meter.AverageMs() == Catch::Approx(2.0));
    CHECK(meter.LastMs() == Catch::Approx(2.0));
    CHECK(meter.Total() == 50);

    // No ticks for 4 seconds: the reported rate must fall instead of staying at the old healthy value.
    CHECK(meter.PerSecond(last + 4s) <= 0.25 + 1e-9);
    CHECK(meter.SecondsSinceLastTick(last + 4s) == Catch::Approx(4.0));
}

TEST_CASE("AdaptiveSlicer converges on slices of about the target duration", "[sync][slicer]")
{
    AdaptiveSlicer::Config config;
    config.TargetMs = 4.0;
    AdaptiveSlicer slicer(config);

    // Pretend every item costs 0.01 ms => 4 ms is 400 items.
    constexpr double kCostMs = 0.01;
    std::uint32_t remaining = 100000;
    std::uint32_t slices = 0;
    std::uint32_t largest = 0;
    while (remaining > 0)
    {
        const auto size = slicer.NextSliceSize(remaining);
        REQUIRE(size >= 1);
        REQUIRE(size <= remaining);
        slicer.Report(size, size * kCostMs);
        largest = std::max(largest, size);
        remaining -= size;
        ++slices;
        REQUIRE(slices < 10000);
    }
    CHECK(largest <= 520); // never much beyond the 400-item budget (tail merge allows a little slack)
    CHECK(largest >= 300);
}

TEST_CASE("AdaptiveSlicer shrinks immediately after an expensive slice", "[sync][slicer]")
{
    AdaptiveSlicer slicer;
    slicer.Report(1000, 1.0); // cheap: 0.001 ms/item
    const auto cheapSize = slicer.NextSliceSize(1'000'000);
    slicer.Report(cheapSize, 40.0); // one slice blows the budget
    const auto afterSpike = slicer.NextSliceSize(1'000'000);
    CHECK(afterSpike < cheapSize);
    // Cost per item is now 40ms/cheapSize, so a 4ms budget must give <= cheapSize/10 (+ rounding).
    CHECK(afterSpike <= cheapSize / 10 + 2);
}

TEST_CASE("AdaptiveSlicer never exceeds the dispatch cap or the remaining work", "[sync][slicer]")
{
    AdaptiveSlicer::Config config;
    config.MaxItems = 1000;
    AdaptiveSlicer slicer(config);
    slicer.Report(10, 0.0001); // practically free
    for (int i = 0; i < 20; ++i)
    {
        const auto size = slicer.NextSliceSize(500000);
        CHECK(size <= 1000);
        slicer.Report(size, 0.0001);
    }
    CHECK(slicer.NextSliceSize(7) <= 7);
    CHECK(slicer.NextSliceSize(0) == 0);
}

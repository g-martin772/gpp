export module GPP.Core:Threading.Sync;

import std;

namespace GPP
{
    export template <typename T>
    class LatestValue
    {
    public:
        LatestValue() : m_Value(std::make_shared<const T>()) {}
        explicit LatestValue(T initial) : m_Value(std::make_shared<const T>(std::move(initial))) {}

        void Publish(T value)
        {
            m_Value.store(std::make_shared<const T>(std::move(value)), std::memory_order_release);
            m_Version.fetch_add(1, std::memory_order_release);
        }

        [[nodiscard]] std::shared_ptr<const T> Load() const
        {
            return m_Value.load(std::memory_order_acquire);
        }

        [[nodiscard]] std::uint64_t Version() const noexcept
        {
            return m_Version.load(std::memory_order_acquire);
        }

    private:
        std::atomic<std::shared_ptr<const T>> m_Value;
        std::atomic<std::uint64_t> m_Version{0};
    };

    export class RateMeter
    {
    public:
        using Clock = std::chrono::steady_clock;

        explicit RateMeter(std::chrono::milliseconds window = std::chrono::milliseconds(500)) noexcept
            : m_WindowNs(std::chrono::duration_cast<std::chrono::nanoseconds>(window).count())
        {
        }

        void Tick(double durationMs = 0.0, Clock::time_point now = Clock::now()) noexcept
        {
            const auto nowNs = ToNs(now);
            m_Total.fetch_add(1, std::memory_order_relaxed);
            m_LastTickNs.store(nowNs, std::memory_order_relaxed);
            m_LastMs.store(durationMs, std::memory_order_relaxed);

            if (m_WindowStartNs == 0)
            {
                m_WindowStartNs = nowNs;
            }
            ++m_WindowCount;
            m_WindowSumMs += durationMs;
            m_WindowMaxMs = std::max(m_WindowMaxMs, durationMs);

            const auto elapsed = nowNs - m_WindowStartNs;
            if (elapsed >= m_WindowNs)
            {
                const double seconds = static_cast<double>(elapsed) * 1e-9;
                m_Rate.store(static_cast<double>(m_WindowCount) / seconds, std::memory_order_relaxed);
                m_AverageMs.store(m_WindowSumMs / static_cast<double>(m_WindowCount), std::memory_order_relaxed);
                m_MaxMs.store(m_WindowMaxMs, std::memory_order_relaxed);
                m_WindowStartNs = nowNs;
                m_WindowCount = 0;
                m_WindowSumMs = 0.0;
                m_WindowMaxMs = 0.0;
            }
        }

        [[nodiscard]] double PerSecond(Clock::time_point now = Clock::now()) const noexcept
        {
            const double rate = m_Rate.load(std::memory_order_relaxed);
            const auto lastNs = m_LastTickNs.load(std::memory_order_relaxed);
            if (lastNs == 0)
            {
                return 0.0;
            }
            const double sinceLast = static_cast<double>(ToNs(now) - lastNs) * 1e-9;
            if (sinceLast > 0.0 && sinceLast * rate > 2.0 && sinceLast > 0.25)
            {
                return std::min(rate, 1.0 / sinceLast);
            }
            return rate;
        }

        [[nodiscard]] double LastMs() const noexcept { return m_LastMs.load(std::memory_order_relaxed); }
        [[nodiscard]] double AverageMs() const noexcept { return m_AverageMs.load(std::memory_order_relaxed); }
        [[nodiscard]] double MaxMs() const noexcept { return m_MaxMs.load(std::memory_order_relaxed); }
        [[nodiscard]] std::uint64_t Total() const noexcept { return m_Total.load(std::memory_order_relaxed); }

        [[nodiscard]] double SecondsSinceLastTick(Clock::time_point now = Clock::now()) const noexcept
        {
            const auto lastNs = m_LastTickNs.load(std::memory_order_relaxed);
            return lastNs == 0 ? -1.0 : static_cast<double>(ToNs(now) - lastNs) * 1e-9;
        }

    private:
        static std::int64_t ToNs(Clock::time_point t) noexcept
        {
            return std::chrono::duration_cast<std::chrono::nanoseconds>(t.time_since_epoch()).count();
        }

        const std::int64_t m_WindowNs;

        std::int64_t m_WindowStartNs = 0;
        std::uint64_t m_WindowCount = 0;
        double m_WindowSumMs = 0.0;
        double m_WindowMaxMs = 0.0;

        // published
        std::atomic<double> m_Rate{0.0};
        std::atomic<double> m_AverageMs{0.0};
        std::atomic<double> m_MaxMs{0.0};
        std::atomic<double> m_LastMs{0.0};
        std::atomic<std::int64_t> m_LastTickNs{0};
        std::atomic<std::uint64_t> m_Total{0};
    };

    export class AdaptiveSlicer
    {
    public:
        struct Config
        {
            double TargetMs = 4.0;
            std::uint32_t MinItems = 1;
            std::uint32_t MaxItems = 65535;
            std::uint32_t InitialItems = 256;
            double GrowthLimit = 2.0;
        };

        AdaptiveSlicer() = default;
        explicit AdaptiveSlicer(Config config) : m_Config(config) {}

        [[nodiscard]] std::uint32_t NextSliceSize(const std::uint32_t remaining) const noexcept
        {
            if (remaining == 0)
            {
                return 0;
            }
            const auto cap = std::min(remaining, m_Config.MaxItems);
            double size = m_CostPerItemMs > 0.0
                              ? m_Config.TargetMs / m_CostPerItemMs
                              : static_cast<double>(m_Config.InitialItems);
            if (m_LastSize > 0)
            {
                size = std::min(size, static_cast<double>(m_LastSize) * m_Config.GrowthLimit);
            }
            auto items = static_cast<std::uint32_t>(
                std::clamp(size, static_cast<double>(m_Config.MinItems), static_cast<double>(cap)));
            // Don't leave a sliver for a whole extra submission when the rest nearly fits.
            if (items < remaining && remaining - items <= std::max(1u, items / 4u) && remaining <= cap)
            {
                items = remaining;
            }
            return std::clamp(items, 1u, cap);
        }

        void Report(const std::uint32_t items, const double elapsedMs) noexcept
        {
            if (items == 0)
            {
                return;
            }
            const double sample = std::max(elapsedMs, 0.0) / static_cast<double>(items);

            m_CostPerItemMs = m_CostPerItemMs <= 0.0 || sample > m_CostPerItemMs
                                  ? sample
                                  : m_CostPerItemMs * 0.75 + sample * 0.25;
            m_LastSize = items;
        }

        void BeginJob() noexcept { m_LastSize = 0; }
        void Reset() noexcept { m_CostPerItemMs = 0.0; m_LastSize = 0; }

        [[nodiscard]] double CostPerItemMs() const noexcept { return m_CostPerItemMs; }
        [[nodiscard]] const Config& GetConfig() const noexcept { return m_Config; }
        void SetTargetMs(const double targetMs) noexcept { m_Config.TargetMs = std::max(targetMs, 0.05); }

    private:
        Config m_Config{};
        double m_CostPerItemMs = 0.0;
        std::uint32_t m_LastSize = 0;
    };
}

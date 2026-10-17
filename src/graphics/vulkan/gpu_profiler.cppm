export module GPP.Graphics:Vulkan.GpuProfiler;

import std;
import vulkan;
import :Vulkan.Device;

export namespace GPP
{
    struct GpuScopeTiming
    {
        std::string Name;
        double Ms = 0.0;
    };

    struct GpuTimestampSample
    {
        std::uint64_t Value = 0;
        std::uint64_t Available = 0;
    };

    [[nodiscard]] inline double TimestampTicksToMs(const std::uint64_t begin, const std::uint64_t end,
                                                   const std::uint32_t validBits, const double periodNs) noexcept
    {
        if (validBits == 0 || periodNs <= 0.0) return 0.0;
        const std::uint64_t mask = validBits >= 64 ? ~0ull : (1ull << validBits) - 1ull;
        return static_cast<double>((end - begin) & mask) * periodNs * 1e-6;
    }

    class GpuTimingLedger
    {
    public:
        GpuTimingLedger(const std::uint32_t slotCount, const std::uint32_t segmentCapacity,
                        const std::uint32_t validBits, const double periodNs)
            : m_Slots(std::max(slotCount, 1u)), m_Capacity(segmentCapacity), m_ValidBits(validBits),
              m_PeriodNs(periodNs)
        {
        }

        [[nodiscard]] bool Enabled() const noexcept { return m_ValidBits > 0 && m_PeriodNs > 0.0 && m_Capacity > 0; }
        [[nodiscard]] std::uint32_t SlotCount() const noexcept { return static_cast<std::uint32_t>(m_Slots.size()); }
        [[nodiscard]] std::uint32_t QueriesCapacity() const noexcept { return m_Capacity * 2u; }
        [[nodiscard]] std::uint32_t QueriesUsed(const std::uint32_t slot) const
        {
            return static_cast<std::uint32_t>(m_Slots[slot % SlotCount()].Segments.size()) * 2u;
        }

        void BeginFrame(const std::uint32_t slot)
        {
            auto& state = m_Slots[slot % SlotCount()];
            state.Segments.clear();
            state.Frame = ++m_FrameCounter;
        }

        [[nodiscard]] std::optional<std::uint32_t> OpenSegment(const std::uint32_t slot, std::string name)
        {
            if (!Enabled()) return std::nullopt;
            auto& state = m_Slots[slot % SlotCount()];
            if (state.Segments.size() >= m_Capacity) return std::nullopt;
            state.Segments.push_back({std::move(name), false});
            return static_cast<std::uint32_t>(state.Segments.size() - 1) * 2u;
        }

        void CloseSegment(const std::uint32_t slot, const std::uint32_t beginQuery)
        {
            auto& segments = m_Slots[slot % SlotCount()].Segments;
            if (beginQuery / 2 < segments.size()) segments[beginQuery / 2].Closed = true;
        }

        bool Resolve(const std::uint32_t slot, const std::span<const GpuTimestampSample> samples)
        {
            auto& state = m_Slots[slot % SlotCount()];
            if (!Enabled() || state.Segments.empty() || state.Frame == 0) return false;
            if (samples.size() < state.Segments.size() * 2) return false;

            std::vector<GpuScopeTiming> out;
            for (std::size_t i = 0; i < state.Segments.size(); ++i)
            {
                const auto& segment = state.Segments[i];
                if (!segment.Closed) continue;
                const auto& begin = samples[i * 2];
                const auto& end = samples[i * 2 + 1];
                if (!begin.Available || !end.Available) return false;
                const double ms = TimestampTicksToMs(begin.Value, end.Value, m_ValidBits, m_PeriodNs);
                const auto it = std::ranges::find(out, segment.Name, &GpuScopeTiming::Name);
                if (it != out.end()) it->Ms += ms;
                else out.push_back({segment.Name, ms});
            }

            std::scoped_lock lock(m_Mutex);
            if (state.Frame <= m_LatestFrame) return false;
            m_LatestFrame = state.Frame;
            m_Latest = std::move(out);
            return true;
        }

        [[nodiscard]] std::vector<GpuScopeTiming> Latest() const
        {
            std::scoped_lock lock(m_Mutex);
            return m_Latest;
        }

        [[nodiscard]] std::uint64_t LatestFrame() const
        {
            std::scoped_lock lock(m_Mutex);
            return m_LatestFrame;
        }

    private:
        struct Segment
        {
            std::string Name;
            bool Closed = false;
        };

        struct Slot
        {
            std::vector<Segment> Segments;
            std::uint64_t Frame = 0;
        };

        std::vector<Slot> m_Slots;
        std::uint32_t m_Capacity;
        std::uint32_t m_ValidBits;
        double m_PeriodNs;
        std::uint64_t m_FrameCounter = 0;
        mutable std::mutex m_Mutex;
        std::vector<GpuScopeTiming> m_Latest;
        std::uint64_t m_LatestFrame = 0;
    };

    class GpuProfiler
    {
    public:
        explicit GpuProfiler(std::shared_ptr<VulkanDevice> device, std::uint32_t slotCount = 1,
                             std::uint32_t segmentCapacity = 2048);
        ~GpuProfiler();

        GpuProfiler(const GpuProfiler&) = delete;
        GpuProfiler& operator=(const GpuProfiler&) = delete;

        [[nodiscard]] bool Enabled() const noexcept { return m_Ledger.Enabled() && !m_Pools.empty(); }

        void BeginFrame(vk::CommandBuffer cmd, std::uint32_t slot);
        void BeginScope(vk::CommandBuffer cmd, std::string_view name);
        void EndScope(vk::CommandBuffer cmd);
        void Suspend(vk::CommandBuffer cmd);
        void Resume(vk::CommandBuffer cmd);
        bool Collect(std::uint32_t slot);

        [[nodiscard]] std::vector<GpuScopeTiming> Latest() const { return m_Ledger.Latest(); }

    private:
        void WriteBegin(vk::CommandBuffer cmd);
        void WriteEnd(vk::CommandBuffer cmd);

        std::shared_ptr<VulkanDevice> m_Device;
        GpuTimingLedger m_Ledger;
        std::vector<vk::QueryPool> m_Pools;
        std::uint32_t m_Slot = 0;
        std::string m_Scope;
        bool m_ScopeOpen = false;
        std::optional<std::uint32_t> m_Segment;
    };

    class GpuScope
    {
    public:
        GpuScope(GpuProfiler* profiler, const vk::CommandBuffer cmd, const std::string_view name)
            : m_Profiler(profiler && profiler->Enabled() ? profiler : nullptr), m_Cmd(cmd)
        {
            if (m_Profiler) m_Profiler->BeginScope(m_Cmd, name);
        }

        ~GpuScope()
        {
            if (m_Profiler) m_Profiler->EndScope(m_Cmd);
        }

        GpuScope(const GpuScope&) = delete;
        GpuScope& operator=(const GpuScope&) = delete;

    private:
        GpuProfiler* m_Profiler;
        vk::CommandBuffer m_Cmd;
    };
}

export module GPP.Core:Profiling;

import std;

namespace GPP
{
    export struct ProfileRecord
    {
        const char* Name{nullptr};
        std::uint64_t StartNs{0};
        std::uint32_t DurationNs{0};
        std::uint16_t Depth{0};
    };

    export class ProfileRing
    {
    public:
        static constexpr std::size_t kCapacity = 1u << 14;

        ProfileRing(std::string name, std::uint32_t id) : m_Name(std::move(name)), m_Id(id) {}

        void Push(const ProfileRecord& record) noexcept
        {
            const auto head = m_Head.load(std::memory_order_relaxed);
            auto& slot = m_Slots[head & (kCapacity - 1)];
            slot.Name.store(reinterpret_cast<std::uintptr_t>(record.Name), std::memory_order_relaxed);
            slot.Start.store(record.StartNs, std::memory_order_relaxed);
            slot.Meta.store((static_cast<std::uint64_t>(record.DurationNs) << 16) | record.Depth, std::memory_order_relaxed);
            m_Head.store(head + 1, std::memory_order_release);
        }

        [[nodiscard]] std::uint64_t Head() const noexcept { return m_Head.load(std::memory_order_acquire); }

        std::uint64_t Read(std::uint64_t from, std::vector<ProfileRecord>& out) const
        {
            const auto head = m_Head.load(std::memory_order_acquire);
            auto first = std::max<std::uint64_t>(from, head > kCapacity ? head - kCapacity : 0);
            const auto base = out.size();
            for (auto i = first; i < head; ++i)
            {
                const auto& slot = m_Slots[i & (kCapacity - 1)];
                const auto meta = slot.Meta.load(std::memory_order_relaxed);
                out.push_back(ProfileRecord{reinterpret_cast<const char*>(slot.Name.load(std::memory_order_relaxed)),
                                            slot.Start.load(std::memory_order_relaxed),
                                            static_cast<std::uint32_t>(meta >> 16),
                                            static_cast<std::uint16_t>(meta & 0xFFFF)});
            }
            std::atomic_thread_fence(std::memory_order_acquire);
            const auto headAfter = m_Head.load(std::memory_order_relaxed);
            const auto lostBelow = headAfter > kCapacity ? headAfter - kCapacity : 0;
            if (lostBelow > first)
            {
                const auto lost = std::min<std::uint64_t>(lostBelow - first, head - first);
                out.erase(out.begin() + static_cast<std::ptrdiff_t>(base),
                          out.begin() + static_cast<std::ptrdiff_t>(base + lost));
            }
            return head;
        }

        [[nodiscard]] const std::string& Name() const noexcept { return m_Name; }
        [[nodiscard]] std::uint32_t Id() const noexcept { return m_Id; }
        [[nodiscard]] bool Retired() const noexcept { return m_Retired.load(std::memory_order_acquire); }
        void Retire() noexcept { m_Retired.store(true, std::memory_order_release); }
        void Rename(std::string name)
        {
            std::scoped_lock lock(m_NameMutex);
            m_Name = std::move(name);
        }
        [[nodiscard]] std::string CurrentName() const
        {
            std::scoped_lock lock(m_NameMutex);
            return m_Name;
        }

        std::uint16_t Depth{0}; // owner thread only

    private:
        struct Slot
        {
            std::atomic<std::uintptr_t> Name{0};
            std::atomic<std::uint64_t> Start{0};
            std::atomic<std::uint64_t> Meta{0};
        };

        std::string m_Name;
        std::uint32_t m_Id;
        mutable std::mutex m_NameMutex;
        std::atomic<bool> m_Retired{false};
        std::atomic<std::uint64_t> m_Head{0};
        std::array<Slot, kCapacity> m_Slots{};
    };

    export class Profiler
    {
    public:
        static void SetEnabled(bool enabled) noexcept;
        [[nodiscard]] static bool IsEnabled() noexcept;
        static void SetThreadName(std::string_view name);
        [[nodiscard]] static const char* Intern(std::string_view name);
        [[nodiscard]] static std::uint64_t NowNs() noexcept;
        [[nodiscard]] static std::vector<std::shared_ptr<ProfileRing>> Rings();
    };

    export class ProfileZone
    {
    public:
        explicit ProfileZone(const char* name) noexcept;
        ~ProfileZone();
        void End() noexcept;
        ProfileZone(const ProfileZone&) = delete;
        ProfileZone& operator=(const ProfileZone&) = delete;

    private:
        const char* m_Name;
        std::uint64_t m_Start{0};
        ProfileRing* m_Ring{nullptr};
        std::uint16_t m_Depth{0};
    };

    export struct ZoneStat
    {
        const char* Name{nullptr};
        std::uint32_t Count{0};
        double TotalMs{0.0};
        double MaxMs{0.0};
        std::uint16_t MinDepth{0};
    };

    export [[nodiscard]] std::vector<ZoneStat> AggregateZones(std::span<const ProfileRecord> records);

    export struct ZoneRow
    {
        std::string Name;
        std::uint32_t Calls{0};  // in the latest update
        double LastMs{0.0};      // total in the latest update
        double AvgMs{0.0};       // mean per call over the window
        double MaxMs{0.0};       // max single call over the window
        std::uint16_t Depth{0};
    };

    export struct ThreadProfile
    {
        std::string Name;
        std::uint32_t Id{0};
        std::vector<ZoneRow> Zones;           // sorted by name
        std::vector<ProfileRecord> Timeline;  // the window, ordered by start
    };

    export class ProfileCollector
    {
    public:
        explicit ProfileCollector(double windowSeconds = 2.0) : m_WindowNs(static_cast<std::uint64_t>(windowSeconds * 1e9)) {}

        void Update();
        void Update(std::uint32_t threadId, const std::string& name, std::span<const ProfileRecord> fresh, std::uint64_t nowNs);

        [[nodiscard]] const std::vector<ThreadProfile>& Threads() const noexcept { return m_Profiles; }
        [[nodiscard]] std::uint64_t NowNs() const noexcept { return m_Now; }
        [[nodiscard]] std::uint64_t WindowNs() const noexcept { return m_WindowNs; }

    private:
        struct State
        {
            std::uint64_t Cursor{0};
            std::vector<ProfileRecord> Window;
        };

        std::uint64_t m_WindowNs;
        std::uint64_t m_Now{0};
        std::map<std::uint32_t, State> m_States;
        std::vector<ThreadProfile> m_Profiles;
    };
}

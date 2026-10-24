module GPP.Core;

import :Profiling;
import std;

namespace GPP
{
    namespace
    {
        std::atomic<bool> g_Enabled{false};

        struct Registry
        {
            std::mutex Mutex;
            std::vector<std::shared_ptr<ProfileRing>> Rings;
            std::deque<std::string> Names;
            std::unordered_map<std::string, const char*> Interned;
            std::uint32_t NextId{1};
        };

        Registry& GetRegistry()
        {
            static Registry* registry = new Registry; // outlives thread_local destructors at exit
            return *registry;
        }

        struct ThreadSlot
        {
            std::shared_ptr<ProfileRing> Ring;
            ~ThreadSlot()
            {
                if (Ring) Ring->Retire();
            }
        };

        thread_local ThreadSlot t_Slot;

        ProfileRing* ThisRing()
        {
            if (t_Slot.Ring) return t_Slot.Ring.get();
            auto& registry = GetRegistry();
            std::scoped_lock lock(registry.Mutex);
            const auto id = registry.NextId++;
            t_Slot.Ring = std::make_shared<ProfileRing>(std::format("thread {}", id), id);
            registry.Rings.push_back(t_Slot.Ring);
            return t_Slot.Ring.get();
        }
    }

    void Profiler::SetEnabled(const bool enabled) noexcept { g_Enabled.store(enabled, std::memory_order_relaxed); }
    bool Profiler::IsEnabled() noexcept { return g_Enabled.load(std::memory_order_relaxed); }

    void Profiler::SetThreadName(const std::string_view name)
    {
        ThisRing()->Rename(std::string(name));
    }

    const char* Profiler::Intern(const std::string_view name)
    {
        auto& registry = GetRegistry();
        std::scoped_lock lock(registry.Mutex);
        const auto it = registry.Interned.find(std::string(name));
        if (it != registry.Interned.end()) return it->second;
        const auto& stored = registry.Names.emplace_back(name);
        registry.Interned.emplace(stored, stored.c_str());
        return stored.c_str();
    }

    std::uint64_t Profiler::NowNs() noexcept
    {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    std::vector<std::shared_ptr<ProfileRing>> Profiler::Rings()
    {
        auto& registry = GetRegistry();
        std::scoped_lock lock(registry.Mutex);
        return registry.Rings;
    }

    ProfileZone::ProfileZone(const char* name) noexcept : m_Name(nullptr)
    {
        if (!g_Enabled.load(std::memory_order_relaxed)) return;
        m_Ring = ThisRing();
        m_Name = name;
        m_Depth = m_Ring->Depth++;
        m_Start = Profiler::NowNs();
    }

    ProfileZone::~ProfileZone() { End(); }

    void ProfileZone::End() noexcept
    {
        if (!m_Name) return;
        const auto end = Profiler::NowNs();
        m_Ring->Depth = m_Depth;
        const auto duration = std::min<std::uint64_t>(end - m_Start, std::numeric_limits<std::uint32_t>::max());
        m_Ring->Push(ProfileRecord{m_Name, m_Start, static_cast<std::uint32_t>(duration), m_Depth});
        m_Name = nullptr;
    }

    std::vector<ZoneStat> AggregateZones(const std::span<const ProfileRecord> records)
    {
        std::vector<ZoneStat> stats;
        std::unordered_map<std::string_view, std::size_t> index;
        for (const auto& record : records)
        {
            if (!record.Name) continue;
            const auto [it, inserted] = index.try_emplace(record.Name, stats.size());
            if (inserted) stats.push_back(ZoneStat{record.Name, 0, 0.0, 0.0, record.Depth});
            auto& stat = stats[it->second];
            const double ms = static_cast<double>(record.DurationNs) * 1e-6;
            ++stat.Count;
            stat.TotalMs += ms;
            stat.MaxMs = std::max(stat.MaxMs, ms);
            stat.MinDepth = std::min(stat.MinDepth, record.Depth);
        }
        return stats;
    }

    void ProfileCollector::Update(const std::uint32_t threadId, const std::string& name,
                                  const std::span<const ProfileRecord> fresh, const std::uint64_t nowNs)
    {
        m_Now = nowNs;
        auto& state = m_States[threadId];
        state.Window.insert(state.Window.end(), fresh.begin(), fresh.end());
        const auto cutoff = nowNs > m_WindowNs ? nowNs - m_WindowNs : 0;
        std::erase_if(state.Window, [&](const ProfileRecord& r) { return r.StartNs + r.DurationNs < cutoff; });

        const auto latest = AggregateZones(fresh);
        const auto windowed = AggregateZones(state.Window);
        ThreadProfile profile;
        profile.Name = name;
        profile.Id = threadId;
        for (const auto& stat : windowed)
        {
            ZoneRow row;
            row.Name = stat.Name;
            row.AvgMs = stat.Count ? stat.TotalMs / stat.Count : 0.0;
            row.MaxMs = stat.MaxMs;
            row.Depth = stat.MinDepth;
            if (const auto it = std::ranges::find(latest, stat.Name, &ZoneStat::Name); it != latest.end())
            {
                row.Calls = it->Count;
                row.LastMs = it->TotalMs;
            }
            profile.Zones.push_back(std::move(row));
        }
        std::ranges::sort(profile.Zones, {}, &ZoneRow::Name);
        profile.Timeline = state.Window;
        std::ranges::stable_sort(profile.Timeline, {}, &ProfileRecord::StartNs);

        const auto slot = std::ranges::find(m_Profiles, threadId, &ThreadProfile::Id);
        if (slot != m_Profiles.end()) *slot = std::move(profile);
        else m_Profiles.push_back(std::move(profile));
    }

    void ProfileCollector::Update()
    {
        const auto now = Profiler::NowNs();
        std::vector<ProfileRecord> fresh;
        for (const auto& ring : Profiler::Rings())
        {
            fresh.clear();
            auto& state = m_States[ring->Id()];
            state.Cursor = ring->Read(state.Cursor, fresh);
            Update(ring->Id(), ring->CurrentName(), fresh, now);
        }
        std::erase_if(m_Profiles, [&](const ThreadProfile& p)
        {
            return p.Timeline.empty() && std::ranges::none_of(Profiler::Rings(), [&](const auto& ring) { return ring->Id() == p.Id && !ring->Retired(); });
        });
    }
}

export module GPP.Core:LogBuffer;

import std;

namespace GPP
{
    export enum class LogLevel {
        Trace,
        Debug,
        Info,
        Warn,
        Error,
        Critical,
        Off
    };

    export struct LogEntry {
        std::uint64_t Id = 0;
        std::chrono::system_clock::time_point Time{};
        LogLevel Level = LogLevel::Info;
        std::string Source;
        std::string Message;
    };

    export class LogBuffer {
    public:
        explicit LogBuffer(const std::size_t capacity = 8192) : m_Capacity(std::max<std::size_t>(capacity, 1)) {}

        void Append(LogEntry entry) {
            std::scoped_lock lock(m_Mutex);
            entry.Id = ++m_LastId;
            m_Entries.push_back(std::move(entry));
            while (m_Entries.size() > m_Capacity) m_Entries.pop_front();
        }

        std::uint64_t CopySince(const std::uint64_t afterId, std::vector<LogEntry>& out) const {
            std::scoped_lock lock(m_Mutex);
            if (m_Entries.empty() || m_Entries.back().Id <= afterId) return afterId;
            const auto first = std::ranges::upper_bound(m_Entries, afterId, {}, &LogEntry::Id);
            out.insert(out.end(), first, m_Entries.end());
            return m_Entries.back().Id;
        }

        bool TryCopyTail(const std::size_t count, std::vector<LogEntry>& out) const {
            std::unique_lock lock(m_Mutex, std::try_to_lock);
            if (!lock.owns_lock()) return false;
            const auto skip = m_Entries.size() > count ? m_Entries.size() - count : 0;
            out.assign(m_Entries.begin() + static_cast<std::ptrdiff_t>(skip), m_Entries.end());
            return true;
        }

        [[nodiscard]] std::uint64_t LastId() const {
            std::scoped_lock lock(m_Mutex);
            return m_LastId;
        }

        [[nodiscard]] std::size_t Capacity() const noexcept { return m_Capacity; }

        void Clear() {
            std::scoped_lock lock(m_Mutex);
            m_Entries.clear();
        }

    private:
        mutable std::mutex m_Mutex;
        std::deque<LogEntry> m_Entries;
        std::size_t m_Capacity;
        std::uint64_t m_LastId = 0;
    };
}

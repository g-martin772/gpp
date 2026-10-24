export module GPP.Simulation:ContentRevision;

import std;

namespace GPP
{
    export struct RevisionStamp
    {
        std::uint64_t Before{0};
        std::uint64_t After{0};
        std::uint64_t Epoch{0};
    };

    export struct RevisionMove
    {
        std::uint64_t From{0};
        std::uint64_t To{0};
        std::uint64_t Epoch{0};
    };

    export class RevisionCounter
    {
    public:
        [[nodiscard]] std::uint64_t Current() const noexcept { return m_Current.load(std::memory_order_acquire); }
        [[nodiscard]] std::uint64_t Epoch() const noexcept { return m_Epoch; }

        void Changed() noexcept
        {
            ++m_Epoch;
            m_Current.store(++m_Next, std::memory_order_release);
        }

        RevisionStamp Recorded() noexcept
        {
            RevisionStamp stamp{Current(), ++m_Next, m_Epoch};
            m_Current.store(stamp.After, std::memory_order_release);
            return stamp;
        }

        void Moved(const RevisionMove& move) noexcept
        {
            const bool trusted = move.Epoch != 0 && move.Epoch == m_Epoch && Current() == move.From;
            m_Current.store(trusted ? move.To : ++m_Next, std::memory_order_release);
        }

        void Restore(const std::uint64_t revision) noexcept
        {
            ++m_Epoch;
            m_Current.store(revision, std::memory_order_release);
        }

    private:
        std::atomic<std::uint64_t> m_Current{0};
        std::uint64_t m_Next{0};
        std::uint64_t m_Epoch{1};
    };
}

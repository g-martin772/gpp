export module GPP.Simulation:Random;

import std;

namespace GPP
{
    // Lock-free splitmix64 stream: the whole state is one counter, so it can be saved and restored exactly.
    export class SimulationRandom
    {
    public:
        explicit SimulationRandom(std::uint64_t seed = 0) { Seed(seed); }

        void Seed(std::uint64_t seed)
        {
            if (seed == 0)
            {
                std::random_device rd;
                seed = (static_cast<std::uint64_t>(rd()) << 32) ^ rd();
            }
            m_Seed.store(seed, std::memory_order_relaxed);
            m_State.store(seed, std::memory_order_relaxed);
        }

        [[nodiscard]] std::uint64_t SeedValue() const noexcept { return m_Seed.load(std::memory_order_relaxed); }
        [[nodiscard]] std::uint64_t State() const noexcept { return m_State.load(std::memory_order_relaxed); }
        void SetState(const std::uint64_t state) noexcept { m_State.store(state, std::memory_order_relaxed); }

        [[nodiscard]] std::uint64_t NextU64() noexcept
        {
            std::uint64_t z = m_State.fetch_add(0x9E3779B97F4A7C15ull, std::memory_order_relaxed) + 0x9E3779B97F4A7C15ull;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
            return z ^ (z >> 31);
        }

        // [0, 1)
        [[nodiscard]] double NextDouble() noexcept
        {
            return static_cast<double>(NextU64() >> 11) * (1.0 / 9007199254740992.0);
        }

        // Inclusive on both ends.
        [[nodiscard]] std::int64_t NextInt(std::int64_t low, std::int64_t high) noexcept
        {
            if (high < low) std::swap(low, high);
            const auto span = static_cast<std::uint64_t>(high - low) + 1;
            return span == 0 ? static_cast<std::int64_t>(NextU64()) : low + static_cast<std::int64_t>(NextU64() % span);
        }

    private:
        std::atomic<std::uint64_t> m_Seed{0};
        std::atomic<std::uint64_t> m_State{0};
    };
}

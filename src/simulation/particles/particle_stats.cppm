export module GPP.Simulation:Particles.Stats;

import std;
import glm;
import :Particles.Types;
import :Particles.Model;

export namespace GPP
{
    // Fixed-point sums of weight * quantity, converted to double. Default sink quantities per unit weight:
    // 0 mass, 1-3 velocity, 4-6 position x velocity about the sink centre, 7 specific energy.
    // Default collider quantities: 0 absorbed mass, 1-3 absorbed momentum, 4-6 impulse handed to particles.
    struct ParticleAccumulatorTotals
    {
        std::uint64_t Count{0};
        std::uint64_t Killed{0};
        std::array<double, kParticleAccumulators> Raw{};

        [[nodiscard]] double Mass() const noexcept { return Raw[0]; }
        [[nodiscard]] glm::dvec3 Momentum(const ParticleUnits& units) const noexcept
        {
            return glm::dvec3(Raw[1], Raw[2], Raw[3]) * units.Velocity();
        }
        [[nodiscard]] glm::dvec3 AngularMomentum(const ParticleUnits& units) const noexcept
        {
            return glm::dvec3(Raw[4], Raw[5], Raw[6]) * units.SpecificAngularMomentum();
        }
        [[nodiscard]] double Energy(const ParticleUnits& units) const noexcept { return Raw[7] * units.SpecificEnergy(); }
        // Momentum the host body gained: absorbed particles minus what it pushed into the others
        [[nodiscard]] glm::dvec3 ImpulseOnBody(const ParticleUnits& units) const noexcept
        {
            return (glm::dvec3(Raw[1], Raw[2], Raw[3]) - glm::dvec3(Raw[4], Raw[5], Raw[6])) * units.Velocity();
        }
    };

    struct ParticleAccumulatorBank
    {
        std::vector<std::array<FixedPoint, kParticleAccumulators>> Fixed;
        std::vector<std::uint64_t> Count;
        std::vector<std::uint64_t> Killed;

        void Resize(const std::size_t size)
        {
            Fixed.resize(size);
            Count.resize(size, 0);
            Killed.resize(size, 0);
        }

        void Clear()
        {
            Fixed.clear();
            Count.clear();
            Killed.clear();
        }

        void Merge(const ParticleAccumulatorBank& other)
        {
            Resize(std::max(Fixed.size(), other.Fixed.size()));
            for (std::size_t i = 0; i < other.Fixed.size(); ++i)
            {
                for (std::size_t q = 0; q < kParticleAccumulators; ++q) Fixed[i][q].Value += other.Fixed[i][q].Value;
                Count[i] += other.Count[i];
                Killed[i] += other.Killed[i];
            }
        }

        [[nodiscard]] ParticleAccumulatorTotals Totals(const std::size_t index, const double scale) const
        {
            ParticleAccumulatorTotals totals;
            totals.Count = Count[index];
            totals.Killed = Killed[index];
            for (std::size_t q = 0; q < kParticleAccumulators; ++q) totals.Raw[q] = Fixed[index][q].ToDouble(scale);
            return totals;
        }
    };

    struct ParticleRegionTotals
    {
        std::uint64_t Count{0};
        double Mass{0.0};
        double MeanTemperature{0.0};
        glm::dvec3 MeanVelocity{0.0}; // simulation units
    };

    struct ParticleStats
    {
        std::uint64_t Serial{0};
        double SimTime{0.0};
        std::uint64_t Alive{0};
        std::uint64_t Dropped{0};
        double AliveMass{0.0};
        double MeanTemperature{0.0};
        glm::dvec3 MeanVelocity{0.0}; // simulation units
        std::vector<ParticleAccumulatorTotals> Sinks;
        std::vector<ParticleAccumulatorTotals> Colliders;
        std::vector<ParticleRegionTotals> Regions;
    };

    // What the sim thread and scripts read; the particle system publishes after every advance.
    struct ParticleBoardData
    {
        ParticleStats Stats;
        ParticleUnits Units;
        std::uint64_t Epoch{0};
        std::uint64_t Emitted{0};
        double EmittedMass{0.0};
        std::vector<std::uint64_t> SinkIds;
        std::vector<std::uint64_t> ColliderIds;
        std::vector<std::pair<std::uint64_t, std::uint64_t>> EmitterAlive; // id, particles emitted so far
    };

    class ParticleStatsBoard
    {
    public:
        void Publish(ParticleBoardData data)
        {
            auto next = std::make_shared<const ParticleBoardData>(std::move(data));
            std::scoped_lock lock(m_Mutex);
            m_Data = std::move(next);
        }

        void Clear()
        {
            std::scoped_lock lock(m_Mutex);
            m_Data.reset();
        }

        [[nodiscard]] std::shared_ptr<const ParticleBoardData> Latest() const
        {
            std::scoped_lock lock(m_Mutex);
            return m_Data;
        }

        [[nodiscard]] std::uint64_t Count() const
        {
            const auto data = Latest();
            return data ? data->Stats.Alive : 0;
        }

        [[nodiscard]] std::optional<ParticleAccumulatorTotals> Sink(const std::uint64_t id) const
        {
            const auto data = Latest();
            if (!data) return std::nullopt;
            for (std::size_t i = 0; i < data->SinkIds.size() && i < data->Stats.Sinks.size(); ++i)
            {
                if (data->SinkIds[i] == id) return data->Stats.Sinks[i];
            }
            return std::nullopt;
        }

        [[nodiscard]] std::optional<ParticleAccumulatorTotals> Collider(const std::uint64_t id) const
        {
            const auto data = Latest();
            if (!data) return std::nullopt;
            for (std::size_t i = 0; i < data->ColliderIds.size() && i < data->Stats.Colliders.size(); ++i)
            {
                if (data->ColliderIds[i] == id) return data->Stats.Colliders[i];
            }
            return std::nullopt;
        }

    private:
        mutable std::mutex m_Mutex;
        std::shared_ptr<const ParticleBoardData> m_Data;
    };
}

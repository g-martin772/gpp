export module GPP.Simulation:Particles.Backend;

import std;
import glm;
import :Particles.Types;
import :Particles.Model;
import :Particles.Stats;

export namespace GPP
{
    // Node-centred grid; a particle contributes to the eight surrounding nodes with trilinear (cloud-in-cell) weights.
    struct ParticleGridSpec
    {
        glm::vec3 Origin{0.0f}; // position of node (0, 0, 0)
        float CellSize{1.0f};
        glm::uvec3 Dimensions{1u};
        // GPU fixed-point range of the momentum and temperature channels; the CPU ignores them
        float VelocityReference{1.0f};
        float TemperatureReference{1.0e4f};

        [[nodiscard]] std::size_t NodeCount() const noexcept
        {
            return static_cast<std::size_t>(Dimensions.x) * Dimensions.y * Dimensions.z;
        }
        [[nodiscard]] std::size_t Index(const std::uint32_t x, const std::uint32_t y, const std::uint32_t z) const noexcept
        {
            return (static_cast<std::size_t>(z) * Dimensions.y + y) * Dimensions.x + x;
        }
    };

    // Sums over the nodes: mass [kg], momentum (mass * velocity) and mass * temperature. Particles whose footprint leaves
    // the grid are counted in OutsideMass, so the node sums plus OutsideMass equal the total alive mass.
    struct ParticleGrid
    {
        ParticleGridSpec Spec;
        std::vector<float> Mass;
        std::vector<glm::vec3> Momentum;
        std::vector<float> MassTemperature;
        double OutsideMass{0.0};
    };

    class IParticleBackend
    {
    public:
        virtual ~IParticleBackend() = default;

        // Allocates storage and drops every particle
        virtual void Configure(const ParticleSettings& settings) = 0;
        virtual void Reset() = 0;
        // Takes effect for CPU backends; GPU kernels get the same hooks as shader includes
        virtual void SetHooks(ParticleHooks hooks) { (void)hooks; }

        // Emit, sort into stability bins, advance every bin through 2^bin substeps, optionally collect statistics
        virtual void MacroStep(const ParticleMacroStep& step) = 0;

        // Latest completed statistics; GPU backends return values one or two frames old
        [[nodiscard]] virtual ParticleStats Stats() = 0;
        // Alive particles in storage order
        virtual void Download(ParticleArrays& out) = 0;
        virtual void Deposit(const ParticleGridSpec& spec, ParticleGrid& out) = 0;

        [[nodiscard]] virtual std::string_view Name() const noexcept = 0;
        [[nodiscard]] virtual const ParticleSettings& Settings() const noexcept = 0;
    };

    // Exact single-threaded-order-independent reference; also the fallback without a GPU.
    class CpuParticleBackend final : public IParticleBackend
    {
    public:
        explicit CpuParticleBackend(std::uint32_t threads = 0);

        void Configure(const ParticleSettings& settings) override;
        void Reset() override;
        void SetHooks(ParticleHooks hooks) override { m_Hooks = std::move(hooks); }
        void MacroStep(const ParticleMacroStep& step) override;
        [[nodiscard]] ParticleStats Stats() override { return m_Stats; }
        void Download(ParticleArrays& out) override;
        void Deposit(const ParticleGridSpec& spec, ParticleGrid& out) override;

        [[nodiscard]] std::string_view Name() const noexcept override { return "cpu"; }
        [[nodiscard]] const ParticleSettings& Settings() const noexcept override { return m_Settings; }
        [[nodiscard]] const ParticleArrays& Particles() const noexcept { return m_Particles; }

    private:
        struct Tally;

        void Emit(const ParticleMacroStep& step);
        void SortIntoBins(const ParticleMacroStep& step);
        void Advance(const ParticleMacroStep& step, std::size_t first, std::size_t last, std::uint32_t bin, Tally& tally);
        void CollectStats(const ParticleMacroStep& step);
        [[nodiscard]] float AccumulatorScale() const noexcept
        {
            return m_Settings.ReferenceWeight * m_Settings.AccumulatorResolution;
        }

        ParticleSettings m_Settings;
        ParticleHooks m_Hooks;
        ParticleArrays m_Particles;
        ParticleArrays m_Scratch;
        std::vector<std::size_t> m_BinStart;
        std::vector<float> m_DragMass;
        ParticleAccumulatorBank m_SinkBank;
        ParticleAccumulatorBank m_ColliderBank;
        ParticleStats m_Stats;
        std::uint64_t m_Dropped{0};
        std::uint64_t m_Serial{0};
        double m_SimTime{0.0};
        std::uint32_t m_Threads{1};
    };
}

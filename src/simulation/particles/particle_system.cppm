export module GPP.Simulation:Particles.System;

import std;
import glm;
import GPP.Core;
import :Particles.Types;
import :Particles.Model;
import :Particles.Stats;
import :Particles.Backend;
import :Particles.Scene;

export namespace GPP
{
    // Host-side gas model for Drag colliders: density in kg/m^3 and gas velocity in m/s at a world position.
    struct ParticleAmbient
    {
        std::function<double(const glm::dvec3&)> Density;
        std::function<glm::dvec3(const glm::dvec3&)> Velocity;
        double ConstantDensity{0.0};
        glm::dvec3 ConstantVelocity{0.0};
    };

    struct ParticleSystemConfig
    {
        ParticleSettings Settings;
        // Simulated seconds per macro step. Zero makes every Advance one macro step of exactly the requested time.
        // A fixed value makes the result independent of how the total time is split into Advance calls.
        double BaseStep{0.0};
        std::uint32_t MaxMacroSteps{256};
    };

    struct ParticleAdvanceReport
    {
        std::uint32_t MacroSteps{0};
        double Simulated{0.0}; // seconds
        double Backlog{0.0};   // seconds requested but not simulated because of MaxMacroSteps
        std::uint64_t Emitted{0};
        bool Limited{false};
    };

    // Generic particle pipeline on a backend: turns the published scene state into emission, sinks and colliders and runs
    // macro steps. Owned by whichever thread owns the backend (the render thread for GPU backends); not thread safe itself.
    class ParticleSystem
    {
    public:
        ParticleSystem(std::unique_ptr<IParticleBackend> backend, ParticleSystemConfig config,
                       std::shared_ptr<ParticleStatsBoard> board = std::make_shared<ParticleStatsBoard>());

        void SetForce(const ParticleForceParams& force) { m_Force = force; }
        void SetHooks(ParticleHooks hooks);
        void SetAmbient(ParticleAmbient ambient) { m_Ambient = std::move(ambient); }
        void SetHub(std::shared_ptr<ParticleHub> hub) { m_Hub = std::move(hub); }

        // Resets all particles when the epoch changed, otherwise updates emitters, sinks and colliders in place
        void Consume(const ParticleSceneState& state);
        ParticleAdvanceReport Advance(double seconds);
        void Reset();
        // Republishes the board; call after a GPU backend collected a completed frame
        void Refresh() { Publish(); }

        [[nodiscard]] IParticleBackend& Backend() noexcept { return *m_Backend; }
        [[nodiscard]] const ParticleSystemConfig& Config() const noexcept { return m_Config; }
        [[nodiscard]] const std::shared_ptr<ParticleStatsBoard>& Board() const noexcept { return m_Board; }
        [[nodiscard]] double SimTime() const noexcept { return m_SimTime; }
        [[nodiscard]] std::uint64_t Epoch() const noexcept { return m_Epoch; }
        [[nodiscard]] std::uint64_t Emitted() const noexcept { return m_EmittedCount; }
        [[nodiscard]] double EmittedMass() const noexcept { return m_EmittedMass; }
        [[nodiscard]] std::uint64_t EmittedBy(std::uint64_t emitterId) const;
        [[nodiscard]] static ParticleEmitterParams MakeEmitterParams(const ParticleEmitterDef& def, const ParticleFrame& frame,
                                                                     std::uint32_t slot, std::uint64_t firstSerial);
        // Particles per second and kilograms per particle of an emitter
        [[nodiscard]] static std::pair<double, double> RateAndWeight(const ParticleEmitterComponent& emitter);

    private:
        struct EmitterState
        {
            std::uint64_t Id{0};
            std::uint32_t Slot{0};
            std::uint64_t NextSerial{0};
            std::uint64_t Emitted{0};
            double Credit{0.0};
            bool Released{false};
        };

        void Publish();
        [[nodiscard]] std::uint32_t SlotFor(std::vector<std::uint64_t>& table, std::uint64_t id, std::uint32_t limit);

        std::unique_ptr<IParticleBackend> m_Backend;
        ParticleSystemConfig m_Config;
        std::shared_ptr<ParticleStatsBoard> m_Board;
        std::shared_ptr<ParticleHub> m_Hub;
        ParticleForceParams m_Force;
        ParticleAmbient m_Ambient;
        ParticleSceneState m_State;
        std::vector<EmitterState> m_Emitters;
        std::vector<std::uint64_t> m_SinkSlots;
        std::vector<std::uint64_t> m_ColliderSlots;
        std::uint64_t m_Epoch{std::numeric_limits<std::uint64_t>::max()};
        std::uint64_t m_LastBurst{0};
        std::uint64_t m_EmittedCount{0};
        double m_EmittedMass{0.0};
        double m_SimTime{0.0};
        double m_Pending{0.0};
    };
}

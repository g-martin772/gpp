export module GPP.Simulation:Particles.Model;

import std;
import glm;
import :Particles.Types;

export namespace GPP
{
    enum class ParticleForceKind : std::uint32_t { None, Newtonian, PaczynskiWiita, Custom };

    // Mirrors `ForceParams` in shaders/particles/particle_common.glsl. Custom forces read User.
    struct ParticleForceParams
    {
        float Mu{0.0f};
        float Softening{0.0f};
        float SchwarzschildRadius{0.0f};
        std::uint32_t Kind{static_cast<std::uint32_t>(ParticleForceKind::None)};
        std::array<float, kParticleUserParams> User{};

        [[nodiscard]] static ParticleForceParams Newtonian(const float mu, const float softening = 0.0f)
        {
            return {mu, softening, 0.0f, static_cast<std::uint32_t>(ParticleForceKind::Newtonian), {}};
        }
        [[nodiscard]] static ParticleForceParams PaczynskiWiita(const float mu, const float schwarzschildRadius)
        {
            return {mu, 0.0f, schwarzschildRadius, static_cast<std::uint32_t>(ParticleForceKind::PaczynskiWiita), {}};
        }
    };

    // Positions, velocities and times are in ParticleUnits, masses in kg. Layouts match the std430 structs in the shaders.
    struct ParticleEmitterParams
    {
        glm::vec4 PositionShape{0.0f};  // xyz origin, w shape
        glm::vec4 AxisRadius{0.0f, 0.0f, 1.0f, 1.0f}; // xyz unit axis, w outer radius
        glm::vec4 Params0{0.0f};        // x inner radius, y thickness, z cone half angle, w speed
        glm::vec4 Params1{0.0f};        // x dispersion sigma, y temperature, z weight, w velocity mode
        glm::vec4 Bulk{0.0f};           // xyz bulk velocity
        glm::vec4 Target{0.0f};         // xyz stream target
        glm::uvec4 Ids{0u};             // x seed low, y seed high, z species slot, w unused
        glm::uvec4 Batch{0u};           // x first serial, y count this step, z first index in the step's new-particle list, w leading burst particles
    };

    struct ParticleSinkParams
    {
        glm::vec4 CenterRadius{0.0f};   // xyz centre, w radius
        glm::uvec4 Info{0u};            // x mode, y accounting, z slot
    };

    struct ParticleColliderParams
    {
        glm::vec4 CenterRadius{0.0f};        // xyz centre at the start of the macro step, w sphere radius
        glm::vec4 HalfExtentsShape{0.0f};    // xyz box half extents, w shape
        glm::vec4 Axis0{1.0f, 0.0f, 0.0f, 0.0f}; // rows of the world-to-body rotation
        glm::vec4 Axis1{0.0f, 1.0f, 0.0f, 0.0f};
        glm::vec4 Axis2{0.0f, 0.0f, 1.0f, 0.0f};
        glm::vec4 VelocityRestitution{0.0f}; // xyz body velocity, w restitution
        glm::vec4 DragForce{0.0f};           // xyz force on the gas inside the shape, kg * Length / Time^2
        glm::uvec4 Info{0u};                 // x response, y slot
    };

    struct ParticleRegionParams
    {
        glm::vec4 CenterRadius{0.0f};
    };

    static_assert(sizeof(ParticleForceParams) == 64);
    static_assert(sizeof(ParticleEmitterParams) == 128);
    static_assert(sizeof(ParticleSinkParams) == 32);
    static_assert(sizeof(ParticleColliderParams) == 128);

    struct ParticleView
    {
        glm::vec3& Position;
        glm::vec3& Velocity;
        float& Weight;
        float& Temperature;
        float& Age;
        std::uint32_t Species;
        std::uint32_t Owner;
    };

    // CPU counterparts of the shader hooks in shaders/particles/hooks.glsl. Everything optional.
    struct ParticleHooks
    {
        // Replaces the built-in acceleration when ParticleForceParams::Kind is Custom
        std::function<glm::vec3(const glm::vec3& position, const glm::vec3& velocity, const ParticleForceParams&)> Acceleration;
        std::function<float(const glm::vec3& position, const glm::vec3& velocity, float eta, const ParticleForceParams&)> MaxStableStep;
        std::function<float(const glm::vec3& position, const ParticleForceParams&)> Potential;
        // Runs at the start of every substep; returning true replaces the built-in kick-drift-kick of that substep
        std::function<bool(ParticleView&, float step, const ParticleForceParams&)> Step;
        // Runs once per particle after all its substeps of a macro step (viscosity)
        std::function<void(ParticleView&, float baseStep, const ParticleForceParams&)> PostMacroStep;
        // Fills the per-unit-weight quantities accumulated by a sink; the default is mass, momentum, angular momentum, energy
        std::function<void(const ParticleView&, const glm::vec3& sinkCentre, const ParticleForceParams&,
                           std::array<float, kParticleAccumulators>&)> AbsorbQuantities;
    };

    struct ParticleSettings
    {
        ParticleUnits Units;
        std::uint32_t Capacity{1u << 20};
        std::uint32_t MaxBin{0};
        float Eta{0.02f};
        // One unit of fixed-point accumulation is this fraction of the reference weight
        float AccumulatorResolution{0x1p-24f};
        float ReferenceWeight{1.0f};
    };

    struct ParticleMacroStep
    {
        float BaseStep{0.0f};
        double SimTime{0.0};
        ParticleForceParams Force;
        std::vector<ParticleEmitterParams> Emitters;
        std::vector<ParticleSinkParams> Sinks;
        std::vector<ParticleColliderParams> Colliders;
        std::vector<ParticleRegionParams> Regions;
        bool CollectStats{false};
    };
}

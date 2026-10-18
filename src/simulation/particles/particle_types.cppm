export module GPP.Simulation:Particles.Types;

import std;
import glm;
import GPP.Core;

export namespace GPP
{
    // Particle state lives in scaled simulation units so float32 stays in range: positions in Length metres,
    // velocities in Length/Time metres per second, masses stay in kilograms.
    struct ParticleUnits
    {
        double Length{1.0};
        double Time{1.0};

        [[nodiscard]] static ParticleUnits Geometrized(const double centralMassKg)
        {
            return {Units::GravitationalRadius(centralMassKg), Units::GeometricTime(centralMassKg)};
        }

        [[nodiscard]] double Velocity() const noexcept { return Length / Time; }
        [[nodiscard]] double Acceleration() const noexcept { return Length / (Time * Time); }
        [[nodiscard]] double SpecificEnergy() const noexcept { return Length * Length / (Time * Time); }
        [[nodiscard]] double SpecificAngularMomentum() const noexcept { return Length * Length / Time; }
        [[nodiscard]] double Force() const noexcept { return Length / (Time * Time); }

        // G M in simulation units
        [[nodiscard]] double Mu(const double massKg) const noexcept
        {
            return Units::kGravitationalConstant * massKg * Time * Time / (Length * Length * Length);
        }

        [[nodiscard]] glm::vec3 ToSim(const glm::dvec3& metres) const { return glm::vec3(metres / Length); }
        [[nodiscard]] glm::vec3 VelocityToSim(const glm::dvec3& metresPerSecond) const
        {
            return glm::vec3(metresPerSecond / Velocity());
        }
    };

    enum class ParticleEmitterShape : std::uint8_t { Point, Sphere, Shell, Disk, Ring, Cone, Stream };
    enum class ParticleRateMode : std::uint8_t { ParticleRate, MassRate };
    enum class ParticleVelocityMode : std::uint8_t { Axial, Radial, Azimuthal };
    enum class ParticleSinkMode : std::uint8_t { Absorb, Kill };
    enum class ParticleColliderResponse : std::uint8_t { Ignore, Collide, Drag, Absorb };
    enum class ParticleColliderShape : std::uint8_t { Sphere, Box };

    namespace ParticleFlag
    {
        inline constexpr std::uint32_t kAlive = 1u;
        inline constexpr std::uint32_t kBinShift = 8u;
        inline constexpr std::uint32_t kBinMask = 0xFFu;
        inline constexpr std::uint32_t kSpeciesMask = 0xFFFFu;
        inline constexpr std::uint32_t kOwnerShift = 16u;
    }

    inline constexpr std::uint32_t kMaxParticleEmitters = 64;
    inline constexpr std::uint32_t kMaxParticleSinks = 16;
    inline constexpr std::uint32_t kMaxParticleColliders = 16;
    inline constexpr std::uint32_t kMaxParticleRegions = 8;
    inline constexpr std::uint32_t kMaxParticleBins = 12;
    inline constexpr std::uint32_t kParticleAccumulators = 8;
    inline constexpr std::uint32_t kParticleUserParams = 12;

    // Struct-of-arrays, same layout as the GPU buffers.
    //   PosMass: xyz position, w mass weight [kg]
    //   VelTemp: xyz velocity, w temperature [K]
    //   Meta:    x species | owner << 16, y flags | bin << 8, z age (float bits), w emitter serial
    struct ParticleArrays
    {
        std::vector<glm::vec4> PosMass;
        std::vector<glm::vec4> VelTemp;
        std::vector<glm::uvec4> Meta;

        [[nodiscard]] std::size_t Size() const noexcept { return PosMass.size(); }
        void Resize(const std::size_t count)
        {
            PosMass.resize(count);
            VelTemp.resize(count);
            Meta.resize(count);
        }
        void Clear()
        {
            PosMass.clear();
            VelTemp.clear();
            Meta.clear();
        }

        [[nodiscard]] static bool Alive(const glm::uvec4& meta) noexcept { return (meta.y & ParticleFlag::kAlive) != 0; }
        [[nodiscard]] static std::uint32_t Species(const glm::uvec4& meta) noexcept
        {
            return meta.x & ParticleFlag::kSpeciesMask;
        }
        [[nodiscard]] static std::uint32_t Owner(const glm::uvec4& meta) noexcept { return meta.x >> ParticleFlag::kOwnerShift; }
        [[nodiscard]] static std::uint32_t Bin(const glm::uvec4& meta) noexcept
        {
            return (meta.y >> ParticleFlag::kBinShift) & ParticleFlag::kBinMask;
        }
        [[nodiscard]] static float Age(const glm::uvec4& meta) noexcept { return std::bit_cast<float>(meta.z); }

        // FNV-1a over every alive particle in storage order
        [[nodiscard]] std::uint64_t Hash() const noexcept
        {
            std::uint64_t hash = 1469598103934665603ull;
            const auto mix = [&hash](const std::uint32_t word)
            {
                for (int shift = 0; shift < 32; shift += 8)
                {
                    hash ^= (word >> shift) & 0xFFu;
                    hash *= 1099511628211ull;
                }
            };
            for (std::size_t i = 0; i < Size(); ++i)
            {
                if (!Alive(Meta[i])) continue;
                for (int c = 0; c < 4; ++c) mix(std::bit_cast<std::uint32_t>(PosMass[i][c]));
                for (int c = 0; c < 4; ++c) mix(std::bit_cast<std::uint32_t>(VelTemp[i][c]));
                mix(Meta[i].x);
                mix(Meta[i].y);
                mix(Meta[i].z);
                mix(Meta[i].w);
            }
            return hash;
        }
    };

    // Counter-based generator shared bit for bit with shaders/particles/particle_rng.glsl.
    namespace ParticleRng
    {
        using Counter = std::array<std::uint32_t, 4>;
        using Key = std::array<std::uint32_t, 2>;

        [[nodiscard]] constexpr Counter Philox4x32(Counter counter, Key key) noexcept
        {
            constexpr std::uint32_t kMul0 = 0xD2511F53u;
            constexpr std::uint32_t kMul1 = 0xCD9E8D57u;
            constexpr std::uint32_t kWeyl0 = 0x9E3779B9u;
            constexpr std::uint32_t kWeyl1 = 0xBB67AE85u;
            for (int round = 0; round < 10; ++round)
            {
                const std::uint64_t product0 = static_cast<std::uint64_t>(kMul0) * counter[0];
                const std::uint64_t product1 = static_cast<std::uint64_t>(kMul1) * counter[2];
                counter = {static_cast<std::uint32_t>(product1 >> 32) ^ counter[1] ^ key[0],
                           static_cast<std::uint32_t>(product1),
                           static_cast<std::uint32_t>(product0 >> 32) ^ counter[3] ^ key[1],
                           static_cast<std::uint32_t>(product0)};
                key[0] += kWeyl0;
                key[1] += kWeyl1;
            }
            return counter;
        }

        // [0, 1) with 24 random bits
        [[nodiscard]] constexpr float ToUnit(const std::uint32_t bits) noexcept
        {
            return static_cast<float>(bits >> 8) * 0x1p-24f;
        }

        struct Stream
        {
            std::uint64_t Seed{0};
            std::uint32_t Emitter{0};

            [[nodiscard]] Counter Draw(const std::uint32_t index, const std::uint32_t round) const noexcept
            {
                return Philox4x32({index, round, Emitter, 0u},
                                  {static_cast<std::uint32_t>(Seed), static_cast<std::uint32_t>(Seed >> 32)});
            }
        };

        // Standard normal pair from two uniforms in [0, 1)
        [[nodiscard]] inline std::pair<float, float> Normal(const float u1, const float u2) noexcept
        {
            const float radius = std::sqrt(-2.0f * std::log(1.0f - u1));
            const float angle = 6.28318530717958647692f * u2;
            return {radius * std::cos(angle), radius * std::sin(angle)};
        }
    }

    // Smallest bin k whose step dt0 / 2^k does not exceed the stability estimate, clamped to maxBin
    [[nodiscard]] inline std::uint32_t AssignParticleBin(const float baseStep, const float stableStep,
                                                         const std::uint32_t maxBin) noexcept
    {
        if (!(stableStep > 0.0f)) return maxBin;
        const float ratio = baseStep / stableStep;
        if (!(ratio > 1.0f)) return 0;
        const std::uint32_t bits = std::bit_cast<std::uint32_t>(ratio);
        int exponent = static_cast<int>((bits >> 23) & 0xFFu) - 127;
        if ((bits & 0x7FFFFFu) != 0) ++exponent;
        return static_cast<std::uint32_t>(std::clamp(exponent, 0, static_cast<int>(maxBin)));
    }

    // Order-independent accumulation of float quantities: each value becomes a signed 64-bit count of
    // `scale` units, split exactly into magnitude hi/lo words so shaders can do the same with 32-bit atomics.
    struct FixedPoint
    {
        std::int64_t Value{0};

        [[nodiscard]] static std::int64_t Quantize(const float value, const float inverseScale) noexcept
        {
            const float scaled = std::nearbyint(value * inverseScale);
            if (!std::isfinite(scaled)) return 0;
            const float magnitude = std::fabs(scaled);
            const float high = std::floor(magnitude * 0x1p-32f);
            const float low = magnitude - high * 0x1p32f;
            const std::int64_t word = (static_cast<std::int64_t>(high) << 32) | static_cast<std::int64_t>(static_cast<std::uint32_t>(low));
            return scaled < 0.0f ? -word : word;
        }

        void Add(const float value, const float inverseScale) noexcept
        {
            Value = static_cast<std::int64_t>(static_cast<std::uint64_t>(Value)
                                              + static_cast<std::uint64_t>(Quantize(value, inverseScale)));
        }

        [[nodiscard]] double ToDouble(const double scale) const noexcept { return static_cast<double>(Value) * scale; }
    };
}

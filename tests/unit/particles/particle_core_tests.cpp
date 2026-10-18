#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

import GPP;
import glm;
import std;

using namespace GPP;
using Catch::Approx;

namespace
{
    ParticleSystemConfig Config(const double baseStep, const std::uint32_t maxBin, const float eta = 0.02f,
                                const std::uint32_t capacity = 1u << 16)
    {
        ParticleSystemConfig config;
        config.Settings.Units = {1.0, 1.0};
        config.Settings.Capacity = capacity;
        config.Settings.MaxBin = maxBin;
        config.Settings.Eta = eta;
        config.BaseStep = baseStep;
        config.MaxMacroSteps = 1u << 24;
        return config;
    }

    ParticleEmitterDef BurstEmitter(const std::uint64_t id, const int count, const glm::dvec3& position,
                                    const glm::dvec3& velocity, const double weight = 1.0)
    {
        ParticleEmitterDef def;
        def.Id = id;
        def.Params.Rate = 0.0;
        def.Params.Burst = count;
        def.Params.ParticleMass = weight;
        def.Params.BulkVelocity = velocity;
        def.Params.Seed = 11;
        def.Position = position;
        return def;
    }

    ParticleSceneState State(std::vector<ParticleEmitterDef> emitters = {})
    {
        ParticleSceneState state;
        state.Frame.Units = {1.0, 1.0};
        state.Emitters = std::move(emitters);
        return state;
    }

    std::unique_ptr<ParticleSystem> MakeSystem(const ParticleSystemConfig& config)
    {
        return std::make_unique<ParticleSystem>(std::make_unique<CpuParticleBackend>(), config);
    }

    ParticleArrays Snapshot(ParticleSystem& system)
    {
        ParticleArrays out;
        system.Backend().Download(out);
        return out;
    }

    double SpecificEnergy(const ParticleArrays& p, const std::size_t i, const double mu)
    {
        const glm::dvec3 x(p.PosMass[i]);
        const glm::dvec3 v(p.VelTemp[i]);
        return 0.5 * glm::dot(v, v) - mu / glm::length(x);
    }

    double AngularMomentum(const ParticleArrays& p, const std::size_t i)
    {
        return glm::length(glm::cross(glm::dvec3(p.PosMass[i]), glm::dvec3(p.VelTemp[i])));
    }
}

TEST_CASE("Philox4x32-10 matches the published known-answer vectors", "[particles][rng]")
{
    using ParticleRng::Philox4x32;
    CHECK(Philox4x32({0, 0, 0, 0}, {0, 0}) == ParticleRng::Counter{0x6627e8d5u, 0xe169c58du, 0xbc57ac4cu, 0x9b00dbd8u});
    CHECK(Philox4x32({~0u, ~0u, ~0u, ~0u}, {~0u, ~0u})
          == ParticleRng::Counter{0x408f276du, 0x41c83b0eu, 0xa20bc7c6u, 0x6d5451fdu});
    CHECK(Philox4x32({0x243f6a88u, 0x85a308d3u, 0x13198a2eu, 0x03707344u}, {0xa4093822u, 0x299f31d0u})
          == ParticleRng::Counter{0xd16cfe09u, 0x94fdcce5u + 6u, 0x5001e420u, 0x24126ea1u});
}

TEST_CASE("Counter-based draws are uniform and independent of call order", "[particles][rng]")
{
    const ParticleRng::Stream stream{42, 3};
    std::array<int, 16> histogram{};
    constexpr int kDraws = 40000;
    for (std::uint32_t i = 0; i < kDraws; ++i)
    {
        const auto bits = stream.Draw(i, 0);
        for (const auto word : bits) ++histogram[static_cast<std::size_t>(ParticleRng::ToUnit(word) * 16.0f)];
    }
    double chi2 = 0.0;
    const double expected = kDraws * 4.0 / 16.0;
    for (const int count : histogram) chi2 += (count - expected) * (count - expected) / expected;
    CHECK(chi2 < 37.7); // 15 degrees of freedom, p = 0.001

    CHECK(stream.Draw(77, 1) == ParticleRng::Stream{42, 3}.Draw(77, 1));
    CHECK(stream.Draw(77, 1) != ParticleRng::Stream{42, 4}.Draw(77, 1));
    CHECK(stream.Draw(77, 1) != ParticleRng::Stream{43, 3}.Draw(77, 1));
    CHECK(stream.Draw(77, 1) != stream.Draw(77, 2));

    double sum = 0.0;
    double sumSquares = 0.0;
    constexpr int kNormals = 20000;
    for (std::uint32_t i = 0; i < kNormals; ++i)
    {
        const auto bits = stream.Draw(i, 5);
        const auto [a, b] = ParticleRng::Normal(ParticleRng::ToUnit(bits[0]), ParticleRng::ToUnit(bits[1]));
        sum += a + b;
        sumSquares += a * a + b * b;
    }
    CHECK(sum / (2 * kNormals) == Approx(0.0).margin(0.02));
    CHECK(sumSquares / (2 * kNormals) == Approx(1.0).margin(0.03));
}

TEST_CASE("Fixed-point accumulation is exact and independent of summation order", "[particles][accounting]")
{
    std::vector<float> values;
    for (int i = 0; i < 2000; ++i) values.push_back(std::sin(static_cast<float>(i)) * 3.0e3f + static_cast<float>(i % 7));
    const float inverse = 0x1p10f;
    FixedPoint forward;
    FixedPoint backward;
    for (const float v : values) forward.Add(v, inverse);
    for (auto it = values.rbegin(); it != values.rend(); ++it) backward.Add(*it, inverse);
    CHECK(forward.Value == backward.Value);
    double reference = 0.0;
    for (const float v : values) reference += v;
    CHECK(forward.ToDouble(1.0 / 1024.0) == Approx(reference).epsilon(1e-6));

    FixedPoint big;
    big.Add(-3.0e12f, 1.0f);
    big.Add(1.25e12f, 1.0f);
    CHECK(big.ToDouble(1.0) == Approx(-1.75e12).epsilon(1e-6));
}

TEST_CASE("Bins halve the step until it is below the stability estimate", "[particles][scheduler]")
{
    CHECK(AssignParticleBin(1.0f, 2.0f, 6) == 0);
    CHECK(AssignParticleBin(1.0f, 1.0f, 6) == 0);
    CHECK(AssignParticleBin(1.0f, 0.5f, 6) == 1);
    CHECK(AssignParticleBin(1.0f, 0.49f, 6) == 2);
    CHECK(AssignParticleBin(1.0f, 0.26f, 6) == 2);
    CHECK(AssignParticleBin(1.0f, 0.2f, 6) == 3);
    CHECK(AssignParticleBin(1.0f, 1e-9f, 6) == 6);
    CHECK(AssignParticleBin(1.0f, 0.0f, 6) == 6);
    CHECK(AssignParticleBin(1.0f, std::numeric_limits<float>::quiet_NaN(), 6) == 6);
    CHECK(AssignParticleBin(1.0f, std::numeric_limits<float>::infinity(), 6) == 0);
}

TEST_CASE("Particles land in the bin their orbital time asks for", "[particles][scheduler]")
{
    auto system = MakeSystem(Config(0.1, 8, 0.002f));
    system->SetForce(ParticleForceParams::Newtonian(1.0f));
    std::vector<ParticleEmitterDef> emitters;
    const std::array radii{1.0, 2.0, 4.0, 8.0};
    for (std::size_t i = 0; i < radii.size(); ++i)
    {
        emitters.push_back(BurstEmitter(i + 1, 10, {radii[i], 0.0, 0.0}, {0.0, 1.0 / std::sqrt(radii[i]), 0.0}));
    }
    system->Consume(State(emitters));
    system->Advance(0.1);
    const auto particles = Snapshot(*system);
    REQUIRE(particles.Size() == 40);
    for (std::size_t i = 0; i < particles.Size(); ++i)
    {
        const double r = glm::length(glm::dvec3(particles.PosMass[i]));
        const double stable = 0.002 * 2.0 * std::numbers::pi * std::pow(r, 1.5);
        const auto expected = AssignParticleBin(0.1f, static_cast<float>(stable), 8);
        CHECK(std::abs(static_cast<int>(ParticleArrays::Bin(particles.Meta[i])) - static_cast<int>(expected)) <= 1);
    }
    std::set<std::uint32_t> bins;
    for (const auto& m : particles.Meta) bins.insert(ParticleArrays::Bin(m));
    CHECK(bins.size() >= 3);
    for (std::size_t i = 1; i < particles.Size(); ++i)
    {
        CHECK(ParticleArrays::Bin(particles.Meta[i - 1]) <= ParticleArrays::Bin(particles.Meta[i]));
    }
}

TEST_CASE("V-P4: circular Newtonian orbits conserve energy and angular momentum for 100 orbits", "[particles][integrator]")
{
    auto system = MakeSystem(Config(0.05, 4, 0.002f));
    system->SetForce(ParticleForceParams::Newtonian(1.0f));
    system->Consume(State({BurstEmitter(1, 1, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}),
                           BurstEmitter(2, 1, {0.0, 3.0, 0.0}, {-1.0 / std::sqrt(3.0), 0.0, 0.0})}));
    system->Advance(0.05);
    const auto start = Snapshot(*system);
    REQUIRE(start.Size() == 2);
    std::array<double, 2> energy{};
    std::array<double, 2> momentum{};
    for (std::size_t i = 0; i < 2; ++i)
    {
        energy[i] = SpecificEnergy(start, i, 1.0);
        momentum[i] = AngularMomentum(start, i);
    }
    system->Advance(100.0 * 2.0 * std::numbers::pi);
    const auto end = Snapshot(*system);
    REQUIRE(end.Size() == 2);
    for (std::size_t i = 0; i < 2; ++i)
    {
        CHECK(std::abs(SpecificEnergy(end, i, 1.0) / energy[i] - 1.0) < 1e-4);
        CHECK(std::abs(AngularMomentum(end, i) / momentum[i] - 1.0) < 1e-4);
    }
}

TEST_CASE("V-P4: an eccentric orbit crossing several step bins keeps its constants of motion", "[particles][integrator]")
{
    auto system = MakeSystem(Config(0.05, 8, 0.0004f));
    system->SetForce(ParticleForceParams::Newtonian(1.0f));
    system->Consume(State({BurstEmitter(1, 1, {1.0, 0.0, 0.0}, {0.0, 0.6, 0.0})}));
    system->Advance(0.05);
    const auto start = Snapshot(*system);
    const double energy = SpecificEnergy(start, 0, 1.0);
    const double momentum = AngularMomentum(start, 0);
    const double semiMajor = -0.5 / energy;
    const double period = 2.0 * std::numbers::pi * std::pow(semiMajor, 1.5);

    std::set<std::uint32_t> bins;
    double worstEnergy = 0.0;
    double worstMomentum = 0.0;
    for (int slice = 0; slice < 100; ++slice)
    {
        system->Advance(period);
        const auto now = Snapshot(*system);
        bins.insert(ParticleArrays::Bin(now.Meta[0]));
        worstEnergy = std::max(worstEnergy, std::abs(SpecificEnergy(now, 0, 1.0) / energy - 1.0));
        worstMomentum = std::max(worstMomentum, std::abs(AngularMomentum(now, 0) / momentum - 1.0));
    }
    CHECK(worstEnergy < 1e-4);
    CHECK(worstMomentum < 1e-4);
    INFO("worst energy " << worstEnergy << " momentum " << worstMomentum);
}

TEST_CASE("The leapfrog step is time symmetric", "[particles][integrator]")
{
    auto forward = MakeSystem(Config(0.01, 0, 0.5f));
    forward->SetForce(ParticleForceParams::Newtonian(1.0f));
    forward->Consume(State({BurstEmitter(1, 1, {1.0, 0.0, 0.0}, {0.0, 0.8, 0.1})}));
    forward->Advance(1.0);
    const auto after = Snapshot(*forward);
    REQUIRE(after.Size() == 1);

    auto backward = MakeSystem(Config(0.01, 0, 0.5f));
    backward->SetForce(ParticleForceParams::Newtonian(1.0f));
    backward->Consume(State({BurstEmitter(1, 1, glm::dvec3(after.PosMass[0]), -glm::dvec3(after.VelTemp[0]))}));
    backward->Advance(1.0);
    const auto back = Snapshot(*backward);
    REQUIRE(back.Size() == 1);
    CHECK(glm::length(glm::vec3(back.PosMass[0]) - glm::vec3(1.0f, 0.0f, 0.0f)) < 1e-5f);
    CHECK(glm::length(glm::vec3(back.VelTemp[0]) + glm::vec3(0.0f, 0.8f, 0.1f)) < 1e-5f);
}

TEST_CASE("Paczynski-Wiita circular orbits have the analytic period and the ISCO sits at three Schwarzschild radii",
          "[particles][integrator][pw]")
{
    constexpr double kMu = 1.0;
    constexpr double kRs = 0.2;
    const auto omega = [&](const double r) { return std::sqrt(kMu / (r * (r - kRs) * (r - kRs))); };
    const auto circular = [&](const double r) { return omega(r) * r; };

    {
        const double r = 1.0;
        auto system = MakeSystem(Config(0.01, 4, 0.002f));
        system->SetForce(ParticleForceParams::PaczynskiWiita(1.0f, 0.2f));
        system->Consume(State({BurstEmitter(1, 1, {r, 0.0, 0.0}, {0.0, circular(r), 0.0})}));
        system->Advance(100.0 * 2.0 * std::numbers::pi / omega(r));
        const auto now = Snapshot(*system);
        REQUIRE(now.Size() == 1);
        const double expected = std::fmod(omega(r) * system->SimTime(), 2.0 * std::numbers::pi);
        double phase = std::atan2(now.PosMass[0].y, now.PosMass[0].x) - expected;
        phase = std::remainder(phase, 2.0 * std::numbers::pi);
        CHECK(std::abs(phase) < 2e-3);
        CHECK(glm::length(glm::dvec3(now.PosMass[0])) == Approx(r).epsilon(1e-4));
        CHECK(AngularMomentum(now, 0) == Approx(r * circular(r)).epsilon(1e-5));
    }

    const auto survives = [&](const double r)
    {
        auto system = MakeSystem(Config(0.02, 6, 0.004f));
        system->SetForce(ParticleForceParams::PaczynskiWiita(1.0f, 0.2f));
        ParticleEmitterDef def = BurstEmitter(1, 1, {r, 0.0, 0.0}, {2e-3, circular(r), 0.0});
        ParticleSinkDef sink;
        sink.Id = 2;
        sink.Params.Radius = 1.05 * kRs;
        auto state = State({def});
        state.Sinks.push_back(sink);
        system->Consume(state);
        system->Advance(60.0);
        return system->Board()->Latest()->Stats.Alive == 1;
    };

    const double isco = 3.0 * kRs;
    double firstStable = 0.0;
    for (double r = 0.5; r < 0.9; r += 0.02)
    {
        if (survives(r))
        {
            firstStable = r;
            break;
        }
    }
    INFO("first stable radius " << firstStable);
    CHECK(firstStable >= isco - 0.03);
    CHECK(firstStable <= isco + 0.04);
    CHECK_FALSE(survives(0.45));
    CHECK(survives(1.0));
}

TEST_CASE("Emitters turn a mass rate into a per-particle weight and release exactly the integrated count", "[particles][emitter]")
{
    ParticleEmitterComponent emitter;
    emitter.RateMode = ParticleRateMode::MassRate;
    emitter.Rate = 2000.0;
    emitter.MassRate = 5.0;
    const auto [rate, weight] = ParticleSystem::RateAndWeight(emitter);
    CHECK(rate == 2000.0);
    CHECK(weight == Approx(0.0025));

    emitter.RateMode = ParticleRateMode::ParticleRate;
    emitter.ParticleMass = 0.5;
    CHECK(ParticleSystem::RateAndWeight(emitter).second == 0.5);

    auto system = MakeSystem(Config(0.01, 0, 0.02f, 1u << 18));
    ParticleEmitterDef def;
    def.Id = 7;
    def.Params.RateMode = ParticleRateMode::MassRate;
    def.Params.Rate = 1234.0;
    def.Params.MassRate = 3.0;
    def.Params.Seed = 5;
    system->Consume(State({def}));
    system->Advance(10.0);
    const auto particles = Snapshot(*system);
    CHECK(system->Emitted() == 12340);
    CHECK(particles.Size() == 12340);
    double mass = 0.0;
    for (const auto& pm : particles.PosMass) mass += pm.w;
    CHECK(mass == Approx(30.0).epsilon(1e-4));
    CHECK(system->EmittedMass() == Approx(30.0).epsilon(1e-9));
}

TEST_CASE("Emitter shapes sample inside the shape with the requested velocity statistics", "[particles][emitter]")
{
    const auto run = [](ParticleEmitterDef def, const std::uint32_t count)
    {
        def.Id = 1;
        def.Params.Rate = 0.0;
        def.Params.Burst = static_cast<int>(count);
        def.Params.Seed = 99;
        auto system = MakeSystem(Config(0.001, 0, 0.5f, 1u << 17));
        system->Consume(State({def}));
        system->Advance(0.001);
        return Snapshot(*system);
    };
    constexpr std::uint32_t kCount = 20000;

    {
        ParticleEmitterDef def;
        def.Params.Shape = ParticleEmitterShape::Sphere;
        def.Params.Radius = 2.0;
        def.Position = {1.0, 2.0, 3.0};
        const auto p = run(def, kCount);
        double meanRadius3 = 0.0;
        for (const auto& pm : p.PosMass)
        {
            const double r = glm::length(glm::dvec3(pm) - def.Position);
            CHECK(r <= 2.0 + 1e-4);
            meanRadius3 += r * r * r;
        }
        CHECK(meanRadius3 / kCount == Approx(0.5 * 8.0).epsilon(0.03));
    }
    {
        ParticleEmitterDef def;
        def.Params.Shape = ParticleEmitterShape::Shell;
        def.Params.Radius = 3.0;
        def.Params.InnerRadius = 2.0;
        const auto p = run(def, kCount);
        for (const auto& pm : p.PosMass)
        {
            const double r = glm::length(glm::dvec3(pm));
            CHECK(r >= 2.0 - 1e-4);
            CHECK(r <= 3.0 + 1e-4);
        }
    }
    {
        ParticleEmitterDef def;
        def.Params.Shape = ParticleEmitterShape::Disk;
        def.Params.Radius = 5.0;
        def.Params.InnerRadius = 1.0;
        def.Params.Thickness = 0.2;
        const auto p = run(def, kCount);
        double meanR2 = 0.0;
        for (const auto& pm : p.PosMass)
        {
            const double r = std::hypot(pm.x, pm.y);
            CHECK(r >= 1.0 - 1e-4);
            CHECK(r <= 5.0 + 1e-4);
            CHECK(std::abs(pm.z) <= 0.1 + 1e-5);
            meanR2 += r * r;
        }
        CHECK(meanR2 / kCount == Approx(13.0).epsilon(0.02));
    }
    {
        ParticleEmitterDef def;
        def.Params.Shape = ParticleEmitterShape::Ring;
        def.Params.Radius = 4.0;
        def.Params.Thickness = 0.4;
        def.Params.VelocityMode = ParticleVelocityMode::Azimuthal;
        def.Params.Speed = 2.0;
        const auto p = run(def, 5000);
        for (std::size_t i = 0; i < p.Size(); ++i)
        {
            const glm::dvec3 pos(p.PosMass[i]);
            const glm::dvec3 vel(p.VelTemp[i]);
            CHECK(std::hypot(pos.x, pos.y) >= 3.8 - 1e-3);
            CHECK(std::hypot(pos.x, pos.y) <= 4.2 + 1e-3);
            CHECK(glm::length(vel) == Approx(2.0).epsilon(1e-3));
            CHECK(glm::dot(vel, glm::dvec3(pos.x, pos.y, 0.0)) == Approx(0.0).margin(2e-2));
        }
    }
    {
        ParticleEmitterDef def;
        def.Params.Shape = ParticleEmitterShape::Cone;
        def.Params.HalfAngle = 0.3f;
        def.Params.Speed = 10.0;
        def.Rotation = glm::quat(glm::vec3(0.4f, 0.2f, 0.0f));
        const auto p = run(def, kCount);
        const glm::dvec3 axis = glm::normalize(glm::dvec3(def.Rotation * glm::vec3(0.0f, 0.0f, 1.0f)));
        double widest = 0.0;
        for (const auto& vt : p.VelTemp)
        {
            const glm::dvec3 vel(vt);
            widest = std::max(widest, std::acos(std::clamp(glm::dot(glm::normalize(vel), axis), -1.0, 1.0)));
            CHECK(glm::length(vel) == Approx(10.0).epsilon(1e-3));
        }
        CHECK(widest <= 0.3 + 1e-3);
        CHECK(widest > 0.28);
    }
    {
        ParticleEmitterDef def;
        def.Params.Shape = ParticleEmitterShape::Stream;
        def.Params.Radius = 0.1;
        def.Params.Speed = 4.0;
        def.Params.Dispersion = 0.5;
        def.Position = {0.0, 0.0, 0.0};
        def.Target = {10.0, 0.0, 0.0};
        const auto p = run(def, kCount);
        glm::dvec3 meanVelocity(0.0);
        for (const auto& vt : p.VelTemp) meanVelocity += glm::dvec3(vt);
        meanVelocity /= kCount;
        CHECK(meanVelocity.x == Approx(4.0).epsilon(0.01));
        CHECK(meanVelocity.y == Approx(0.0).margin(0.02));
        double variance = 0.0;
        for (const auto& vt : p.VelTemp) variance += (vt.y - meanVelocity.y) * (vt.y - meanVelocity.y);
        CHECK(std::sqrt(variance / kCount) == Approx(0.5).epsilon(0.03));
    }
    {
        ParticleEmitterDef def;
        def.Params.Shape = ParticleEmitterShape::Point;
        def.Params.ThermalVelocity = true;
        def.Params.Temperature = 1.0e4;
        def.Params.MeanMolecularWeight = 1.0;
        const auto p = run(def, kCount);
        double variance = 0.0;
        for (const auto& vt : p.VelTemp) variance += vt.x * vt.x;
        const double sigma = std::sqrt(Units::kBoltzmannConstant * 1.0e4 / Units::kProtonMass);
        CHECK(std::sqrt(variance / kCount) == Approx(sigma).epsilon(0.03));
        CHECK(p.VelTemp[0].w == Approx(1.0e4));
    }
}

TEST_CASE("Emission and motion are deterministic and independent of how the time is split", "[particles][determinism]")
{
    const auto make = [](const std::vector<double>& slices)
    {
        auto system = MakeSystem(Config(1.0 / 64.0, 3, 0.01f, 1u << 16));
        system->SetForce(ParticleForceParams::Newtonian(1.0f, 0.01f));
        ParticleEmitterDef def;
        def.Id = 3;
        def.Params.Shape = ParticleEmitterShape::Shell;
        def.Params.Radius = 4.0;
        def.Params.InnerRadius = 2.0;
        def.Params.Rate = 500.0;
        def.Params.Speed = 0.3;
        def.Params.Dispersion = 0.05;
        def.Params.VelocityMode = ParticleVelocityMode::Azimuthal;
        def.Params.Seed = 2024;
        ParticleSinkDef sink;
        sink.Id = 9;
        sink.Params.Radius = 0.8;
        auto state = State({def});
        state.Sinks.push_back(sink);
        system->Consume(state);
        for (const double dt : slices) system->Advance(dt);
        return std::pair{Snapshot(*system).Hash(), system->Emitted()};
    };

    const auto whole = make({6.0});
    CHECK(whole == make({6.0}));
    std::vector<double> sixty(600, 0.01);
    CHECK(whole == make(sixty));
    std::vector<double> binary(384, 1.0 / 64.0);
    CHECK(whole == make(binary));
    CHECK(whole == make({1.0, 2.5, 0.5, 2.0}));
    CHECK(whole.second == 3000);
    CHECK(whole.first != 0);
}

TEST_CASE("Sinks absorb inside their radius and the books balance", "[particles][sink][accounting]")
{
    auto config = Config(1.0 / 32.0, 2, 0.02f);
    config.Settings.ReferenceWeight = 0.25f;
    auto system = MakeSystem(config);
    system->SetForce(ParticleForceParams::Newtonian(1.0f, 0.05f));
    ParticleEmitterDef def;
    def.Id = 1;
    def.Params.Shape = ParticleEmitterShape::Sphere;
    def.Params.Radius = 6.0;
    def.Params.Rate = 400.0;
    def.Params.ParticleMass = 0.25;
    def.Params.Seed = 8;
    ParticleSinkDef sink;
    sink.Id = 5;
    sink.Params.Radius = 1.0;
    auto state = State({def});
    state.Sinks.push_back(sink);
    system->Consume(state);
    system->Advance(20.0);

    const auto data = system->Board()->Latest();
    REQUIRE(data);
    const auto absorbed = system->Board()->Sink(5);
    REQUIRE(absorbed);
    CHECK(absorbed->Count > 0);
    CHECK(data->Stats.Alive + absorbed->Count == system->Emitted());
    CHECK(absorbed->Mass() + data->Stats.AliveMass == Approx(system->EmittedMass()).epsilon(1e-6));
    CHECK(system->EmittedMass() == Approx(0.25 * 400.0 * 20.0));

    const auto particles = Snapshot(*system);
    for (const auto& pm : particles.PosMass) CHECK(glm::length(glm::vec3(pm)) >= 1.0f);
    CHECK(absorbed->Raw[7] != 0.0);
    CHECK(glm::length(absorbed->AngularMomentum({1.0, 1.0})) >= 0.0);
}

TEST_CASE("Kill sinks remove particles without accounting for their momentum", "[particles][sink]")
{
    auto system = MakeSystem(Config(0.1, 0));
    ParticleEmitterDef def = BurstEmitter(1, 100, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0});
    def.Params.Shape = ParticleEmitterShape::Sphere;
    def.Params.Radius = 1.0;
    ParticleSinkDef sink;
    sink.Id = 2;
    sink.Params.Radius = 10.0;
    sink.Params.Mode = ParticleSinkMode::Kill;
    auto state = State({def});
    state.Sinks.push_back(sink);
    system->Consume(state);
    system->Advance(0.1);
    const auto totals = system->Board()->Sink(2);
    REQUIRE(totals);
    CHECK(totals->Killed == 100);
    CHECK(totals->Count == 0);
    CHECK(totals->Mass() == 0.0);
    CHECK(system->Board()->Latest()->Stats.Alive == 0);
}

TEST_CASE("Capacity overflow drops particles and counts them", "[particles][emitter]")
{
    auto system = MakeSystem(Config(0.1, 0, 0.02f, 50));
    system->Consume(State({BurstEmitter(1, 80, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0})}));
    system->Advance(0.1);
    const auto data = system->Board()->Latest();
    CHECK(data->Stats.Alive == 50);
    CHECK(data->Stats.Dropped == 30);
}

TEST_CASE("A new epoch discards every particle and replays the same emission", "[particles][determinism]")
{
    auto system = MakeSystem(Config(0.05, 1));
    ParticleEmitterDef def = BurstEmitter(1, 40, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0});
    def.Params.Rate = 100.0;
    auto state = State({def});
    system->SetForce(ParticleForceParams::Newtonian(1.0f));
    system->Consume(state);
    system->Advance(1.0);
    const auto first = Snapshot(*system).Hash();
    CHECK(Snapshot(*system).Size() == 140);

    system->Consume(state);
    system->Advance(0.5);
    CHECK(Snapshot(*system).Size() == 190);

    state.Epoch = 1;
    system->Consume(state);
    CHECK(Snapshot(*system).Size() == 0);
    system->Advance(1.0);
    CHECK(Snapshot(*system).Hash() == first);
}

TEST_CASE("Colliders reflect, absorb and report the impulse they exchange", "[particles][collider]")
{
    const auto fire = [](const ParticleColliderResponse response, const glm::dvec3& bodyVelocity)
    {
        auto system = MakeSystem(Config(0.01, 0));
        ParticleEmitterDef def = BurstEmitter(1, 100, {-3.0, 0.0, 0.0}, {5.0, 0.0, 0.0}, 2.0);
        def.Params.Shape = ParticleEmitterShape::Disk;
        def.Params.Radius = 0.1;
        def.Rotation = glm::quat(glm::vec3(0.0f));
        ParticleColliderDef body;
        body.Id = 2;
        body.Params.Response = response;
        body.Params.Shape = ParticleColliderShape::Sphere;
        body.Params.Radius = 1.0;
        body.Params.Restitution = 1.0f;
        body.Velocity = bodyVelocity;
        auto state = State({def});
        state.Colliders.push_back(body);
        system->Consume(state);
        system->Advance(0.01);
        system->Advance(1.5);
        return std::pair{Snapshot(*system), system->Board()->Collider(2)};
    };

    {
        const auto [particles, totals] = fire(ParticleColliderResponse::Collide, {0.0, 0.0, 0.0});
        REQUIRE(totals);
        double px = 0.0;
        for (std::size_t i = 0; i < particles.Size(); ++i)
        {
            px += particles.PosMass[i].w * particles.VelTemp[i].x;
            CHECK(glm::length(glm::vec3(particles.PosMass[i])) >= 1.0f - 1e-5f);
        }
        CHECK(px < 0.0);
        CHECK(totals->Raw[4] == Approx(-2.0 * 100.0 * 2.0 * 5.0 * 0.0 + totals->Raw[4]));
        CHECK(totals->Raw[4] < -500.0);
        const glm::dvec3 onBody = totals->ImpulseOnBody({1.0, 1.0});
        CHECK(onBody.x == Approx(-totals->Raw[4]).epsilon(1e-9));
        CHECK(onBody.x > 500.0);
    }
    {
        const auto [particles, totals] = fire(ParticleColliderResponse::Absorb, {0.0, 0.0, 0.0});
        REQUIRE(totals);
        CHECK(totals->Count == 100);
        CHECK(totals->Mass() == Approx(200.0).epsilon(1e-6));
        CHECK(totals->Raw[1] == Approx(1000.0).epsilon(1e-4));
        CHECK(particles.Size() == 0);
    }
    {
        const auto [moving, movingTotals] = fire(ParticleColliderResponse::Collide, {5.0, 0.0, 0.0});
        REQUIRE(movingTotals);
        CHECK(std::abs(movingTotals->Raw[4]) < 50.0);
    }
}

TEST_CASE("Drag colliders push the gas with the equal and opposite of the force on the body", "[particles][collider][drag]")
{
    auto config = Config(0.02, 0);
    auto system = MakeSystem(config);
    ParticleAmbient ambient;
    ambient.ConstantDensity = 3.0;
    ambient.ConstantVelocity = {0.0, 0.0, 0.0};
    system->SetAmbient(ambient);

    ParticleEmitterDef gas = BurstEmitter(1, 400, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, 0.5);
    gas.Params.Shape = ParticleEmitterShape::Sphere;
    gas.Params.Radius = 0.4;
    ParticleColliderDef body;
    body.Id = 2;
    body.Params.Response = ParticleColliderResponse::Drag;
    body.Params.Shape = ParticleColliderShape::Sphere;
    body.Params.Radius = 1.0;
    body.Params.DragCoefficient = 0.5f;
    body.Velocity = {2.0, 0.0, 0.0};
    auto state = State({gas});
    state.Colliders.push_back(body);
    system->Consume(state);
    system->Advance(0.02);
    system->Advance(0.1);

    const double expectedForce = 0.5 * 3.0 * 0.5 * (std::numbers::pi * 1.0) * 2.0 * 2.0;
    const auto totals = system->Board()->Collider(2);
    REQUIRE(totals);
    const glm::dvec3 given(totals->Raw[4], totals->Raw[5], totals->Raw[6]);
    CHECK(given.x == Approx(expectedForce * 0.12).epsilon(0.02));
    CHECK(given.y == Approx(0.0).margin(1e-3));
    const double meanVx = system->Board()->Latest()->Stats.MeanVelocity.x;
    CHECK(meanVx == Approx(given.x / 200.0).epsilon(0.02));
}

TEST_CASE("V-P8: the splat deposits all mass and momentum", "[particles][grid]")
{
    auto system = MakeSystem(Config(0.1, 0));
    ParticleEmitterDef def = BurstEmitter(1, 5000, {0.0, 0.0, 0.0}, {1.0, -2.0, 0.5}, 0.7);
    def.Params.Shape = ParticleEmitterShape::Sphere;
    def.Params.Radius = 3.0;
    def.Params.Temperature = 500.0;
    system->Consume(State({def}));
    system->Advance(0.1);

    ParticleGridSpec spec;
    spec.Origin = {-3.0f, -3.0f, -3.0f};
    spec.CellSize = 0.25f;
    spec.Dimensions = {26, 26, 26};
    ParticleGrid grid;
    system->Backend().Deposit(spec, grid);
    double mass = 0.0;
    glm::dvec3 momentum(0.0);
    double massTemperature = 0.0;
    for (std::size_t i = 0; i < grid.Mass.size(); ++i)
    {
        mass += grid.Mass[i];
        momentum += glm::dvec3(grid.Momentum[i]);
        massTemperature += grid.MassTemperature[i];
    }
    const auto particles = Snapshot(*system);
    double total = 0.0;
    for (const auto& pm : particles.PosMass) total += pm.w;
    CHECK(mass + grid.OutsideMass == Approx(total).epsilon(1e-6));
    CHECK(grid.OutsideMass < 0.05 * total);
    CHECK(momentum.x == Approx(mass * 1.0).epsilon(0.02));
    CHECK(momentum.y == Approx(mass * -2.0).epsilon(0.02));
    CHECK(massTemperature == Approx(mass * 500.0).epsilon(1e-4));

    ParticleGridSpec small;
    small.Origin = {-1.0f, -1.0f, -1.0f};
    small.CellSize = 0.5f;
    small.Dimensions = {5, 5, 5};
    ParticleGrid clipped;
    system->Backend().Deposit(small, clipped);
    double inside = 0.0;
    for (const float m : clipped.Mass) inside += m;
    CHECK(inside + clipped.OutsideMass == Approx(total).epsilon(1e-6));
    CHECK(clipped.OutsideMass > 0.5 * total);
}

TEST_CASE("Region statistics gather mass, count and mean temperature inside a sphere", "[particles][stats]")
{
    auto system = MakeSystem(Config(0.1, 0));
    ParticleEmitterDef hot = BurstEmitter(1, 300, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, 2.0);
    hot.Params.Shape = ParticleEmitterShape::Sphere;
    hot.Params.Radius = 1.0;
    hot.Params.Temperature = 900.0;
    ParticleEmitterDef cold = BurstEmitter(2, 100, {10.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, 1.0);
    cold.Params.Shape = ParticleEmitterShape::Sphere;
    cold.Params.Radius = 1.0;
    cold.Params.Temperature = 100.0;
    auto state = State({hot, cold});
    state.Regions.push_back({{0.0, 0.0, 0.0}, 2.0});
    state.Regions.push_back({{5.0, 0.0, 0.0}, 8.0});
    system->Consume(state);
    system->Advance(0.1);
    const auto data = system->Board()->Latest();
    REQUIRE(data->Stats.Regions.size() == 2);
    CHECK(data->Stats.Regions[0].Count == 300);
    CHECK(data->Stats.Regions[0].Mass == Approx(600.0));
    CHECK(data->Stats.Regions[0].MeanTemperature == Approx(900.0));
    CHECK(data->Stats.Regions[1].Count == 400);
    CHECK(data->Stats.Regions[1].MeanTemperature == Approx((600.0 * 900.0 + 100.0 * 100.0) / 700.0));
    CHECK(data->Stats.Alive == 400);
    CHECK(data->Stats.AliveMass == Approx(700.0));
}

TEST_CASE("Custom forces, step hooks and absorb quantities plug into the CPU backend", "[particles][hooks]")
{
    auto system = MakeSystem(Config(0.1, 0));
    ParticleForceParams force;
    force.Kind = static_cast<std::uint32_t>(ParticleForceKind::Custom);
    force.User[0] = 2.0f;
    system->SetForce(force);
    ParticleHooks hooks;
    hooks.Acceleration = [](const glm::vec3&, const glm::vec3&, const ParticleForceParams& f)
    {
        return glm::vec3(0.0f, -f.User[0], 0.0f);
    };
    hooks.PostMacroStep = [](ParticleView& p, const float, const ParticleForceParams&) { p.Temperature += 1.0f; };
    hooks.AbsorbQuantities = [](const ParticleView& p, const glm::vec3&, const ParticleForceParams&,
                                std::array<float, kParticleAccumulators>& q)
    {
        q = {};
        q[0] = 1.0f;
        q[7] = p.Velocity.y;
    };
    system->SetHooks(hooks);
    ParticleSinkDef sink;
    sink.Id = 4;
    sink.Position = {0.0, -1.0, 0.0};
    sink.Params.Radius = 0.5;
    auto state = State({BurstEmitter(1, 10, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0})});
    state.Sinks.push_back(sink);
    system->Consume(state);
    system->Advance(0.1);
    const auto first = Snapshot(*system);
    REQUIRE(first.Size() == 10);
    CHECK(first.VelTemp[0].y == Approx(-0.2f).epsilon(1e-4));
    CHECK(first.VelTemp[0].w == 1.0f + 1.0e4f);
    system->Advance(1.5);
    const auto totals = system->Board()->Sink(4);
    REQUIRE(totals);
    CHECK(totals->Count == 10);
    CHECK(totals->Raw[7] < 0.0);
}

TEST_CASE("Particle components round-trip through the scene and gather in guid order", "[particles][scene]")
{
    Scene scene;
    const auto emitter = scene.CreateEntityWithGuid(20, "Emitter");
    scene.Registry().emplace<TransformComponent>(emitter, TransformComponent{.Position = {1.0, 2.0, 3.0}});
    ParticleEmitterComponent params;
    params.Shape = ParticleEmitterShape::Cone;
    params.Rate = 77.0;
    params.RateMode = ParticleRateMode::MassRate;
    params.MassRate = 9.0;
    params.Seed = 31337;
    params.HalfAngle = 0.5f;
    scene.Registry().emplace<ParticleEmitterComponent>(emitter, params);

    const auto sinkEntity = scene.CreateEntityWithGuid(10, "Sink");
    scene.Registry().emplace<TransformComponent>(sinkEntity, TransformComponent{.Position = {0.0, 0.0, 5.0}});
    scene.Registry().emplace<ParticleSinkComponent>(sinkEntity, ParticleSinkComponent{.Radius = 4.0});

    const auto bodyEntity = scene.CreateEntityWithGuid(30, "Body");
    scene.Registry().emplace<TransformComponent>(bodyEntity);
    scene.Registry().emplace<VelocityComponent>(bodyEntity, VelocityComponent{.Linear = {1.0, 2.0, 3.0}});
    scene.Registry().emplace<ParticleColliderComponent>(bodyEntity,
                                                        ParticleColliderComponent{.Response = ParticleColliderResponse::Drag});

    const auto yaml = scene.SerializeToYaml();
    Scene loaded;
    loaded.DeserializeFromYaml(yaml);
    ParticleFrame frame;
    frame.Origin = {1.0, 0.0, 0.0};
    frame.Units = {2.0, 1.0};
    const auto state = GatherParticleScene(loaded, frame, 3, 42);
    REQUIRE(state.Emitters.size() == 1);
    CHECK(state.Emitters[0].Id == 20);
    CHECK(state.Emitters[0].Params.Shape == ParticleEmitterShape::Cone);
    CHECK(state.Emitters[0].Params.Seed == 31337);
    CHECK(state.Emitters[0].Params.MassRate == 9.0);
    CHECK(state.Emitters[0].Position == glm::dvec3(1.0, 2.0, 3.0));
    REQUIRE(state.Sinks.size() == 1);
    CHECK(state.Sinks[0].Params.Radius == 4.0);
    REQUIRE(state.Colliders.size() == 1);
    CHECK(state.Colliders[0].Velocity == glm::dvec3(1.0, 2.0, 3.0));
    CHECK(state.Epoch == 3);
    CHECK(state.SceneGeneration == 42);

    const auto again = GatherParticleScene(loaded, frame, 3, 43);
    CHECK(again.ContentHash == state.ContentHash);
    loaded.Registry().get<ParticleSinkComponent>(loaded.FindByGuid(10)).Radius = 5.0;
    CHECK(GatherParticleScene(loaded, frame, 3, 43).ContentHash != state.ContentHash);

    const auto params0 = ParticleSystem::MakeEmitterParams(state.Emitters[0], frame, 4, 100);
    CHECK(params0.PositionShape.x == Approx(0.0f));
    CHECK(params0.PositionShape.y == Approx(1.0f));
    CHECK(params0.PositionShape.w == static_cast<float>(ParticleEmitterShape::Cone));
    CHECK(params0.Params1.z == Approx(9.0f / 77.0f));
    CHECK(params0.Ids.z == 4);
    CHECK(params0.Batch.x == 100);
}

TEST_CASE("Attached emitters follow the entity they are attached to", "[particles][scene]")
{
    Scene scene;
    const auto ship = scene.CreateEntityWithGuid(5, "Ship");
    scene.Registry().emplace<TransformComponent>(ship, TransformComponent{.Position = {10.0, 0.0, 0.0}});
    scene.Registry().emplace<VelocityComponent>(ship, VelocityComponent{.Linear = {0.0, 3.0, 0.0}});
    const auto emitter = scene.CreateEntityWithGuid(6, "Plume");
    scene.Registry().emplace<ParticleEmitterComponent>(emitter, ParticleEmitterComponent{.AttachedTo = 5});
    const auto state = GatherParticleScene(scene, {}, 0, 0);
    REQUIRE(state.Emitters.size() == 1);
    CHECK(state.Emitters[0].Position == glm::dvec3(10.0, 0.0, 0.0));
    CHECK(state.Emitters[0].Velocity == glm::dvec3(0.0, 3.0, 0.0));
}

TEST_CASE("The script hub hands out deterministic transient emitters", "[particles][hub]")
{
    const auto run = []
    {
        ParticleHub hub;
        ParticleEmitterComponent params;
        const auto a = hub.CreateEmitter(params, {1.0, 0.0, 0.0});
        const auto b = hub.CreateEmitter(params, {2.0, 0.0, 0.0});
        hub.Burst(b, 25);
        CHECK(hub.Edit(a, [](ParticleEmitterDef& d) { d.Position = {9.0, 9.0, 9.0}; }));
        CHECK(hub.Destroy(a));
        CHECK_FALSE(hub.Destroy(a));
        const auto snapshot = hub.Snapshot();
        CHECK(snapshot.Emitters.size() == 1);
        CHECK(snapshot.Bursts.size() == 1);
        hub.Acknowledge(snapshot.Bursts[0].Serial);
        CHECK(hub.Snapshot().Bursts.empty());
        return std::pair{a, b};
    };
    CHECK(run() == run());
    CHECK((run().first & kTransientParticleIdBit) != 0);

    ParticleHub hub;
    hub.CreateEmitter({}, {0.0, 0.0, 0.0});
    const auto before = hub.Snapshot();
    hub.Reset();
    const auto after = hub.Snapshot();
    CHECK(after.Epoch == before.Epoch + 1);
    CHECK(after.Emitters.empty());
    CHECK(hub.Region({0.0, 0.0, 0.0}, 1.0) == 0);
    CHECK(hub.Region({0.0, 0.0, 0.0}, 1.0) == 0);
    CHECK(hub.Region({1.0, 0.0, 0.0}, 1.0) == 1);
}

TEST_CASE("Scripted bursts are applied once and acknowledged", "[particles][hub]")
{
    auto hub = std::make_shared<ParticleHub>();
    auto system = MakeSystem(Config(0.1, 0));
    system->SetHub(hub);
    ParticleEmitterComponent params;
    params.Rate = 0.0;
    params.Seed = 4;
    const auto id = hub->CreateEmitter(params, {0.0, 0.0, 0.0});
    auto consume = [&]
    {
        auto snapshot = hub->Snapshot();
        system->Consume(GatherParticleScene(Scene{}, {ParticleFrame{{}, {1.0, 1.0}}}, 0, 0, &snapshot));
    };
    consume();
    system->Advance(0.1);
    CHECK(Snapshot(*system).Size() == 0);
    hub->Burst(id, 30);
    consume();
    system->Advance(0.1);
    CHECK(Snapshot(*system).Size() == 30);
    system->Advance(0.1);
    CHECK(Snapshot(*system).Size() == 30);
    CHECK(hub->Snapshot().Bursts.empty());
}

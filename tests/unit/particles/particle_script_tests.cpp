#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

import GPP;
import glm;
import std;

using namespace GPP;
using Catch::Approx;

namespace
{
    struct Rig
    {
        std::shared_ptr<ParticleHub> Hub = std::make_shared<ParticleHub>();
        LuauVm Vm;
        int Handle{0};

        explicit Rig(const std::string& body)
        {
            RegisterMathBindings(Vm);
            RegisterParticleBindings(Vm, Hub);
            Vm.Seal();
            auto loaded = Vm.Load("local M = {}\nfunction M.run()\n" + body + "\nend\nreturn M", "=test");
            REQUIRE(loaded.has_value());
            Handle = *loaded;
        }

        void Run()
        {
            const auto result = Vm.Call(Handle, "run");
            if (!result) UNSCOPED_INFO(result.error().Message);
            REQUIRE(result.has_value());
        }
    };
}

TEST_CASE("particles.emitter builds a transient emitter from a table", "[particles][luau]")
{
    Rig rig(R"(
        local e = particles.emitter{
            shape = "stream", from = vector.create(1, 2, 3), toward = { x = 10, y = 0, z = 0 },
            mass_rate = 5.0, rate = 100, speed = 5000, dispersion = 1000, seed = 7, temperature = 3000,
            velocity = "radial", direction = vector.create(0, 1, 0), thermal = true, burst = 12,
        }
        assert(particles.set_origin(e, vector.create(4, 5, 6)))
        assert(particles.set_rate(e, 8.0))
        assert(particles.pause(e))
        assert(particles.resume(e))
        particles.burst(e, 30)
        local second = particles.emitter{ shape = "sphere", radius = 2 }
        assert(particles.destroy(second))
        assert(particles.count() == 0)
        assert(particles.region_stats(vector.create(0, 0, 0), 5) == nil)
    )");
    rig.Run();
    const auto snapshot = rig.Hub->Snapshot();
    REQUIRE(snapshot.Emitters.size() == 1);
    const auto& e = snapshot.Emitters[0];
    CHECK((e.Id & kTransientParticleIdBit) != 0);
    CHECK(e.Params.Shape == ParticleEmitterShape::Stream);
    CHECK(e.Params.RateMode == ParticleRateMode::MassRate);
    CHECK(e.Params.MassRate == Approx(8.0));
    CHECK(e.Params.Rate == Approx(100.0));
    CHECK(e.Params.Seed == 7);
    CHECK(e.Params.Burst == 12);
    CHECK(e.Params.Active);
    CHECK(e.Params.ThermalVelocity);
    CHECK(e.Params.VelocityMode == ParticleVelocityMode::Radial);
    CHECK(e.Position == glm::dvec3(4.0, 5.0, 6.0));
    CHECK(e.Target == glm::dvec3(10.0, 0.0, 0.0));
    CHECK(glm::length(e.Rotation * glm::vec3(0.0f, 0.0f, 1.0f) - glm::vec3(0.0f, 1.0f, 0.0f)) < 1e-5f);
    REQUIRE(snapshot.Bursts.size() == 1);
    CHECK(snapshot.Bursts[0].Count == 30);
    REQUIRE(snapshot.Regions.size() == 1);
    CHECK(snapshot.Regions[0].Radius == 5.0);
}

TEST_CASE("Scripted emitters are deterministic across runs and without a seed get one from their id", "[particles][luau][determinism]")
{
    const auto run = []
    {
        Rig rig("particles.emitter{ shape = 'disk', radius = 3 } particles.emitter{ shape = 'cone' }");
        rig.Run();
        return rig.Hub->Snapshot().Emitters;
    };
    const auto a = run();
    const auto b = run();
    REQUIRE(a.size() == 2);
    CHECK(a[0].Id == b[0].Id);
    CHECK(a[0].Params.Seed == b[0].Params.Seed);
    CHECK(a[0].Params.Seed != a[1].Params.Seed);
}

TEST_CASE("Scripted emitters feed the particle system and region stats come back from the board", "[particles][luau]")
{
    Rig rig("particles.emitter{ shape = 'sphere', radius = 1, rate = 0, burst = 200, particle_mass = 2, temperature = 400 }");
    rig.Run();

    ParticleSystemConfig config;
    config.Settings.Units = {1.0, 1.0};
    config.BaseStep = 0.1;
    ParticleSystem system(std::make_unique<CpuParticleBackend>(), config, rig.Hub->Board());
    system.SetHub(rig.Hub);

    const auto consume = [&]
    {
        auto transient = rig.Hub->Snapshot();
        ParticleFrame frame;
        frame.Units = {1.0, 1.0};
        system.Consume(GatherParticleScene(Scene{}, frame, 0, 0, &transient));
    };
    Rig reader("local r = particles.region_stats(vector.create(0, 0, 0), 3) assert(r == nil or r.count >= 0)");
    consume();
    system.Advance(0.1);
    CHECK(rig.Hub->Board()->Count() == 200);

    const auto region = rig.Hub->Region({0.0, 0.0, 0.0}, 3.0);
    consume();
    system.Advance(0.1);
    const auto stats = rig.Hub->RegionStats(region);
    REQUIRE(stats);
    CHECK(stats->Count == 200);
    CHECK(stats->Mass == Approx(400.0));
    CHECK(stats->MeanTemperature == Approx(400.0));

    rig.Hub->Reset();
    consume();
    CHECK(rig.Hub->Board()->Count() == 0);
}

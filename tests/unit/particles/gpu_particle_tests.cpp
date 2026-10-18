#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

import GPP;
import glm;
import std;

using namespace GPP;
using Catch::Approx;

namespace
{
    struct GpuRig
    {
        std::shared_ptr<Logger> Log = std::make_shared<Logger>();
        std::shared_ptr<VulkanDevice> Device = CreateHeadlessDevice(Log);
        std::shared_ptr<FileSystem> Files = std::make_shared<FileSystem>();

        static std::unique_ptr<GpuRig>& Slot()
        {
            static std::unique_ptr<GpuRig> rig;
            return rig;
        }

        static GpuRig& Get()
        {
            auto& slot = Slot();
            if (!slot) slot = std::make_unique<GpuRig>();
            return *slot;
        }
    };

    // The device must go before static destruction: the validation layer aborts in vkDeviceWaitIdle once the loader is torn down
    struct GpuRigTeardown : Catch::EventListenerBase
    {
        using Catch::EventListenerBase::EventListenerBase;
        void testRunEnded(const Catch::TestRunStats&) override
        {
            if (auto& slot = GpuRig::Slot(); slot && slot->Device) slot->Device->WaitIdle();
            GpuRig::Slot().reset();
        }
    };
    CATCH_REGISTER_LISTENER(GpuRigTeardown)

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

    std::unique_ptr<ParticleSystem> MakeCpu(const ParticleSystemConfig& config)
    {
        return std::make_unique<ParticleSystem>(std::make_unique<CpuParticleBackend>(), config);
    }

    std::unique_ptr<ParticleSystem> MakeGpu(const ParticleSystemConfig& config,
                                            const ParticleForceKind force = ParticleForceKind::Newtonian)
    {
        auto& rig = GpuRig::Get();
        GpuParticleOptions options;
        options.Device = rig.Device;
        options.FileSystem = rig.Files;
        options.Log = rig.Log;
        options.ShaderDirectory = std::filesystem::path(GPP_SOURCE_DIR) / "shaders" / "particles";
        options.Force = force;
        return std::make_unique<ParticleSystem>(std::make_unique<GpuParticleBackend>(std::move(options)), config);
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

    ParticleArrays Snapshot(ParticleSystem& system)
    {
        ParticleArrays out;
        system.Backend().Download(out);
        return out;
    }

    // Particles in emission order, so two backends can be compared element by element
    std::vector<std::size_t> BySerial(const ParticleArrays& p)
    {
        std::vector<std::size_t> order(p.Size());
        std::iota(order.begin(), order.end(), 0);
        std::ranges::sort(order, [&](const std::size_t a, const std::size_t b)
        {
            return std::tuple(p.Meta[a].x, p.Meta[a].w) < std::tuple(p.Meta[b].x, p.Meta[b].w);
        });
        return order;
    }

    double MaxPositionDifference(const ParticleArrays& a, const ParticleArrays& b)
    {
        REQUIRE(a.Size() == b.Size());
        const auto orderA = BySerial(a);
        const auto orderB = BySerial(b);
        double worst = 0.0;
        for (std::size_t i = 0; i < a.Size(); ++i)
        {
            REQUIRE(a.Meta[orderA[i]].w == b.Meta[orderB[i]].w);
            worst = std::max(worst, static_cast<double>(glm::length(glm::vec3(a.PosMass[orderA[i]]) - glm::vec3(b.PosMass[orderB[i]]))));
        }
        return worst;
    }
}

#define REQUIRE_GPU() \
    do { if (!GpuRig::Get().Device) SKIP("no Vulkan device"); } while (false)

TEST_CASE("GPU emission matches the CPU reference particle for particle", "[particles][gpu]")
{
    REQUIRE_GPU();
    ParticleEmitterDef def;
    def.Id = 1;
    def.Params.Shape = ParticleEmitterShape::Shell;
    def.Params.Radius = 3.0;
    def.Params.InnerRadius = 1.0;
    def.Params.Rate = 0.0;
    def.Params.Burst = 3000;
    def.Params.Speed = 0.5;
    def.Params.Dispersion = 0.1;
    def.Params.VelocityMode = ParticleVelocityMode::Radial;
    def.Params.Temperature = 777.0;
    def.Params.ParticleMass = 2.5;
    def.Params.Seed = 0x1234567890ull;
    def.Position = {0.5, 0.25, -0.3};
    ParticleEmitterDef disk = def;
    disk.Id = 2;
    disk.Params.Shape = ParticleEmitterShape::Disk;
    disk.Params.Thickness = 0.3;
    disk.Params.VelocityMode = ParticleVelocityMode::Azimuthal;
    ParticleEmitterDef cone = def;
    cone.Id = 3;
    cone.Params.Shape = ParticleEmitterShape::Cone;
    cone.Params.VelocityMode = ParticleVelocityMode::Axial;
    cone.Rotation = glm::quat(glm::vec3(0.3f, 1.0f, 0.0f));
    ParticleEmitterDef stream = def;
    stream.Id = 4;
    stream.Params.Shape = ParticleEmitterShape::Stream;
    stream.Params.Radius = 0.2;
    stream.Target = {5.0, 1.0, -2.0};

    auto cpu = MakeCpu(Config(0.01, 0));
    auto gpu = MakeGpu(Config(0.01, 0));
    for (auto* system : {cpu.get(), gpu.get()})
    {
        system->Consume(State({def, disk, cone, stream}));
        system->Advance(0.01);
    }
    const auto a = Snapshot(*cpu);
    const auto b = Snapshot(*gpu);
    REQUIRE(a.Size() == 12000);
    CHECK(MaxPositionDifference(a, b) < 2e-4);
    CHECK(gpu->Backend().Name() == "gpu");

    const auto orderA = BySerial(a);
    const auto orderB = BySerial(b);
    for (std::size_t i = 0; i < a.Size(); i += 97)
    {
        CHECK(glm::length(glm::vec3(a.VelTemp[orderA[i]]) - glm::vec3(b.VelTemp[orderB[i]])) < 2e-4f);
        CHECK(a.PosMass[orderA[i]].w == b.PosMass[orderB[i]].w);
        CHECK(a.Meta[orderA[i]].x == b.Meta[orderB[i]].x);
    }
}

TEST_CASE("GPU block steps follow the CPU through eccentric orbits", "[particles][gpu]")
{
    REQUIRE_GPU();
    const auto run = [](ParticleSystem& system)
    {
        system.SetForce(ParticleForceParams::Newtonian(1.0f, 0.01f));
        std::vector<ParticleEmitterDef> emitters;
        for (int i = 0; i < 8; ++i)
        {
            emitters.push_back(BurstEmitter(i + 1, 25, {1.0 + 0.2 * i, 0.0, 0.1 * i}, {0.0, 0.5 + 0.08 * i, 0.0}));
            emitters.back().Params.Shape = ParticleEmitterShape::Sphere;
            emitters.back().Params.Radius = 0.05;
            emitters.back().Params.Dispersion = 0.02;
        }
        system.Consume(State(emitters));
        system.Advance(0.05);
        system.Advance(10.0);
    };
    auto cpu = MakeCpu(Config(0.05, 6, 0.004f));
    auto gpu = MakeGpu(Config(0.05, 6, 0.004f));
    run(*cpu);
    run(*gpu);
    const auto a = Snapshot(*cpu);
    const auto b = Snapshot(*gpu);
    REQUIRE(a.Size() == b.Size());
    CHECK(a.Size() == 200);
    std::set<std::uint32_t> bins;
    for (const auto& m : b.Meta) bins.insert(ParticleArrays::Bin(m));
    CHECK(bins.size() >= 2);
    CHECK(MaxPositionDifference(a, b) < 5e-3);
}

TEST_CASE("GPU sinks and colliders keep the same books as the CPU", "[particles][gpu][accounting]")
{
    REQUIRE_GPU();
    const auto run = [](ParticleSystem& system)
    {
        system.SetForce(ParticleForceParams::Newtonian(1.0f, 0.05f));
        ParticleAmbient ambient;
        ambient.ConstantDensity = 2.0;
        system.SetAmbient(ambient);
        ParticleEmitterDef def;
        def.Id = 1;
        def.Params.Shape = ParticleEmitterShape::Sphere;
        def.Params.Radius = 6.0;
        def.Params.Rate = 300.0;
        def.Params.ParticleMass = 0.25;
        def.Params.Seed = 8;
        def.Params.Temperature = 5000.0;
        ParticleSinkDef sink;
        sink.Id = 5;
        sink.Params.Radius = 1.0;
        ParticleColliderDef body;
        body.Id = 6;
        body.Position = {3.0, 0.0, 0.0};
        body.Velocity = {0.0, 0.3, 0.0};
        body.Params.Response = ParticleColliderResponse::Collide;
        body.Params.Radius = 0.8;
        ParticleColliderDef eater;
        eater.Id = 7;
        eater.Position = {-3.0, 0.0, 0.0};
        eater.Params.Response = ParticleColliderResponse::Absorb;
        eater.Params.Shape = ParticleColliderShape::Box;
        eater.Params.HalfExtents = {0.5, 1.0, 1.5};
        eater.Rotation = glm::quat(glm::vec3(0.0f, 0.4f, 0.2f));
        ParticleColliderDef drag;
        drag.Id = 8;
        drag.Position = {0.0, 3.0, 0.0};
        drag.Velocity = {0.5, 0.0, 0.0};
        drag.Params.Response = ParticleColliderResponse::Drag;
        drag.Params.Radius = 0.9;
        auto state = State({def});
        state.Sinks.push_back(sink);
        state.Colliders = {body, eater, drag};
        state.Regions.push_back({{0.0, 0.0, 0.0}, 3.0});
        system.Consume(state);
        system.Advance(1.0 / 32.0);
        system.Advance(8.0);
    };
    auto config = Config(1.0 / 32.0, 2, 0.02f);
    config.Settings.ReferenceWeight = 0.25f;
    auto cpu = MakeCpu(config);
    auto gpu = MakeGpu(config);
    run(*cpu);
    run(*gpu);

    const auto a = cpu->Board()->Latest();
    const auto b = gpu->Board()->Latest();
    REQUIRE(a);
    REQUIRE(b);
    CHECK(cpu->Emitted() == gpu->Emitted());
    CHECK(b->Stats.Alive == a->Stats.Alive);
    CHECK(b->Stats.AliveMass == Approx(a->Stats.AliveMass).epsilon(1e-6));
    CHECK(b->Stats.MeanTemperature == Approx(5000.0).epsilon(1e-5));
    CHECK(b->Stats.Alive + gpu->Board()->Sink(5)->Count + gpu->Board()->Collider(7)->Count
          == gpu->Emitted());
    CHECK(gpu->Board()->Sink(5)->Count > 0);
    CHECK(gpu->Board()->Collider(7)->Count > 0);

    for (const auto id : {std::uint64_t{5}})
    {
        const auto sa = *cpu->Board()->Sink(id);
        const auto sb = *gpu->Board()->Sink(id);
        CHECK(sb.Count == Approx(static_cast<double>(sa.Count)).margin(3.0));
        CHECK(sb.Mass() == Approx(sa.Mass()).epsilon(0.02));
        CHECK(sb.Raw[7] == Approx(sa.Raw[7]).epsilon(0.05).margin(0.5));
    }
    const auto ca = *cpu->Board()->Collider(6);
    const auto cb = *gpu->Board()->Collider(6);
    CHECK(cb.Raw[4] == Approx(ca.Raw[4]).epsilon(0.05).margin(0.5));
    CHECK(cb.Raw[5] == Approx(ca.Raw[5]).epsilon(0.05).margin(0.5));
    const auto da = *cpu->Board()->Collider(8);
    const auto db = *gpu->Board()->Collider(8);
    CHECK(db.Raw[4] == Approx(da.Raw[4]).epsilon(0.05).margin(0.05));
    CHECK(b->Stats.Regions.size() == 1);
    CHECK(b->Stats.Regions[0].Count == Approx(static_cast<double>(a->Stats.Regions[0].Count)).margin(3.0));
    CHECK(b->Stats.Regions[0].Mass == Approx(a->Stats.Regions[0].Mass).epsilon(0.02));
}

TEST_CASE("GPU runs are bit-identical and independent of frame splitting", "[particles][gpu][determinism]")
{
    REQUIRE_GPU();
    const auto make = [](const std::vector<double>& slices)
    {
        auto system = MakeGpu(Config(1.0 / 64.0, 3, 0.01f, 1u << 16));
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
    const auto whole = make({3.0});
    CHECK(whole == make({3.0}));
    std::vector<double> small(300, 0.01);
    CHECK(whole == make(small));
    CHECK(whole == make({0.5, 1.5, 0.25, 0.75}));
    CHECK(whole.second == 1500);
}

TEST_CASE("GPU mass and momentum deposit match the CPU splat", "[particles][gpu][grid]")
{
    REQUIRE_GPU();
    ParticleEmitterDef def = BurstEmitter(1, 20000, {0.0, 0.0, 0.0}, {1.0, -2.0, 0.5}, 0.7);
    def.Params.Shape = ParticleEmitterShape::Sphere;
    def.Params.Radius = 3.0;
    def.Params.Temperature = 500.0;
    def.Params.Dispersion = 0.3;

    ParticleGridSpec spec;
    spec.Origin = {-3.0f, -3.0f, -3.0f};
    spec.CellSize = 0.25f;
    spec.Dimensions = {26, 26, 26};
    spec.VelocityReference = 4.0f;
    spec.TemperatureReference = 1000.0f;

    auto cpu = MakeCpu(Config(0.1, 0));
    auto gpu = MakeGpu(Config(0.1, 0));
    ParticleGrid a;
    ParticleGrid b;
    for (auto* system : {cpu.get(), gpu.get()})
    {
        system->Consume(State({def}));
        system->Advance(0.1);
    }
    cpu->Backend().Deposit(spec, a);
    gpu->Backend().Deposit(spec, b);
    double massA = 0.0, massB = 0.0, tempA = 0.0, tempB = 0.0;
    glm::dvec3 momentumA(0.0), momentumB(0.0);
    for (std::size_t i = 0; i < a.Mass.size(); ++i)
    {
        massA += a.Mass[i];
        massB += b.Mass[i];
        tempA += a.MassTemperature[i];
        tempB += b.MassTemperature[i];
        momentumA += glm::dvec3(a.Momentum[i]);
        momentumB += glm::dvec3(b.Momentum[i]);
    }
    CHECK(massB == Approx(massA).epsilon(1e-3));
    CHECK(tempB == Approx(tempA).epsilon(2e-3));
    CHECK(momentumB.x == Approx(momentumA.x).epsilon(2e-3));
    CHECK(momentumB.y == Approx(momentumA.y).epsilon(2e-3));
    CHECK(b.OutsideMass == Approx(a.OutsideMass).margin(1.0));
    CHECK(massB + b.OutsideMass == Approx(0.7 * 20000).epsilon(1e-3));
    double worst = 0.0;
    for (std::size_t i = 0; i < a.Mass.size(); ++i) worst = std::max(worst, static_cast<double>(std::abs(a.Mass[i] - b.Mass[i])));
    CHECK(worst < 0.01);
}

TEST_CASE("Paczynski-Wiita kernels run on the GPU and the ISCO plunges", "[particles][gpu][pw]")
{
    REQUIRE_GPU();
    const auto survives = [](const double r)
    {
        auto system = MakeGpu(Config(0.02, 6, 0.004f), ParticleForceKind::PaczynskiWiita);
        system->SetForce(ParticleForceParams::PaczynskiWiita(1.0f, 0.2f));
        const double omega = std::sqrt(1.0 / (r * (r - 0.2) * (r - 0.2)));
        ParticleEmitterDef def = BurstEmitter(1, 1, {r, 0.0, 0.0}, {2e-3, omega * r, 0.0});
        ParticleSinkDef sink;
        sink.Id = 2;
        sink.Params.Radius = 0.21;
        auto state = State({def});
        state.Sinks.push_back(sink);
        system->Consume(state);
        system->Advance(40.0);
        return system->Board()->Latest()->Stats.Alive == 1;
    };
    CHECK(survives(1.0));
    CHECK_FALSE(survives(0.45));
    CHECK(survives(0.7));
}

TEST_CASE("GPU throughput for a million Newtonian particles", "[.][particles][gpu][bench]")
{
    REQUIRE_GPU();
    constexpr std::uint32_t kParticles = 1'000'000;
    auto system = MakeGpu(Config(0.05, 0, 0.02f, kParticles + 4096));
    system->SetForce(ParticleForceParams::Newtonian(1.0f, 0.01f));
    ParticleEmitterDef def;
    def.Id = 1;
    def.Params.Shape = ParticleEmitterShape::Disk;
    def.Params.Radius = 20.0;
    def.Params.InnerRadius = 2.0;
    def.Params.Rate = 0.0;
    def.Params.Burst = kParticles;
    def.Params.Speed = 0.0;
    system->Consume(State({def}));
    system->Advance(0.05);
    auto& backend = system->Backend();
    ParticleArrays warm;
    backend.Download(warm);
    REQUIRE(warm.Size() == kParticles);

    constexpr int kSteps = 200;
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < kSteps; ++i) system->Advance(0.05);
    ParticleArrays done;
    backend.Download(done);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    const double rate = static_cast<double>(kParticles) * kSteps / seconds;
    WARN("1e6 particles, " << kSteps << " macro steps: " << seconds << " s, " << rate / 1e6 << " million particle-steps/s");
    CHECK(done.Size() == kParticles);
}


TEST_CASE("Recorded GPU frames collect their statistics once the frame has finished", "[particles][gpu][recording]")
{
    REQUIRE_GPU();
    auto& rig = GpuRig::Get();
    const auto scenario = [](ParticleSystem& system)
    {
        ParticleEmitterDef def;
        def.Id = 1;
        def.Params.Shape = ParticleEmitterShape::Sphere;
        def.Params.Radius = 4.0;
        def.Params.Rate = 640.0;
        def.Params.ParticleMass = 0.5;
        def.Params.Seed = 3;
        ParticleSinkDef sink;
        sink.Id = 2;
        sink.Params.Radius = 1.0;
        auto state = State({def});
        state.Sinks.push_back(sink);
        system.SetForce(ParticleForceParams::Newtonian(1.0f, 0.05f));
        system.Consume(state);
    };
    auto config = Config(1.0 / 32.0, 1);
    config.Settings.ReferenceWeight = 0.5f;

    auto direct = MakeGpu(config);
    scenario(*direct);
    for (int i = 0; i < 4; ++i) direct->Advance(1.0);

    auto recorded = MakeGpu(config);
    scenario(*recorded);
    auto& backend = static_cast<GpuParticleBackend&>(recorded->Backend());
    VulkanCommandPool pool(rig.Device, rig.Log, rig.Device->GetQueueIndices().Graphics);
    for (int frame = 0; frame < 4; ++frame)
    {
        const std::uint32_t slot = frame % 3;
        auto cmd = pool.AllocateSingleUseCommandBuffer();
        cmd.Begin();
        backend.BeginRecording(cmd.GetCommandBuffer(), slot);
        recorded->Advance(1.0);
        backend.EndRecording();
        cmd.End();
        cmd.Submit(rig.Device->GetGraphicsQueue());
        backend.CollectCompleted(slot);
        recorded->Refresh();
    }
    const auto a = direct->Board()->Latest();
    const auto b = recorded->Board()->Latest();
    REQUIRE(a);
    REQUIRE(b);
    CHECK(b->Stats.Alive == a->Stats.Alive);
    CHECK(b->Stats.AliveMass == Approx(a->Stats.AliveMass));
    CHECK(recorded->Board()->Sink(2)->Count == direct->Board()->Sink(2)->Count);
    CHECK(Snapshot(*recorded).Hash() == Snapshot(*direct).Hash());
    CHECK(b->Stats.Alive + recorded->Board()->Sink(2)->Count == recorded->Emitted());
}

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

import GPP;
import std;

using namespace GPP;

namespace
{
    struct DriftModule final : ISimulationModule
    {
        std::uint64_t Target{0};
        std::shared_ptr<SimulationRandom> Rng;
        std::vector<float> Dts;

        void OnTick(Scene& scene, float dt) override
        {
            Dts.push_back(dt);
            const auto entity = scene.FindByGuid(Target);
            if (!scene.IsValid(entity)) return;
            auto& transform = scene.Registry().get<TransformComponent>(entity);
            transform.Position.x += dt;
            transform.Position.y += static_cast<float>(Rng ? Rng->NextDouble() : 0.0);
        }
    };

    struct RandomModule final : ISimulationModule
    {
        std::uint64_t Target{0};
        std::shared_ptr<SimulationRandom> Rng;

        void OnTick(Scene& scene, float) override
        {
            const auto entity = scene.FindByGuid(Target);
            if (!scene.IsValid(entity)) return;
            scene.Registry().get<TransformComponent>(entity).Position.y += static_cast<float>(Rng->NextDouble());
        }
    };

    constexpr std::uint64_t kTarget = 4242;

    Scene MakeScene(std::uint64_t seed = 0)
    {
        Scene scene;
        scene.DeserializeFromYaml(std::format(R"(Scene: Headless
{}Entities:
  - Guid: {}
    Components:
      Metadata: {{Guid: {}, Name: Mover, TypeTag: Box}}
      Transform: {{Position: [0, 0, 0]}}
)", seed != 0 ? std::format("Seed: {}\n", seed) : std::string(), kTarget, kTarget));
        return scene;
    }

    float PositionY(const HeadlessResult& result)
    {
        return result.Final.Registry().get<TransformComponent>(result.Final.FindByGuid(kTarget)).Position.y;
    }

    std::expected<HeadlessResult, std::string> Run(const HeadlessOptions& options, std::uint64_t seed = 0)
    {
        auto module = std::make_shared<RandomModule>();
        module->Target = kTarget;
        auto withRng = options;
        withRng.OnRunnerCreated = [&](SimulationRunner& runner) { module->Rng = runner.Random(); };
        return RunHeadless(MakeScene(seed), {module}, withRng);
    }
}

TEST_CASE("Headless run with zero ticks returns the loaded scene", "[headless]")
{
    auto scene = MakeScene(7);
    const auto before = scene.SerializeToYaml();
    auto module = std::make_shared<DriftModule>();
    module->Target = kTarget;
    const auto result = RunHeadless(std::move(scene), {module}, {.Ticks = 0});
    REQUIRE(result.has_value());
    CHECK(result->Ticks == 0);
    CHECK(result->Final.SerializeToYaml() == before);
    CHECK(module->Dts.empty());
}

TEST_CASE("Headless run advances ticks at the requested dt", "[headless]")
{
    auto module = std::make_shared<DriftModule>();
    module->Target = kTarget;
    const auto result = RunHeadless(MakeScene(7), {module}, {.Ticks = 100, .Dt = 0.01});
    REQUIRE(result.has_value());
    CHECK(result->Ticks == 100);
    CHECK(module->Dts.size() == 100);
    CHECK(result->TickRate == Catch::Approx(100.0));
    CHECK(result->Elapsed == Catch::Approx(1.0));
    CHECK(result->SimTime == Catch::Approx(result->Final.Metadata().TimeScale));
}

TEST_CASE("Headless run defaults to 60 Hz", "[headless]")
{
    auto module = std::make_shared<DriftModule>();
    const auto result = RunHeadless(MakeScene(7), {module}, {.Ticks = 60});
    REQUIRE(result.has_value());
    CHECK(result->TickRate == Catch::Approx(60.0));
    CHECK(result->Elapsed == Catch::Approx(1.0));
}

TEST_CASE("Headless runs are deterministic", "[headless]")
{
    const auto a = Run({.Ticks = 200});
    const auto b = Run({.Ticks = 200});
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    CHECK(a->Hash == b->Hash);
    CHECK(a->Final.SerializeToYaml() == b->Final.SerializeToYaml());
}

TEST_CASE("Headless seed selects the random stream", "[headless]")
{
    const auto a = Run({.Ticks = 50, .Seed = 11});
    const auto b = Run({.Ticks = 50, .Seed = 11});
    const auto c = Run({.Ticks = 50, .Seed = 12});
    const auto fromScene = Run({.Ticks = 50}, 11);
    REQUIRE(a.has_value());
    REQUIRE(c.has_value());
    CHECK(PositionY(*a) == PositionY(*b));
    CHECK(PositionY(*a) != PositionY(*c));
    CHECK(PositionY(*a) == PositionY(*fromScene));
}

TEST_CASE("Headless rejects an unusable dt", "[headless]")
{
    for (const double dt : {0.0, -0.1, 2.0, 1e-9, std::numeric_limits<double>::quiet_NaN()})
    {
        const auto result = Run({.Ticks = 1, .Dt = dt});
        CHECK_FALSE(result.has_value());
    }
}

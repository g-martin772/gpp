#include <catch2/catch_test_macros.hpp>
#include <yaml-cpp/yaml.h>

import GPP;
import std;

using namespace GPP;

namespace
{
    struct HealthComponent
    {
        float Value{100.0f};
    };
}

namespace YAML
{
    template <>
    struct convert<HealthComponent>
    {
        static Node encode(const HealthComponent& value)
        {
            Node node;
            node["Value"] = value.Value;
            return node;
        }

        static bool decode(const Node& node, HealthComponent& out)
        {
            if (node["Value"]) out.Value = node["Value"].as<float>();
            return true;
        }
    };
}

namespace
{
    void RegisterTestComponents()
    {
        static std::once_flag flag;
        std::call_once(flag, [] { RegisterComponent<HealthComponent>("Health"); });
    }
}

TEST_CASE ("Scene entities carry stable metadata", "[simulation][scene]")
{
    Scene scene("TestScene");
    const auto entity = scene.CreateEntity("Hero", "Player");

    REQUIRE(scene.IsValid(entity));
    const auto guid = scene.GuidOf(entity);
    CHECK(guid != 0);
    CHECK(scene.FindByGuid(guid) == entity);

    scene.DestroyEntity(entity);
    CHECK_FALSE(scene.IsValid(entity));
    CHECK(scene.FindByGuid(guid) == entt::entity{entt::null});
}

TEST_CASE ("Scene copy deep-clones every registered component", "[simulation][scene]")
{
    RegisterTestComponents();

    Scene scene("Source");
    const auto entity = scene.CreateEntity("Box");
    scene.Registry().emplace<TransformComponent>(entity, TransformComponent{.Position = {1.0f, 2.0f, 3.0f}});
    scene.Registry().emplace<HealthComponent>(entity, HealthComponent{.Value = 42.0f});

    const Scene clone = scene.Clone();
    const auto cloneEntity = clone.FindByGuid(scene.GuidOf(entity));

    REQUIRE(clone.IsValid(cloneEntity));
    CHECK(clone.Registry().get<TransformComponent>(cloneEntity).Position == glm::vec3(1.0f, 2.0f, 3.0f));
    CHECK(clone.Registry().get<HealthComponent>(cloneEntity).Value == 42.0f);

    // Mutating the source after cloning must not affect the clone.
    scene.Registry().get<HealthComponent>(entity).Value = 1.0f;
    CHECK(clone.Registry().get<HealthComponent>(cloneEntity).Value == 42.0f);
}

TEST_CASE ("Scene YAML round-trip preserves entities and components", "[simulation][scene]")
{
    RegisterTestComponents();

    Scene scene("RoundTrip");
    const auto entity = scene.CreateEntity("Rock", "Prop");
    scene.Registry().emplace<TransformComponent>(entity, TransformComponent{.Position = {4.0f, 5.0f, 6.0f}});
    scene.Registry().emplace<HealthComponent>(entity, HealthComponent{.Value = 7.5f});
    const auto guid = scene.GuidOf(entity);

    const auto yaml = scene.SerializeToYaml();

    Scene loaded;
    loaded.DeserializeFromYaml(yaml);

    CHECK(loaded.Metadata().Name == "RoundTrip");
    const auto loadedEntity = loaded.FindByGuid(guid);
    REQUIRE(loaded.IsValid(loadedEntity));
    CHECK(loaded.Registry().get<MetadataComponent>(loadedEntity).Name == "Rock");
    CHECK(loaded.Registry().get<MetadataComponent>(loadedEntity).TypeTag == "Prop");
    CHECK(loaded.Registry().get<TransformComponent>(loadedEntity).Position == glm::vec3(4.0f, 5.0f, 6.0f));
    CHECK(loaded.Registry().get<HealthComponent>(loadedEntity).Value == 7.5f);
}

TEST_CASE ("Scene::SyncInto mirrors create, update, and destroy", "[simulation][scene][sync]")
{
    RegisterTestComponents();

    Scene sim("Sim");
    Scene render = sim.Clone();

    const auto a = sim.CreateEntity("A");
    sim.Registry().emplace<TransformComponent>(a, TransformComponent{.Position = {1.0f, 0.0f, 0.0f}});

    Scene::SyncInto(sim, render);

    const auto renderA = render.FindByGuid(sim.GuidOf(a));
    REQUIRE(render.IsValid(renderA));
    CHECK(render.Registry().get<TransformComponent>(renderA).Position == glm::vec3(1.0f, 0.0f, 0.0f));

    // Update propagates.
    sim.Registry().get<TransformComponent>(a).Position = {9.0f, 0.0f, 0.0f};
    Scene::SyncInto(sim, render);
    CHECK(render.Registry().get<TransformComponent>(renderA).Position == glm::vec3(9.0f, 0.0f, 0.0f));

    // A component removal on the sim side is reflected on the render side.
    sim.Registry().remove<TransformComponent>(a);
    Scene::SyncInto(sim, render);
    CHECK_FALSE(render.Registry().all_of<TransformComponent>(renderA));

    // Destroying the sim entity removes it from the render scene too.
    sim.DestroyEntity(a);
    Scene::SyncInto(sim, render);
    CHECK_FALSE(render.IsValid(renderA));

    // A sim-only (non-registered-for-sync) component never reaches the render scene.
    const auto b = sim.CreateEntity("PhysicsOnly");
    sim.Registry().emplace<PhysicsActorHandle>(b, PhysicsActorHandle{.Actor = reinterpret_cast<void*>(0x1)});
    Scene::SyncInto(sim, render);
    const auto renderB = render.FindByGuid(sim.GuidOf(b));
    REQUIRE(render.IsValid(renderB));
    CHECK_FALSE(render.Registry().all_of<PhysicsActorHandle>(renderB));
}

TEST_CASE ("SimulationRunner ticks on a background thread and syncs the render scene", "[simulation][runner]")
{
    struct CounterModule : ISimulationModule
    {
        void OnInit(Scene& scene) override
        {
            Entity = scene.CreateEntity("Counter");
            scene.Registry().emplace<TransformComponent>(Entity);
        }

        void OnTick(Scene& scene, float) override
        {
            scene.Registry().get<TransformComponent>(Entity).Position.x += 1.0f;
            ++Ticks;
        }

        entt::entity Entity{entt::null};
        std::atomic<int> Ticks{0};
    };

    auto module = std::make_shared<CounterModule>();

    SimulationOptions options;
    options.FixedTimestep = std::chrono::duration<float>(1.0f / 240.0f);

    SimulationRunner runner(Scene("RunnerScene"), module, options);
    runner.Start();

    // Give the background thread room to run several ticks without ever touching it directly;
    // all we read is the synced render-facing copy, which is what a renderer would do.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    runner.Stop();
    CHECK(module->Ticks.load() > 0);

    auto view = runner.LockRenderScene();
    bool found = false;
    for (auto& [guid, entity] : view->Guids().GuidToEntity)
    {
        if (view->Registry().all_of<TransformComponent>(entity))
        {
            found = true;
            CHECK(view->Registry().get<TransformComponent>(entity).Position.x > 0.0f);
        }
    }
    CHECK(found);
}

namespace
{
    std::vector<std::string> EntityNamesInOrder(const std::string& yaml)
    {
        const auto root = YAML::Load(yaml);
        std::vector<std::string> names;
        for (const auto& entityNode : root["Entities"])
        {
            names.push_back(entityNode["Components"]["Metadata"]["Name"].as<std::string>());
        }
        return names;
    }
}

TEST_CASE ("Scene entity order is stable across repeated save/load cycles", "[simulation][scene]")
{
    Scene scene("SimulationDemo");
    scene.CreateEntity("Wanderer A");
    scene.CreateEntity("Wanderer B");

    auto yaml = scene.SerializeToYaml();
    CHECK(EntityNamesInOrder(yaml) == std::vector<std::string>{"Wanderer A", "Wanderer B"});

    // Each iteration mirrors a full app restart: load from disk, clone it the way
    // SimulationRunner does, and save again. Order must not flip back and forth.
    for (int i = 0; i < 4; ++i)
    {
        Scene loaded;
        loaded.DeserializeFromYaml(yaml);
        yaml = loaded.Clone().SerializeToYaml();
        CHECK(EntityNamesInOrder(yaml) == std::vector<std::string>{"Wanderer A", "Wanderer B"});
    }
}

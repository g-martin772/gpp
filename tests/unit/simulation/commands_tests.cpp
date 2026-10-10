#include <catch2/catch_test_macros.hpp>

import GPP;
import std;

using namespace GPP;

namespace
{
    std::uint64_t MakeBox(Scene& scene, const std::string& name)
    {
        const auto entity = scene.CreateEntity(name, "Box");
        scene.Registry().emplace<TransformComponent>(entity);
        return scene.GuidOf(entity);
    }

    glm::vec3 PositionOf(const Scene& scene, const std::uint64_t guid)
    {
        return scene.Registry().get<TransformComponent>(scene.FindByGuid(guid)).Position;
    }

    bool WaitFor(const std::function<bool()>& predicate)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (predicate()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return predicate();
    }

    struct NoopModule final : ISimulationModule
    {
    };
}

TEST_CASE("SetField applies and returns an inverse that restores the value", "[commands]")
{
    Scene scene("S");
    const auto guid = MakeBox(scene, "A");

    Command inverse = DestroyEntityCommand{};
    REQUIRE(ApplyCommand(scene, SetFieldCommand{guid, "Transform", "Position", glm::vec3(1, 2, 3)}, &inverse));
    CHECK(PositionOf(scene, guid) == glm::vec3(1, 2, 3));

    REQUIRE(ApplyCommand(scene, inverse));
    CHECK(PositionOf(scene, guid) == glm::vec3(0, 0, 0));

    CHECK_FALSE(ApplyCommand(scene, SetFieldCommand{guid, "Transform", "Position", glm::vec3(0, 0, 0)}));
    CHECK_FALSE(ApplyCommand(scene, SetFieldCommand{guid, "Nope", "Position", glm::vec3(0, 0, 0)}));
}

TEST_CASE("Add and Remove component invert each other and keep field data", "[commands]")
{
    Scene scene("S");
    const auto guid = MakeBox(scene, "A");

    Command undoAdd = DestroyEntityCommand{};
    REQUIRE(ApplyCommand(scene, AddComponentCommand{guid, "RigidBody", {}}, &undoAdd));
    CHECK_FALSE(ApplyCommand(scene, AddComponentCommand{guid, "RigidBody", {}}));
    REQUIRE(ApplyCommand(scene, SetFieldCommand{guid, "RigidBody", "Mass", 42.0f}));

    Command undoRemove = DestroyEntityCommand{};
    REQUIRE(ApplyCommand(scene, RemoveComponentCommand{guid, "RigidBody"}, &undoRemove));
    CHECK_FALSE(scene.Registry().all_of<RigidBodyComponent>(scene.FindByGuid(guid)));

    REQUIRE(ApplyCommand(scene, undoRemove));
    CHECK(scene.Registry().get<RigidBodyComponent>(scene.FindByGuid(guid)).Mass == 42.0f);

    REQUIRE(ApplyCommand(scene, undoAdd));
    CHECK_FALSE(scene.Registry().all_of<RigidBodyComponent>(scene.FindByGuid(guid)));
}

TEST_CASE("Destroy inverse respawns the entity with its components", "[commands]")
{
    Scene scene("S");
    const auto guid = MakeBox(scene, "Keep");
    REQUIRE(ApplyCommand(scene, SetFieldCommand{guid, "Transform", "Position", glm::vec3(4, 5, 6)}));
    const auto before = scene.SerializeToYaml();

    Command respawn = DestroyEntityCommand{};
    REQUIRE(ApplyCommand(scene, DestroyEntityCommand{guid}, &respawn));
    CHECK_FALSE(scene.IsValid(scene.FindByGuid(guid)));

    REQUIRE(ApplyCommand(scene, respawn));
    CHECK(scene.SerializeToYaml() == before);
}

TEST_CASE("Clone, SetParent and SetExtension are invertible; parent cycles are rejected", "[commands]")
{
    Scene scene("S");
    const auto a = MakeBox(scene, "A");
    const auto b = MakeBox(scene, "B");

    Command undoClone = DestroyEntityCommand{};
    REQUIRE(ApplyCommand(scene, CloneEntityCommand{a, 777}, &undoClone));
    CHECK(scene.IsValid(scene.FindByGuid(777)));
    REQUIRE(ApplyCommand(scene, undoClone));
    CHECK_FALSE(scene.IsValid(scene.FindByGuid(777)));

    Command undoParent = DestroyEntityCommand{};
    REQUIRE(ApplyCommand(scene, SetParentCommand{b, a}, &undoParent));
    CHECK_FALSE(ApplyCommand(scene, SetParentCommand{a, b}));
    CHECK_FALSE(ApplyCommand(scene, SetParentCommand{a, a}));
    REQUIRE(ApplyCommand(scene, undoParent));
    CHECK_FALSE(scene.Registry().all_of<HierarchyComponent>(scene.FindByGuid(b)));

    Command undoExt = DestroyEntityCommand{};
    REQUIRE(ApplyCommand(scene, SetExtensionCommand{"Notes", "hello: 1", false}, &undoExt));
    REQUIRE(scene.FindExtension("Notes") != nullptr);
    REQUIRE(ApplyCommand(scene, undoExt));
    CHECK(scene.FindExtension("Notes") == nullptr);
}

TEST_CASE("Commands round-trip through YAML", "[commands]")
{
    const std::vector<Command> commands{
        SetFieldCommand{1, "Transform", "Position", glm::vec3(1, 2, 3)},
        SetFieldCommand{1, "Transform", "Name", std::string("x")},
        SetFieldCommand{2, "RigidBody", "Mass", 2.5f},
        AddComponentCommand{3, "Camera", "Fov: 40"},
        RemoveComponentCommand{3, "Camera"},
        SpawnEntityCommand{4, "Components:\n  Transform: {}"},
        DestroyEntityCommand{4},
        CloneEntityCommand{5, 6},
        SetParentCommand{6, 5},
        SetExtensionCommand{"Notes", "a: 1", false},
    };
    for (const auto& command : commands)
    {
        const auto text = CommandToYaml(command);
        const auto parsed = CommandFromYaml(text);
        REQUIRE(parsed.has_value());
        CHECK(parsed->index() == command.index());
        CHECK(CommandToYaml(*parsed) == text);
    }
    CHECK_FALSE(CommandFromYaml("Type: Bogus").has_value());
}

TEST_CASE("History coalesces drags into one undo step", "[commands][history]")
{
    CommandHistory history;
    const auto field = [](float v) { return Command{SetFieldCommand{1, "Transform", "Position", glm::vec3(v)}}; };
    const auto now = std::chrono::steady_clock::now();
    history.Record({{field(0)}, {field(1)}, "move", "k", now});
    history.Record({{field(1)}, {field(2)}, "move", "k", now + std::chrono::milliseconds(10)});
    history.Record({{field(2)}, {field(3)}, "move", "k", now + std::chrono::milliseconds(20)});

    auto entry = history.TakeUndo();
    REQUIRE(entry);
    CHECK_FALSE(history.CanUndo());
    REQUIRE(entry->Undo.size() == 1);
    REQUIRE(entry->Redo.size() == 1);
    CHECK(std::get<SetFieldCommand>(entry->Undo[0]).Value == FieldValue{glm::vec3(0)});
    CHECK(std::get<SetFieldCommand>(entry->Redo[0]).Value == FieldValue{glm::vec3(3)});

    history.Record({{field(0)}, {field(1)}, "a", "k", now});
    history.Record({{field(1)}, {field(2)}, "b", "k", now + std::chrono::seconds(5)});
    CHECK(history.TakeUndo().has_value());
    CHECK(history.TakeUndo().has_value());
}

TEST_CASE("Runner undo and redo go through the command queue", "[commands][runner]")
{
    Scene scene("S");
    const auto guid = MakeBox(scene, "A");
    SimulationRunner runner(std::move(scene), std::make_shared<NoopModule>());
    runner.Start();
    runner.SetPaused(true);

    CommandOptions options;
    options.Undoable = true;
    options.Label = "Move";
    runner.EnqueueCommand(SetFieldCommand{guid, "Transform", "Position", glm::vec3(9, 0, 0)}, options);
    REQUIRE(WaitFor([&] { return runner.CanUndo(); }));
    REQUIRE(WaitFor([&] { return PositionOf(*runner.AcquireSnapshot(), guid).x == 9.0f; }));

    REQUIRE(runner.Undo());
    REQUIRE(WaitFor([&] { return PositionOf(*runner.AcquireSnapshot(), guid).x == 0.0f; }));
    CHECK(runner.CanRedo());

    REQUIRE(runner.Redo());
    REQUIRE(WaitFor([&] { return PositionOf(*runner.AcquireSnapshot(), guid).x == 9.0f; }));
    runner.Stop();
}

TEST_CASE("OnlyWhilePlaying commands are dropped while paused", "[simulation][commands]")
{
    Scene scene;
    const auto guid = MakeBox(scene, "A");
    SimulationRunner runner(std::move(scene), std::make_shared<NoopModule>());
    runner.Start();
    runner.SetPaused(true);

    CommandOptions options;
    options.OnlyWhilePlaying = true;
    runner.EnqueueCommand(SetFieldCommand{guid, "Transform", "Position", glm::vec3(5, 0, 0)}, options);
    runner.EnqueueCommand(SetFieldCommand{guid, "Transform", "Position", glm::vec3(0, 7, 0)});
    REQUIRE(WaitFor([&] { return PositionOf(*runner.AcquireSnapshot(), guid).y == 7.0f; }));
    CHECK(PositionOf(*runner.AcquireSnapshot(), guid).x == 0.0f);

    runner.SetPaused(false);
    runner.EnqueueCommand(SetFieldCommand{guid, "Transform", "Position", glm::vec3(5, 0, 0)}, options);
    REQUIRE(WaitFor([&] { return PositionOf(*runner.AcquireSnapshot(), guid).x == 5.0f; }));
    runner.Stop();
}

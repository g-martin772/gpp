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

TEST_CASE("History view lists entries newest first and tracks redo", "[commands][history]")
{
    CommandHistory history;
    const auto field = [](float v) { return Command{SetFieldCommand{1, "Transform", "Position", glm::vec3(v)}}; };
    const auto now = std::chrono::steady_clock::now();
    const auto before = history.Version();
    for (int i = 0; i < 4; ++i)
    {
        history.Record({{field(float(i))}, {field(float(i + 1))}, "step" + std::to_string(i), "",
                        now + std::chrono::seconds(i)});
    }
    CHECK(history.Version() > before);

    auto view = history.View();
    REQUIRE(view.Undo.size() == 4);
    CHECK(view.UndoCount == 4);
    CHECK(view.RedoCount == 0);
    CHECK(view.Undo[0].Label == "step3");
    CHECK(view.Undo[3].Label == "step0");
    CHECK(view.Undo[0].Time == now + std::chrono::seconds(3));
    CHECK(history.View(2).Undo.size() == 2);
    CHECK(history.View(2).UndoCount == 4);

    history.PushRedo(*history.TakeUndo());
    history.PushRedo(*history.TakeUndo());
    view = history.View();
    CHECK(view.UndoCount == 2);
    REQUIRE(view.Redo.size() == 2);
    CHECK(view.Redo[0].Label == "step2");
    CHECK(view.Redo[1].Label == "step3");
    CHECK(history.RedoLabel() == "step2");
}

TEST_CASE("History version changes on every mutation", "[commands][history]")
{
    CommandHistory history;
    const auto field = Command{SetFieldCommand{1, "Transform", "Position", glm::vec3(1)}};
    std::uint64_t last = history.Version();
    const auto advanced = [&]
    {
        const bool moved = history.Version() != last;
        last = history.Version();
        return moved;
    };
    history.Record({{field}, {field}, "a", "", std::chrono::steady_clock::now()});
    CHECK(advanced());
    history.PushRedo(*history.TakeUndo());
    CHECK(advanced());
    CHECK_FALSE(history.View().Redo.empty());
    CHECK_FALSE(advanced());
    history.Clear();
    CHECK(advanced());
    history.SetCapacity(8);
    CHECK(advanced());
}

TEST_CASE("History capacity is configurable and trims the oldest", "[commands][history]")
{
    CommandHistory history;
    CHECK(history.Capacity() == CommandHistory::kDefaultCapacity);
    CHECK(CommandHistory::kDefaultCapacity >= 1024);
    const auto field = Command{SetFieldCommand{1, "Transform", "Position", glm::vec3(1)}};
    const auto now = std::chrono::steady_clock::now();
    for (int i = 0; i < 10; ++i) history.Record({{field}, {field}, "e" + std::to_string(i), "", now});
    history.SetCapacity(4);
    auto view = history.View();
    REQUIRE(view.UndoCount == 4);
    CHECK(view.Undo.front().Label == "e9");
    CHECK(view.Undo.back().Label == "e6");
    history.Record({{field}, {field}, "e10", "", now});
    CHECK(history.View().UndoCount == 4);
    CHECK(history.View().Undo.back().Label == "e7");
    history.SetCapacity(0);
    CHECK(history.Capacity() == 1);
}

TEST_CASE("Runner undoes and redoes several steps at once", "[commands][runner]")
{
    Scene scene("S");
    const auto guid = MakeBox(scene, "A");
    SimulationRunner runner(std::move(scene), std::make_shared<NoopModule>());
    runner.Start();
    runner.SetPaused(true);

    for (int i = 1; i <= 3; ++i)
    {
        CommandOptions options;
        options.Undoable = true;
        options.Label = "Move " + std::to_string(i);
        runner.EnqueueCommand(SetFieldCommand{guid, "Transform", "Position", glm::vec3(float(i), 0, 0)}, options);
    }
    REQUIRE(WaitFor([&] { return runner.History().UndoCount == 3; }));
    REQUIRE(WaitFor([&] { return PositionOf(*runner.AcquireSnapshot(), guid).x == 3.0f; }));

    CHECK(runner.UndoSteps(2) == 2);
    REQUIRE(WaitFor([&] { return PositionOf(*runner.AcquireSnapshot(), guid).x == 1.0f; }));
    CHECK(runner.History().RedoCount == 2);
    CHECK(runner.RedoLabel() == "Move 2");

    CHECK(runner.RedoSteps(5) == 2);
    REQUIRE(WaitFor([&] { return PositionOf(*runner.AcquireSnapshot(), guid).x == 3.0f; }));
    CHECK(runner.UndoSteps(9) == 3);
    REQUIRE(WaitFor([&] { return PositionOf(*runner.AcquireSnapshot(), guid).x == 0.0f; }));
    runner.Stop();
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

namespace
{
    void RegisterCounter()
    {
        CustomCommandRegistry::Instance().Register("Test.Counter", CustomCommandHandler{
            [](Scene& scene, const YAML::Node& payload) -> std::optional<std::string>
            {
                const auto* current = scene.FindExtension("Counter");
                const int previous = current ? (*current)["Value"].as<int>() : -1;
                scene.SetExtension("Counter", payload);
                return std::format("Value: {}", previous);
            },
            [](Scene& scene, const YAML::Node& payload)
            {
                if (payload["Value"].as<int>() < 0) scene.RemoveExtension("Counter");
                else scene.SetExtension("Counter", payload);
                return true;
            }});
    }

    CustomCommand Counter(int value)
    {
        return CustomCommand{"Test.Counter", std::format("Value: {}", value), false};
    }

    int CounterOf(const Scene& scene)
    {
        const auto* node = scene.FindExtension("Counter");
        return node ? (*node)["Value"].as<int>() : -1;
    }
}

TEST_CASE("Custom commands run through registered handlers and invert", "[commands][custom]")
{
    RegisterCounter();
    Scene scene("S");

    Command inverse = DestroyEntityCommand{};
    REQUIRE(ApplyCommand(scene, Counter(7), &inverse));
    CHECK(CounterOf(scene) == 7);
    REQUIRE(std::holds_alternative<CustomCommand>(inverse));
    CHECK(std::get<CustomCommand>(inverse).Undo);
    REQUIRE(ApplyCommand(scene, inverse));
    CHECK(CounterOf(scene) == -1);

    CHECK_FALSE(ApplyCommand(scene, CustomCommand{"Test.Missing", "a: 1", false}));
    CHECK_FALSE(ApplyCommand(scene, CustomCommand{"Test.Counter", "[: bad", false}));

    const Command command = Counter(3);
    const auto text = CommandToYaml(command);
    const auto parsed = CommandFromYaml(text);
    REQUIRE(parsed.has_value());
    CHECK(CommandToYaml(*parsed) == text);
    CHECK(std::get<CustomCommand>(*parsed).Name == "Test.Counter");
}

TEST_CASE("SetComponent replaces a whole component and restores it on undo", "[commands][custom]")
{
    Scene scene("S");
    const auto guid = MakeBox(scene, "A");

    RigidBodyComponent body;
    body.Mass = 12.0f;
    Command undo = DestroyEntityCommand{};
    REQUIRE(ApplyCommand(scene, MakeSetComponentCommand(guid, "RigidBody", body), &undo));
    CHECK(scene.Registry().get<RigidBodyComponent>(scene.FindByGuid(guid)).Mass == 12.0f);

    body.Mass = 20.0f;
    Command undoEdit = DestroyEntityCommand{};
    REQUIRE(ApplyCommand(scene, MakeSetComponentCommand(guid, "RigidBody", body), &undoEdit));
    CHECK_FALSE(ApplyCommand(scene, MakeSetComponentCommand(guid, "RigidBody", body)));
    REQUIRE(ApplyCommand(scene, undoEdit));
    CHECK(scene.Registry().get<RigidBodyComponent>(scene.FindByGuid(guid)).Mass == 12.0f);

    REQUIRE(ApplyCommand(scene, undo));
    CHECK_FALSE(scene.Registry().all_of<RigidBodyComponent>(scene.FindByGuid(guid)));
}

TEST_CASE("A transaction groups commands into one undo step", "[commands][transaction]")
{
    Scene scene("S");
    const auto a = MakeBox(scene, "A");
    const auto b = MakeBox(scene, "B");
    SimulationRunner runner(std::move(scene), std::make_shared<NoopModule>());

    CommandOptions options;
    options.Undoable = true;
    options.Label = "Step";
    {
        ScopedTransaction transaction(runner, "Gizmo drag");
        for (int i = 1; i <= 5; ++i)
        {
            runner.EnqueueCommand(SetFieldCommand{a, "Transform", "Position", glm::vec3(static_cast<float>(i), 0, 0)}, options);
            runner.EnqueueCommand(SetFieldCommand{b, "Transform", "Position", glm::vec3(0, static_cast<float>(i), 0)}, options);
        }
    }
    runner.EnqueueCommand(SetFieldCommand{a, "Transform", "Scale", glm::vec3(3)}, options);
    runner.StepAndWait(1);
    CHECK(PositionOf(*runner.AcquireSnapshot(), a).x == 5.0f);

    REQUIRE(runner.Undo());
    runner.StepAndWait(1);
    CHECK(runner.UndoLabel() == "Gizmo drag");
    REQUIRE(runner.Undo());
    runner.StepAndWait(1);
    CHECK(PositionOf(*runner.AcquireSnapshot(), a) == glm::vec3(0));
    CHECK(PositionOf(*runner.AcquireSnapshot(), b) == glm::vec3(0));
    CHECK_FALSE(runner.CanUndo());

    REQUIRE(runner.Redo());
    REQUIRE(runner.Redo());
    runner.StepAndWait(1);
    CHECK(PositionOf(*runner.AcquireSnapshot(), a).x == 5.0f);
    CHECK(PositionOf(*runner.AcquireSnapshot(), b).y == 5.0f);
}

TEST_CASE("Empty and nested transactions are harmless", "[commands][transaction]")
{
    Scene scene("S");
    MakeBox(scene, "A");
    SimulationRunner runner(std::move(scene), std::make_shared<NoopModule>());
    runner.BeginTransaction("empty");
    runner.EndTransaction();
    runner.BeginTransaction("one");
    runner.BeginTransaction("two");
    runner.EndTransaction();
    runner.EndTransaction();
    runner.StepAndWait(1);
    CHECK_FALSE(runner.CanUndo());
}

TEST_CASE("Custom commands and transactions replay from a recording", "[commands][custom][replay]")
{
    RegisterCounter();
    Scene scene("S");
    const auto guid = MakeBox(scene, "A");
    SimulationRunner source(std::move(scene), std::make_shared<NoopModule>());
    source.StepAndWait(2);
    source.BeginRecording();
    CommandOptions options;
    options.Undoable = true;
    {
        ScopedTransaction transaction(source, "Batch");
        source.EnqueueCommand(Counter(4), options);
        source.EnqueueCommand(SetFieldCommand{guid, "Transform", "Position", glm::vec3(1, 2, 3)}, options);
    }
    source.StepAndWait(2);
    source.Undo();
    source.StepAndWait(2);
    source.Redo();
    source.StepAndWait(2);
    auto recording = source.EndRecording();
    const auto expected = source.AcquireSnapshot()->SerializeToYaml();
    REQUIRE(recording.Commands.size() == 6);
    CHECK(CounterOf(*source.AcquireSnapshot()) == 4);

    SimulationRunner replay(Scene("Empty"), std::make_shared<NoopModule>());
    replay.PrepareReplay(recording);
    replay.StepAndWait(recording.Ticks());
    CHECK(replay.AcquireSnapshot()->SerializeToYaml() == expected);
    CHECK(CounterOf(*replay.AcquireSnapshot()) == 4);
}

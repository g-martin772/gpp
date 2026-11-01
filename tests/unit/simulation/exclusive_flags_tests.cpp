#include <catch2/catch_test_macros.hpp>

import GPP;
import std;

using namespace GPP;

namespace
{
    struct NoopModule final : ISimulationModule
    {
    };

    std::uint64_t MakeCamera(Scene& scene, const std::string& name, const bool primary)
    {
        const auto entity = scene.CreateEntity(name, "Camera");
        scene.Registry().emplace<TransformComponent>(entity);
        scene.Registry().emplace<CameraComponent>(entity, CameraComponent{.Primary = primary});
        return scene.GuidOf(entity);
    }

    bool IsPrimary(const Scene& scene, const std::uint64_t guid)
    {
        return scene.Registry().get<CameraComponent>(scene.FindByGuid(guid)).Primary;
    }

    int CountPrimary(const Scene& scene, const std::vector<std::uint64_t>& guids)
    {
        int count = 0;
        for (const auto guid : guids) count += IsPrimary(scene, guid) ? 1 : 0;
        return count;
    }

    std::vector<bool> Flags(const Scene& scene, const std::vector<std::uint64_t>& guids)
    {
        std::vector<bool> flags;
        for (const auto guid : guids) flags.push_back(IsPrimary(scene, guid));
        return flags;
    }

    CommandOptions Undoable(std::string label)
    {
        CommandOptions options;
        options.Undoable = true;
        options.Label = std::move(label);
        return options;
    }
}

TEST_CASE("Exclusive flag edits leave exactly one holder", "[exclusive]")
{
    RegisterBaseComponents();
    Scene scene("S");
    CHECK(ExclusiveFlagEdits(scene.Registry()).empty());

    const auto a = MakeCamera(scene, "A", false);
    const auto b = MakeCamera(scene, "B", false);
    auto edits = ExclusiveFlagEdits(scene.Registry());
    REQUIRE(edits.size() == 1);
    CHECK(edits[0].Guid == a);
    CHECK(edits[0].Value);
    CHECK(ExclusiveFlagEdits(scene.Registry(), b)[0].Guid == b);

    scene.Registry().get<CameraComponent>(scene.FindByGuid(a)).Primary = true;
    scene.Registry().get<CameraComponent>(scene.FindByGuid(b)).Primary = true;
    edits = ExclusiveFlagEdits(scene.Registry());
    REQUIRE(edits.size() == 1);
    CHECK(edits[0].Guid == b);
    CHECK_FALSE(edits[0].Value);
    CHECK(ExclusiveFlagEdits(scene.Registry(), b)[0].Guid == a);
    CHECK(ExclusiveFlagEdits(scene.Registry(), 0, a)[0].Guid == a);
}

TEST_CASE("Loading a scene repairs the primary camera", "[exclusive]")
{
    RegisterBaseComponents();
    Scene source("S");
    const auto a = MakeCamera(source, "A", true);
    const auto b = MakeCamera(source, "B", true);
    const auto c = MakeCamera(source, "C", false);
    Scene loaded("L");
    loaded.DeserializeFromYaml(source.SerializeToYaml());
    CHECK(CountPrimary(loaded, {a, b, c}) == 1);
    CHECK_FALSE(IsPrimary(loaded, c));

    Scene none("N");
    const auto d = MakeCamera(none, "D", false);
    const auto e = MakeCamera(none, "E", false);
    Scene reloaded("R");
    reloaded.DeserializeFromYaml(none.SerializeToYaml());
    CHECK(CountPrimary(reloaded, {d, e}) == 1);
}

TEST_CASE("Making a camera primary demotes the others and undoes in one step", "[exclusive][commands]")
{
    RegisterBaseComponents();
    Scene scene("S");
    const auto a = MakeCamera(scene, "A", true);
    const auto b = MakeCamera(scene, "B", false);
    const auto c = MakeCamera(scene, "C", false);
    SimulationRunner runner(std::move(scene), std::make_shared<NoopModule>());
    runner.EnqueueCommand(SetFieldCommand{b, "Camera", "Primary", true}, Undoable("Make primary"));
    runner.StepAndWait(1);
    CHECK(Flags(*runner.AcquireSnapshot(), {a, b, c}) == std::vector<bool>{false, true, false});

    REQUIRE(runner.Undo());
    runner.StepAndWait(1);
    CHECK(Flags(*runner.AcquireSnapshot(), {a, b, c}) == std::vector<bool>{true, false, false});
    CHECK_FALSE(runner.CanUndo());

    REQUIRE(runner.Redo());
    runner.StepAndWait(1);
    CHECK(Flags(*runner.AcquireSnapshot(), {a, b, c}) == std::vector<bool>{false, true, false});
}

TEST_CASE("Clearing the only primary camera keeps it primary", "[exclusive][commands]")
{
    RegisterBaseComponents();
    Scene scene("S");
    const auto a = MakeCamera(scene, "A", true);
    const auto b = MakeCamera(scene, "B", false);
    SimulationRunner runner(std::move(scene), std::make_shared<NoopModule>());
    runner.EnqueueCommand(SetFieldCommand{a, "Camera", "Primary", false});
    runner.StepAndWait(1);
    CHECK(Flags(*runner.AcquireSnapshot(), {a, b}) == std::vector<bool>{true, false});
}

TEST_CASE("Deleting the primary camera promotes another and undo restores the original", "[exclusive][commands]")
{
    RegisterBaseComponents();
    Scene scene("S");
    const auto a = MakeCamera(scene, "A", false);
    const auto b = MakeCamera(scene, "B", true);
    const auto c = MakeCamera(scene, "C", false);
    SimulationRunner runner(std::move(scene), std::make_shared<NoopModule>());
    runner.EnqueueCommand(DestroyEntityCommand{b}, Undoable("Delete"));
    runner.StepAndWait(1);
    {
        const auto snapshot = runner.AcquireSnapshot();
        CHECK_FALSE(snapshot->IsValid(snapshot->FindByGuid(b)));
        CHECK(CountPrimary(*snapshot, {a, c}) == 1);
    }

    REQUIRE(runner.Undo());
    runner.StepAndWait(1);
    CHECK(Flags(*runner.AcquireSnapshot(), {a, b, c}) == std::vector<bool>{false, true, false});

    REQUIRE(runner.Redo());
    runner.StepAndWait(1);
    CHECK(CountPrimary(*runner.AcquireSnapshot(), {a, c}) == 1);
}

TEST_CASE("A new camera never steals the primary flag", "[exclusive][commands]")
{
    RegisterBaseComponents();
    Scene scene("S");
    const auto a = MakeCamera(scene, "A", true);
    const auto plain = scene.CreateEntity("Plain", "Empty");
    scene.Registry().emplace<TransformComponent>(plain);
    const auto plainGuid = scene.GuidOf(plain);
    const auto source = MakeCamera(scene, "Source", false);
    SimulationRunner runner(std::move(scene), std::make_shared<NoopModule>());

    runner.EnqueueCommand(AddComponentCommand{plainGuid, "Camera", {}}, Undoable("Add camera"));
    runner.StepAndWait(1);
    CHECK(IsPrimary(*runner.AcquireSnapshot(), a));
    CHECK_FALSE(IsPrimary(*runner.AcquireSnapshot(), plainGuid));

    runner.EnqueueCommand(CloneEntityCommand{a, 777}, Undoable("Clone"));
    runner.StepAndWait(1);
    CHECK(Flags(*runner.AcquireSnapshot(), {a, 777, source}) == std::vector<bool>{true, false, false});

    REQUIRE(runner.Undo());
    REQUIRE(runner.Undo());
    runner.StepAndWait(1);
    CHECK(IsPrimary(*runner.AcquireSnapshot(), a));
}

TEST_CASE("Direct writes keep the primary camera unique", "[exclusive]")
{
    RegisterBaseComponents();
    Scene scene("S");
    const auto a = MakeCamera(scene, "A", true);
    const auto b = MakeCamera(scene, "B", false);
    scene.Registry().get<CameraComponent>(scene.FindByGuid(b)).Primary = true;
    EnforceExclusiveFlags(scene, b);
    CHECK(Flags(scene, {a, b}) == std::vector<bool>{false, true});
}

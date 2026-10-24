#include <catch2/catch_test_macros.hpp>

import GPP;
import std;

using namespace GPP;

namespace
{
    std::uint64_t MakeBox(Scene& scene)
    {
        const auto entity = scene.CreateEntity("A", "Box");
        scene.Registry().emplace<TransformComponent>(entity);
        return scene.GuidOf(entity);
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

    struct Fixture
    {
        std::uint64_t Guid{0};
        SimulationRunner Runner;

        static Scene Make(std::uint64_t& guid)
        {
            Scene scene("S");
            guid = MakeBox(scene);
            return scene;
        }

        Fixture() : Runner(Make(Guid), std::make_shared<NoopModule>())
        {
            Runner.Start();
            Runner.SetPaused(true);
        }
        ~Fixture() { Runner.Stop(); }

        void Move(const float x, const bool undoable = true, const std::string& key = {})
        {
            CommandOptions options;
            options.Undoable = undoable;
            options.CoalesceKey = key;
            Runner.EnqueueCommand(SetFieldCommand{Guid, "Transform", "Position", glm::vec3(x, 0, 0)}, options);
        }

        std::uint64_t Published() const { return Runner.AcquireSnapshot().ContentRevision(); }

        void WaitPublished(const std::uint64_t revision)
        {
            REQUIRE(WaitFor([&] { return Published() == revision; }));
        }

        void WaitPosition(const float x)
        {
            REQUIRE(WaitFor([&]
            {
                const auto snapshot = Runner.AcquireSnapshot();
                const auto entity = snapshot->FindByGuid(Guid);
                return snapshot->Registry().get<TransformComponent>(entity).Position.x == x;
            }));
        }
    };
}

TEST_CASE("RevisionCounter restores a revision only along an unbroken history", "[revision]")
{
    RevisionCounter counter;
    const auto first = counter.Recorded();
    const auto second = counter.Recorded();
    CHECK(first.Before == 0);
    CHECK(second.Before == first.After);

    counter.Moved({second.After, second.Before, second.Epoch});
    CHECK(counter.Current() == first.After);
    counter.Moved({first.After, first.Before, first.Epoch});
    CHECK(counter.Current() == 0);

    counter.Moved({first.Before, first.After, first.Epoch});
    counter.Changed();
    const auto stale = counter.Current();
    counter.Moved({first.After, first.Before, first.Epoch});
    CHECK(counter.Current() != 0);
    CHECK(counter.Current() != stale);
}

TEST_CASE("Content revision follows edits and returns to the start on undo", "[revision][runner]")
{
    Fixture f;
    const auto start = f.Published();

    f.Move(5.0f);
    f.WaitPosition(5.0f);
    const auto edited = f.Published();
    CHECK(edited != start);

    REQUIRE(f.Runner.Undo());
    f.WaitPosition(0.0f);
    CHECK(f.Published() == start);

    REQUIRE(f.Runner.Redo());
    f.WaitPosition(5.0f);
    CHECK(f.Published() == edited);
}

TEST_CASE("Coalesced edits undo to the revision before the first one", "[revision][runner]")
{
    Fixture f;
    const auto start = f.Published();

    f.Move(1.0f, true, "drag");
    f.Move(2.0f, true, "drag");
    f.Move(3.0f, true, "drag");
    f.WaitPosition(3.0f);
    CHECK(f.Published() != start);

    REQUIRE(f.Runner.Undo());
    f.WaitPosition(0.0f);
    CHECK(f.Published() == start);
    CHECK_FALSE(f.Runner.CanUndo());
}

TEST_CASE("A non-undoable edit keeps the scene modified through undo", "[revision][runner]")
{
    Fixture f;
    const auto start = f.Published();

    f.Move(1.0f);
    f.WaitPosition(1.0f);
    f.Runner.EnqueueEdit([](Scene& scene) { scene.SetTimeScale(2.0); });
    REQUIRE(WaitFor([&] { return f.Runner.Metadata().TimeScale == 2.0; }));

    REQUIRE(f.Runner.Undo());
    f.WaitPosition(0.0f);
    CHECK(f.Published() != start);
}

TEST_CASE("System edits and OnlyWhilePlaying commands do not change the content revision", "[revision][runner]")
{
    Fixture f;
    const auto start = f.Published();

    std::atomic<bool> ran{false};
    f.Runner.EnqueueSystemEdit([&](Scene&) { ran = true; });
    REQUIRE(WaitFor([&] { return ran.load(); }));

    CommandOptions options;
    options.OnlyWhilePlaying = true;
    f.Runner.SetPaused(false);
    f.Runner.EnqueueCommand(SetFieldCommand{f.Guid, "Transform", "Position", glm::vec3(4, 0, 0)}, options);
    f.WaitPosition(4.0f);
    f.Runner.SetPaused(true);

    CHECK(f.Published() == start);
}

TEST_CASE("Restoring the content revision after a play session makes the scene clean again", "[revision][runner]")
{
    Fixture f;
    f.Move(1.0f);
    f.WaitPosition(1.0f);
    const auto before = f.Published();

    f.Move(8.0f, false);
    f.WaitPosition(8.0f);
    CHECK(f.Published() != before);

    f.Runner.EnqueueSystemEdit([&](Scene&) { f.Runner.RestoreContentRevision(before); });
    REQUIRE(WaitFor([&] { return f.Published() == before; }));
}

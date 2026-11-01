#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

import GPP;
import std;
import glm;

using namespace GPP;
using Catch::Approx;
namespace fs = std::filesystem;

namespace
{
    struct Recorded
    {
        std::string Name;
        std::vector<LuauValue> Args;

        [[nodiscard]] double Num(const std::size_t i) const { return std::get<double>(Args.at(i)); }
        [[nodiscard]] std::string Text(const std::size_t i) const { return std::get<std::string>(Args.at(i)); }
        [[nodiscard]] bool Flag(const std::size_t i) const { return std::get<bool>(Args.at(i)); }
    };

    struct Outcome
    {
        Scene Final;
        std::uint64_t Hash{0};
        std::vector<Recorded> Events;
        std::vector<ScriptError> Errors;

        [[nodiscard]] std::vector<Recorded> Named(const std::string& name) const
        {
            std::vector<Recorded> out;
            for (const auto& event : Events)
            {
                if (event.Name == name) out.push_back(event);
            }
            return out;
        }

        [[nodiscard]] glm::dvec3 Position(const std::uint64_t guid) const
        {
            return Final.Registry().get<TransformComponent>(Final.FindByGuid(guid)).Position;
        }

        [[nodiscard]] glm::dvec3 Velocity(const std::uint64_t guid) const
        {
            return Final.Registry().get<VelocityComponent>(Final.FindByGuid(guid)).Linear;
        }
    };

    constexpr const char* kProbeEntity = R"(
  - Guid: 100
    Components:
      Metadata: {Guid: 100, Name: Probe, TypeTag: Entity}
      Transform: {Position: [0, 0, 0]}
      Scripts:
        - Name: probe
)";

    Outcome Execute(const std::string& entities, const std::string& script, const std::uint64_t ticks)
    {
        const auto root = fs::temp_directory_path() / "gpp_script_physics_tests";
        fs::remove_all(root);
        fs::create_directories(root);
        std::ofstream(root / "probe.luau") << script;
        AssetOptions options;
        options.Kinds["Scripts"] = AssetKindOptions{{root}, {".luau"}};
        AssetDirectories assets(options);

        RegisterBaseComponents();
        RegisterScriptComponents();
        Scene scene;
        scene.DeserializeFromYaml("Scene: Physics\nSchemaVersion: 2\nEntities:\n" + entities + kProbeEntity);

        Outcome outcome;
        auto physics = std::make_shared<PhysicsSimulationModule>(std::make_shared<EventDispatcher>(), std::make_shared<Logger>());
        auto scripts = std::make_shared<ScriptSimulationModule>(assets);
        scripts->SetPhysics(physics);
        (void)scripts->Host().Events().Subscribe({}, [&](const ScriptEvent& event) { outcome.Events.push_back({event.Name, event.Args}); });
        auto result = RunHeadless(std::move(scene), {scripts, physics},
                                  {.Ticks = ticks, .Dt = 0.01, .Seed = 5,
                                   .OnRunnerCreated = [&](SimulationRunner& runner) { scripts->SetRandom(runner.Random()); }});
        REQUIRE(result.has_value());
        outcome.Final = std::move(result->Final);
        outcome.Hash = result->Hash;
        outcome.Errors = scripts->TakeErrors();
        fs::remove_all(root);
        return outcome;
    }

    std::string Box(const int guid, const std::string& name, const std::string& position, const std::string& type,
                    const std::string& extra = {}, const std::string& colliderExtra = {})
    {
        return std::format(R"(
  - Guid: {0}
    Components:
      Metadata: {{Guid: {0}, Name: {1}, TypeTag: Entity}}
      Transform: {{Position: {2}}}
      Velocity: {{Linear: [0, 0, 0]}}
      RigidBody: {{Type: {3}, Mass: 2, EnableGravity: false{4}}}
      Collider: {{Shape: Box, HalfExtents: [1, 1, 1]{5}}}
)", guid, name, position, type, extra, colliderExtra);
    }

    constexpr const char* kFirstTick = R"(
return {
    OnTick = function(self)
        self.n = (self.n or 0) + 1
        if self.n == 1 then
            %s
        end
    end,
}
)";

    std::string OnFirstTick(const std::string& body)
    {
        std::string script = kFirstTick;
        script.replace(script.find("%s"), 2, body);
        return script;
    }
}

TEST_CASE("physics.raycast returns the closest hit with entity, point, normal and distance", "[scripting][physics-api]")
{
    const auto entities = Box(1, "Near", "[0, 0, 10]", "Static") + Box(2, "Far", "[0, 0, 20]", "Static");
    const auto outcome = Execute(entities, OnFirstTick(R"(
        local hit = physics.raycast(vector.create(0, 0, 0), vector.create(0, 0, 1), 100)
        events.emit("hit", hit.entity, hit.point.x, hit.point.z, hit.normal.z, hit.distance)
        events.emit("miss", physics.raycast(vector.create(0, 0, 0), vector.create(0, 0, 1), 5) == nil)
        events.emit("side", physics.raycast(vector.create(5, 0, 0), vector.create(0, 0, 1), 100) == nil)
        events.emit("table", physics.raycast({ x = 0, y = 0, z = 0 }, { x = 0, y = 0, z = 2 }, 100).distance)
    )"), 2);
    REQUIRE(outcome.Errors.empty());
    const auto hit = outcome.Named("hit");
    REQUIRE(hit.size() == 1);
    CHECK(std::get<std::uint64_t>(hit[0].Args[0]) == 1);
    CHECK(hit[0].Num(1) == Approx(0.0).margin(1e-4));
    CHECK(hit[0].Num(2) == Approx(9.0).margin(1e-4));
    CHECK(hit[0].Num(3) == Approx(-1.0).margin(1e-4));
    CHECK(hit[0].Num(4) == Approx(9.0).margin(1e-4));
    CHECK(outcome.Named("miss")[0].Flag(0));
    CHECK(outcome.Named("side")[0].Flag(0));
    CHECK(outcome.Named("table")[0].Num(0) == Approx(9.0).margin(1e-4));
}

TEST_CASE("physics.raycast is correct 1e11 m from the origin", "[scripting][physics-api][precision]")
{
    const auto entities = Box(1, "Target", "[1.0e11, 0, 10]", "Static") + Box(2, "Anchor", "[1.0e11, 0, 30]", "Static");
    const auto outcome = Execute(entities, OnFirstTick(R"(
        local hit = physics.raycast({ x = 1e11, y = 0, z = 0 }, vector.create(0, 0, 1), 100)
        events.emit("hit", hit.entity, hit.point64.x, hit.point64.y, hit.point64.z, hit.distance, hit.normal.z)
        local sweep = physics.spherecast({ x = 1e11, y = 0, z = 0 }, 0.5, vector.create(0, 0, 1), 100)
        events.emit("sweep", sweep.entity, sweep.distance)
        local near = physics.overlap({ x = 1e11, y = 0, z = 10 }, 0.25)
        events.emit("overlap", #near, near[1])
    )"), 2);
    REQUIRE(outcome.Errors.empty());
    const auto hit = outcome.Named("hit");
    REQUIRE(hit.size() == 1);
    CHECK(std::get<std::uint64_t>(hit[0].Args[0]) == 1);
    CHECK(hit[0].Num(1) == Approx(1.0e11).margin(1.0));
    CHECK(hit[0].Num(3) == Approx(9.0).margin(1e-3));
    CHECK(hit[0].Num(4) == Approx(9.0).margin(1e-3));
    CHECK(hit[0].Num(5) == Approx(-1.0).margin(1e-4));
    CHECK(outcome.Named("sweep")[0].Num(1) == Approx(8.5).margin(1e-3));
    CHECK(outcome.Named("overlap")[0].Num(0) == 1);
}

TEST_CASE("physics.spherecast sweeps a sphere and physics.overlap lists entities sorted by guid", "[scripting][physics-api]")
{
    const auto entities = Box(7, "C", "[2, 0, 0]", "Static") + Box(3, "A", "[0, 0, 0]", "Static") +
                          Box(5, "B", "[-2, 0, 0]", "Static") + Box(9, "Wall", "[0, 0, 10]", "Static");
    const auto outcome = Execute(entities, OnFirstTick(R"(
        local hit = physics.spherecast(vector.create(0, 0, 3), 0.5, vector.create(0, 0, 1), 100)
        events.emit("sweep", hit.entity, hit.distance, hit.normal.z)
        local ids = physics.overlap(vector.create(0, 0, 0), 2.5)
        events.emit("ids", #ids, ids[1], ids[2], ids[3])
        events.emit("none", #physics.overlap(vector.create(50, 0, 0), 1))
        events.emit("masked", #physics.overlap(vector.create(0, 0, 0), 2.5, 0))
    )"), 2);
    REQUIRE(outcome.Errors.empty());
    const auto sweep = outcome.Named("sweep");
    REQUIRE(sweep.size() == 1);
    CHECK(std::get<std::uint64_t>(sweep[0].Args[0]) == 9);
    CHECK(sweep[0].Num(1) == Approx(5.5).margin(1e-3));
    CHECK(sweep[0].Num(2) == Approx(-1.0).margin(1e-4));
    const auto ids = outcome.Named("ids");
    REQUIRE(ids.size() == 1);
    CHECK(ids[0].Num(0) == 3);
    CHECK(std::get<std::uint64_t>(ids[0].Args[1]) == 3);
    CHECK(std::get<std::uint64_t>(ids[0].Args[2]) == 5);
    CHECK(std::get<std::uint64_t>(ids[0].Args[3]) == 7);
    CHECK(outcome.Named("none")[0].Num(0) == 0);
    CHECK(outcome.Named("masked")[0].Num(0) == 0);
}

TEST_CASE("Collider Layer and Mask filter both queries and collisions", "[scripting][physics-api][layers]")
{
    const auto entities = Box(1, "LayerOne", "[0, 0, 10]", "Static", "", ", Layer: 1") +
                          Box(2, "LayerTwo", "[0, 0, 20]", "Static", "", ", Layer: 2") +
                          Box(3, "Floor", "[50, -3, 0]", "Static", "", ", Layer: 2") +
                          Box(4, "Ghost", "[50, 0, 0]", "Dynamic", "", ", Layer: 1, Mask: 1") +
                          Box(5, "Solid", "[80, 0, 0]", "Dynamic", "", ", Layer: 1") +
                          Box(6, "Floor2", "[80, -3, 0]", "Static", "", ", Layer: 2");
    const auto outcome = Execute(entities, OnFirstTick(R"(
        local only_two = physics.raycast(vector.create(0, 0, 0), vector.create(0, 0, 1), 100, 4)
        events.emit("two", only_two.entity)
        local only_one = physics.raycast(vector.create(0, 0, 0), vector.create(0, 0, 1), 100, 2)
        events.emit("one", only_one.entity)
        events.emit("empty", physics.raycast(vector.create(0, 0, 0), vector.create(0, 0, 1), 100, 8) == nil)
        physics.set_velocity(scene.find("Ghost"), vector.create(0, -5, 0))
        physics.set_velocity(scene.find("Solid"), vector.create(0, -5, 0))
    )"), 120);
    REQUIRE(outcome.Errors.empty());
    CHECK(std::get<std::uint64_t>(outcome.Named("two")[0].Args[0]) == 2);
    CHECK(std::get<std::uint64_t>(outcome.Named("one")[0].Args[0]) == 1);
    CHECK(outcome.Named("empty")[0].Flag(0));
    CHECK(outcome.Position(4).y < -3.0);
    CHECK(outcome.Position(5).y > -3.0);
    CHECK(outcome.Position(5).y < -0.5);
}

TEST_CASE("physics forces, impulses and velocities change the body at the right tick", "[scripting][physics-api]")
{
    const auto entities = Box(1, "Impulsed", "[0, 0, 0]", "Dynamic") + Box(2, "Forced", "[10, 0, 0]", "Dynamic") +
                          Box(3, "Spun", "[20, 0, 0]", "Dynamic") + Box(4, "Fast", "[30, 0, 0]", "Dynamic") +
                          Box(5, "Late", "[40, 0, 0]", "Dynamic") + Box(6, "Torqued", "[50, 0, 0]", "Dynamic");
    const auto script = R"(
return {
    OnTick = function(self)
        self.n = (self.n or 0) + 1
        if self.n == 1 then
            physics.add_impulse(scene.find("Impulsed"), vector.create(4, 0, 0))
            physics.add_impulse(scene.find("Spun"), vector.create(0, 0, 2), vector.create(20, 1, 0))
            physics.set_velocity(scene.find("Fast"), vector.create(0, 7, 0))
            physics.set_angular_velocity(scene.find("Fast"), vector.create(0, 0, 3))
            events.emit("before", scene.get(scene.find("Impulsed"), "Velocity", "Linear").x)
        end
        if self.n <= 10 then
            physics.add_force(scene.find("Forced"), vector.create(6, 0, 0))
            physics.add_torque(scene.find("Torqued"), vector.create(0, 0, 4))
        end
        if self.n == 2 then
            events.emit("tick2", scene.get(scene.find("Late"), "Velocity", "Linear").x)
        end
    end,
    OnLateTick = function(self)
        if self.n == 1 then
            events.emit("late1", scene.get(scene.find("Impulsed"), "Velocity", "Linear").x)
            physics.add_impulse(scene.find("Late"), vector.create(4, 0, 0))
        end
        if self.n == 2 then
            events.emit("late2", scene.get(scene.find("Late"), "Velocity", "Linear").x)
        end
    end,
}
)";
    const auto outcome = Execute(entities, script, 10);
    REQUIRE(outcome.Errors.empty());
    CHECK(outcome.Named("before")[0].Num(0) == Approx(0.0).margin(1e-6));
    CHECK(outcome.Named("late1")[0].Num(0) == Approx(2.0).margin(1e-4));
    CHECK(outcome.Named("tick2")[0].Num(0) == Approx(0.0).margin(1e-6));
    CHECK(outcome.Named("late2")[0].Num(0) == Approx(2.0).margin(1e-4));
    CHECK(outcome.Velocity(1).x == Approx(2.0).margin(1e-4));
    CHECK(outcome.Velocity(2).x == Approx(3.0 * 0.1).margin(1e-3));
    CHECK(outcome.Velocity(4).y == Approx(7.0).margin(1e-4));
    const auto spun = outcome.Final.Registry().get<VelocityComponent>(outcome.Final.FindByGuid(3));
    CHECK(spun.Linear.z == Approx(1.0).margin(1e-4));
    CHECK(std::abs(spun.Angular.x) > 0.01);
    CHECK(outcome.Final.Registry().get<VelocityComponent>(outcome.Final.FindByGuid(4)).Angular.z == Approx(3.0).margin(0.05));
    CHECK(std::abs(outcome.Final.Registry().get<VelocityComponent>(outcome.Final.FindByGuid(6)).Angular.z) > 0.01);
}

TEST_CASE("physics.add_impulse behaves 1e11 m from the origin", "[scripting][physics-api][precision]")
{
    const auto entities = Box(1, "Far", "[1.0e11, 5.0e10, -2.0e10]", "Dynamic") + Box(2, "Mate", "[1.0000000001e11, 5.0e10, -2.0e10]", "Static");
    const auto outcome = Execute(entities, OnFirstTick(R"(
        local far = scene.find("Far")
        physics.add_impulse(far, vector.create(10, 0, 0), { x = 1e11, y = 5e10, z = -2e10 })
    )"), 100);
    REQUIRE(outcome.Errors.empty());
    CHECK(outcome.Velocity(1).x == Approx(5.0).margin(1e-3));
    CHECK(outcome.Position(1).x == Approx(1.0e11 + 5.0).margin(0.1));
    CHECK(outcome.Position(1).y == Approx(5.0e10).margin(0.01));
}

TEST_CASE("physics.set_kinematic_target moves a kinematic body and pushes dynamic ones", "[scripting][physics-api]")
{
    const auto entities = Box(1, "Mover", "[0, 0, 0]", "Kinematic") + Box(2, "Pushed", "[2.5, 0, 0]", "Dynamic");
    const auto script = R"(
return {
    OnTick = function(self)
        self.n = (self.n or 0) + 1
        local mover = scene.find("Mover")
        physics.set_kinematic_target(mover, vector.create(self.n * 0.1, 0, 0))
        if self.n == 5 then physics.set_kinematic_target(mover, { x = 3, y = 4, z = 5 }, vector.create(0, 90, 0)) end
    end,
}
)";
    const auto outcome = Execute(entities, script, 5);
    REQUIRE(outcome.Errors.empty());
    CHECK(outcome.Position(1).x == Approx(3.0).margin(1e-6));
    CHECK(outcome.Position(1).y == Approx(4.0).margin(1e-6));
    CHECK(outcome.Position(1).z == Approx(5.0).margin(1e-6));
    const auto rotation = outcome.Final.Registry().get<TransformComponent>(outcome.Final.FindByGuid(1)).Rotation;
    CHECK(std::abs(rotation.y) == Approx(std::sin(glm::radians(45.0f))).margin(1e-4));

    const auto sweep = Execute(entities, R"(
return {
    OnTick = function(self)
        self.n = (self.n or 0) + 1
        physics.set_kinematic_target(scene.find("Mover"), vector.create(self.n * 0.05, 0, 0))
    end,
}
)", 100);
    REQUIRE(sweep.Errors.empty());
    CHECK(sweep.Position(1).x == Approx(5.0).margin(1e-6));
    CHECK(sweep.Position(2).x > 5.0);
}

TEST_CASE("physics.set_gravity_scale writes the RigidBody GravityScale field", "[scripting][physics-api]")
{
    const auto outcome = Execute(Box(1, "Ball", "[0, 0, 0]", "Dynamic"), OnFirstTick(R"(
        physics.set_gravity_scale(scene.find("Ball"), 0.25)
    )"), 2);
    REQUIRE(outcome.Errors.empty());
    CHECK(outcome.Final.Registry().get<RigidBodyComponent>(outcome.Final.FindByGuid(1)).GravityScale == Approx(0.25));
}

TEST_CASE("Joints hold bodies together until they break or are broken", "[scripting][physics-api][joints]")
{
    const auto entities = Box(1, "Anchor", "[0, 0, 0]", "Static") + Box(2, "Held", "[0, 0, 5]", "Dynamic") +
                          Box(3, "Loose", "[20, 0, 5]", "Dynamic") + Box(4, "Wall", "[40, 0, 0]", "Static") +
                          Box(5, "Fragile", "[40, 0, 5]", "Dynamic") + Box(6, "Hinged", "[60, 0, 5]", "Dynamic") +
                          Box(7, "Rope", "[80, 0, 5]", "Dynamic") + Box(8, "Hook", "[80, 0, 0]", "Static") +
                          Box(9, "Released", "[100, 0, 5]", "Dynamic") + Box(10, "Pin", "[100, 0, 0]", "Static");
    const auto script = R"(
return {
    OnTick = function(self)
        self.n = (self.n or 0) + 1
        if self.n == 1 then
            local held = scene.find("Held")
            physics.create_joint(scene.find("Anchor"), held, "fixed")
            self.fragile = physics.create_joint(scene.find("Wall"), scene.find("Fragile"), "fixed", { break_force = 50 })
            physics.create_joint(scene.find("Hook"), scene.find("Rope"), "distance", { distance = 5 })
            physics.create_joint(scene.find("Hinged"), nil, "hinge", { anchor = vector.create(60, 0, 5), axis = vector.create(0, 1, 0) })
            self.released = physics.create_joint(scene.find("Pin"), scene.find("Released"), "fixed")
            events.emit("handles", self.fragile, self.released)
        end
        if self.n == 3 then events.emit("intact", physics.joint_broken(self.fragile)) end
        if self.n == 5 then
            events.emit("break", physics.break_joint(self.released), physics.break_joint(self.released))
            events.emit("unknown", physics.joint_broken(self.released) == nil)
        end
        if self.n == 10 then
            for _, name in { "Held", "Fragile", "Hinged", "Rope" } do
                physics.add_impulse(scene.find(name), vector.create(0, 0, 40))
            end
            physics.add_impulse(scene.find("Loose"), vector.create(0, 0, 40))
            physics.add_impulse(scene.find("Released"), vector.create(0, 0, 40))
        end
        if self.n == 30 then physics.add_impulse(scene.find("Fragile"), vector.create(0, 0, 40)) end
        if self.n == 80 then events.emit("broken", physics.joint_broken(self.fragile)) end
    end,
}
)";
    const auto outcome = Execute(entities, script, 100);
    REQUIRE(outcome.Errors.empty());
    CHECK(outcome.Named("intact")[0].Flag(0) == false);
    CHECK(outcome.Named("break")[0].Flag(0));
    CHECK_FALSE(outcome.Named("break")[0].Flag(1));
    CHECK(outcome.Named("unknown")[0].Flag(0));
    CHECK(outcome.Named("broken")[0].Flag(0));
    CHECK(outcome.Position(2).z == Approx(5.0).margin(0.05));
    CHECK(outcome.Position(3).z > 20.0);
    CHECK(outcome.Position(5).z > 15.0);
    CHECK(outcome.Position(9).z > 20.0);
    CHECK(glm::length(outcome.Position(7) - glm::dvec3(80, 0, 0)) < 5.2);
    CHECK(outcome.Position(6).y == Approx(0.0).margin(0.05));
    CHECK(outcome.Position(6).z < 10.0);
}

TEST_CASE("Bad physics arguments raise Luau errors instead of crashing", "[scripting][physics-api][errors]")
{
    const auto entities = Box(1, "Body", "[0, 0, 0]", "Dynamic") + Box(2, "Floor", "[0, -9, 0]", "Static") +
                          Box(3, "Platform", "[0, 9, 0]", "Kinematic");
    const auto outcome = Execute(entities, OnFirstTick(R"(
        local body, floor, platform, probe = scene.find("Body"), scene.find("Floor"), scene.find("Platform"), scene.find("Probe")
        local v = vector.create(1, 0, 0)
        local function check(label, ok, err) events.emit("err", label, ok, tostring(err)) end
        check("unknown", pcall(physics.add_force, scene.find("Ghost"), v))
        check("not entity", pcall(physics.add_force, 12, v))
        check("no body", pcall(physics.add_impulse, probe, v))
        check("static", pcall(physics.set_velocity, floor, v))
        check("kinematic", pcall(physics.add_torque, platform, v))
        check("dynamic target", pcall(physics.set_kinematic_target, body, v))
        check("vector", pcall(physics.add_force, body, "up"))
        check("nan", pcall(physics.add_force, body, vector.create(0 / 0, 0, 0)))
        check("zero dir", pcall(physics.raycast, v, vector.create(0, 0, 0), 10))
        check("range", pcall(physics.raycast, v, v, -1))
        check("radius", pcall(physics.overlap, v, 0))
        check("kind", pcall(physics.create_joint, body, floor, "spring"))
        check("same", pcall(physics.create_joint, body, body, "fixed"))
        check("param", pcall(physics.create_joint, body, floor, "fixed", { strength = 1 }))
        check("kinematic joint", pcall(physics.create_joint, floor, platform, "fixed"))
        check("scale", pcall(physics.set_gravity_scale, probe, 1))
        check("mask", pcall(physics.raycast, v, v, 10, "all"))
    )"), 2);
    REQUIRE(outcome.Errors.empty());
    const auto errors = outcome.Named("err");
    REQUIRE(errors.size() == 17);
    const std::vector<std::pair<std::string, std::string>> expected{
        {"unknown", "expected an entity"},
        {"not entity", "expected an entity"},
        {"no body", "entity 'Probe' has no RigidBody and Collider"},
        {"static", "entity 'Floor' must be a Dynamic body"},
        {"kinematic", "entity 'Platform' must be a Dynamic body"},
        {"dynamic target", "entity 'Body' must be a Kinematic body"},
        {"vector", "force must be a vector or {x, y, z}"},
        {"nan", "finite"},
        {"zero dir", "direction must not be zero"},
        {"range", "max_dist must be a positive number"},
        {"radius", "radius must be a positive number"},
        {"kind", "kind must be"},
        {"same", "two different entities"},
        {"param", "unknown parameter 'strength'"},
        {"kinematic joint", "at least one dynamic body"},
        {"scale", "has no RigidBody and Collider"},
        {"mask", "layer_mask must be a number"},
    };
    for (std::size_t i = 0; i < expected.size(); ++i)
    {
        INFO(expected[i].first);
        CHECK(errors[i].Text(0) == expected[i].first);
        CHECK_FALSE(errors[i].Flag(1));
        CHECK(errors[i].Text(2).find(expected[i].second) != std::string::npos);
    }
}

TEST_CASE("A script using physics replays to the same state hash", "[scripting][physics-api][determinism]")
{
    const auto entities = Box(1, "Floor", "[0, -2, 0]", "Static") + Box(2, "A", "[0, 0, 0]", "Dynamic") +
                          Box(3, "B", "[3, 0, 0]", "Dynamic") + Box(4, "K", "[6, 0, 0]", "Kinematic");
    const auto script = R"(
return {
    OnTick = function(self)
        self.n = (self.n or 0) + 1
        local a, b, k = scene.find("A"), scene.find("B"), scene.find("K")
        if self.n == 1 then physics.create_joint(a, b, "distance", { distance = 4, stiffness = 20, damping = 1 }) end
        physics.add_force(a, vector.create(math.random() * 4, 0, 0))
        physics.add_impulse(b, vector.create(0, 0.1, math.random()), vector.create(3, 0.5, 0))
        physics.set_kinematic_target(k, vector.create(6, math.sin(self.n * 0.1), 0))
        local hit = physics.raycast(vector.create(0, 10, 0), vector.create(0, -1, 0), 50)
        if hit and self.n % 20 == 0 then physics.add_torque(b, hit.point * 0.01) end
    end,
}
)";
    const auto first = Execute(entities, script, 200);
    REQUIRE(first.Errors.empty());
    for (int i = 0; i < 2; ++i) CHECK(Execute(entities, script, 200).Hash == first.Hash);
}

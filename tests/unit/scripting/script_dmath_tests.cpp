#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

import GPP;
import std;

using namespace GPP;
using Catch::Approx;
namespace fs = std::filesystem;

namespace
{
    ScriptTestFile RunLuau(const std::string& source)
    {
        const auto root = fs::temp_directory_path() / "gpp_dmath_tests";
        fs::create_directories(root / "lib");
        AssetOptions options;
        options.Kinds["Scripts"] = AssetKindOptions{{root}, {".luau"}};
        AssetDirectories assets(options);
        ScriptTestOptions runner;
        runner.Assets = &assets;
        auto file = RunScriptTestSource(source, "dmath.luau", runner);
        INFO(FormatScriptTestReport({.Files = {file}}));
        CHECK(file.Ok());
        return file;
    }

    struct Rig
    {
        Rig() : Scene("DMath")
        {
            RegisterBaseComponents();
            Entity = Scene.CreateEntity("Far");
            Scene.Registry().emplace<TransformComponent>(Entity);
            Scene.Registry().emplace<VelocityComponent>(Entity);
            Guid = Scene.GuidOf(Entity);
            Context = std::make_shared<SceneBindings>();
            Context->Read = &Scene;
            Context->Write = [this](std::function<void(GPP::Scene&)> write) { Queued.push_back(std::move(write)); };
            Vm.Register("test", "guid", [this](LuauNativeCall& call)
            {
                call.PushEntity(Guid);
                return 1;
            });
            RegisterMathBindings(Vm);
            RegisterSceneBindings(Vm, Context);
            Vm.Seal();
        }

        [[nodiscard]] std::expected<void, LuauError> Run(const std::string& body)
        {
            auto handle = Vm.Load("local M = {}\nfunction M.run()\nlocal e = test.guid()\n" + body + "\nend\nreturn M", "=rig");
            REQUIRE(handle.has_value());
            return Vm.Call(*handle, "run");
        }

        void Flush()
        {
            for (auto& write : Queued) write(Scene);
            Queued.clear();
        }

        GPP::Scene Scene;
        entt::entity Entity{entt::null};
        std::uint64_t Guid{0};
        std::shared_ptr<SceneBindings> Context;
        std::vector<std::function<void(GPP::Scene&)>> Queued;
        LuauVm Vm;
    };
}

TEST_CASE("dvec3 keeps 1e11 m positions with millimetre offsets that a float32 vector loses", "[luau][dmath][precision]")
{
    RunLuau(R"lua(
test("offsets survive arithmetic and round trips", function()
    local base = dvec3.new(1e11, -2.5e11, 3e11)
    local moved = base + dvec3.new(1e-3, 1e-3, -1e-3)
    local delta = moved - base
    assert_near(delta.x, 1e-3, 1e-4)
    assert_near(delta.z, -1e-3, 1e-4)
    local x, y, z = dvec3.unpack(moved)
    assert_eq(dvec3.new(x, y, z) == moved, true)
    local narrow = dvec3.to_vec(moved)
    assert(math.abs(narrow.x - moved.x) > 1000, "float32 should be off by kilometres at 1e11")
    local back = dvec3.from_vec(narrow)
    assert(back ~= moved)
    assert((back - base).x ~= delta.x)
end)

test("many small steps accumulate exactly where float32 stalls", function()
    local p = dvec3.new(1e11, 0, 0)
    local step = dvec3.new(0.125, 0, 0)
    local f = vector.create(1e11, 0, 0)
    local fstep = vector.create(0.125, 0, 0)
    for _ = 1, 1000 do
        p = p + step
        f = f + fstep
    end
    assert_eq(p.x - 1e11, 125)
    assert_eq(f.x, vector.create(1e11, 0, 0).x)
end)
)lua");
}

TEST_CASE("dvec3 operators, comparison and length", "[luau][dmath]")
{
    RunLuau(R"lua(
test("operators", function()
    local a, b = dvec3.new(1, 2, 3), dvec3.new(4, 5, 6)
    assert_eq(a + b == dvec3.new(5, 7, 9), true)
    assert_eq(b - a == dvec3.new(3, 3, 3), true)
    assert_eq(a * 2 == dvec3.new(2, 4, 6), true)
    assert_eq(2 * a == dvec3.new(2, 4, 6), true)
    assert_eq(a * b == dvec3.new(4, 10, 18), true)
    assert_eq(b / 2 == dvec3.new(2, 2.5, 3), true)
    assert_eq(b / a == dvec3.new(4, 2.5, 2), true)
    assert_eq(-a == dvec3.new(-1, -2, -3), true)
    assert_eq(a ~= b, true)
    assert_eq(a == dvec3.new(1, 2, 3), true)
    assert_eq(#dvec3.new(3, 4, 12), 13)
    assert_eq(typeof(a), "dvec3")
    assert_eq(tostring(dvec3.new(1, 2.5, -3)), "dvec3(1, 2.5, -3)")
    assert_eq(dvec3.new() == dvec3.new(0, 0, 0), true)
end)

test("functions and methods", function()
    local a, b = dvec3.new(1, 0, 0), dvec3.new(0, 1, 0)
    assert_eq(dvec3.dot(a, b), 0)
    assert_eq(dvec3.cross(a, b) == dvec3.new(0, 0, 1), true)
    assert_eq(dvec3.length(dvec3.new(2, 3, 6)), 7)
    assert_eq(dvec3.length_squared(dvec3.new(2, 3, 6)), 49)
    assert_near(dvec3.length(dvec3.normalize(dvec3.new(1e11, 1e11, 0))), 1, 1e-15)
    assert_eq(dvec3.normalize(dvec3.new(0, 0, 0)) == dvec3.new(0, 0, 0), true)
    assert_eq(dvec3.distance(dvec3.new(1e11, 0, 0), dvec3.new(1e11, 3, 4)), 5)
    assert_eq(dvec3.lerp(a, b, 0.5) == dvec3.new(0.5, 0.5, 0), true)
    assert_eq(dvec3.to_vec(dvec3.new(1, 2, 3)), vector.create(1, 2, 3))
    assert_eq(dvec3.from_vec(vector.create(1, 2, 3)) == dvec3.new(1, 2, 3), true)
    assert_eq(a:dot(b), 0)
    assert_eq(a:cross(b) == dvec3.new(0, 0, 1), true)
    assert_eq(dvec3.new(0, 3, 4):length(), 5)
    assert_eq(dvec3.new(0, 3, 4):normalize() == dvec3.new(0, 0.6, 0.8), true)
    assert_eq(a:distance(b), math.sqrt(2))
    assert_eq(a:to_vec(), vector.create(1, 0, 0))
end)

test("values are immutable and usable as table keys by identity only", function()
    local a = dvec3.new(1, 2, 3)
    expect_error(function() a.x = 5 end, "dvec3 is immutable")
    expect_error(function() return a.nope end, "dvec3 has no member 'nope'")
    expect_error(function() return a[1] end, "read by name")
    expect_error(function() dvec3.new = nil end)
    expect_error(function() dquat.new = nil end)
    assert_eq(getmetatable(a), "locked")
end)
)lua");
}

TEST_CASE("dvec3 and dquat misuse reports readable errors", "[luau][dmath]")
{
    RunLuau(R"lua(
test("operators reject foreign operands", function()
    local a = dvec3.new(1, 2, 3)
    expect_error(function() return a + 1 end, "cannot apply '+' to dvec3 and number")
    expect_error(function() return 1 + a end, "cannot apply '+' to number and dvec3")
    expect_error(function() return a - vector.create(1, 1, 1) end, "cannot apply '-' to dvec3 and vector")
    expect_error(function() return a * "x" end, "cannot apply '*' to dvec3 and string")
    expect_error(function() return 1 / a end, "cannot apply '/' to number and dvec3")
    expect_error(function() return a + dquat.new() end, "cannot apply '+' to dvec3 and dquat")
    expect_error(function() return dquat.new() * 2 end, "cannot apply '*' to dquat and number")
    expect_error(function() return dquat.new() + dquat.new() end)
    expect_error(function() return a < a end)
end)

test("functions name the argument and the type received", function()
    expect_error(function() dvec3.new(1, 2) end, "dvec3.new: z must be a number, got no value")
    expect_error(function() dvec3.new("a", 2, 3) end, "dvec3.new: x must be a number, got string")
    expect_error(function() dvec3.dot(dvec3.new(), 1) end, "dvec3.dot: argument 2 must be a dvec3, got number")
    expect_error(function() dvec3.length(vector.create(1, 2, 3)) end, "must be a dvec3, got vector")
    expect_error(function() dvec3.from_vec({ x = 1 }) end, "dvec3.from_vec: argument 1 must be a dvec3 or vector, got table")
    expect_error(function() dquat.rotate(dquat.new(), vector.create(1, 0, 0)) end, "dquat.rotate: argument 2 must be a dvec3, got vector")
    expect_error(function() dquat.mul(dquat.new(), dvec3.new()) end, "dquat.mul: argument 2 must be a dquat, got dvec3")
    expect_error(function() dquat.from_axis_angle(dvec3.new(), 1) end, "axis must not be zero")
    expect_error(function() dquat.from_axis_angle(dvec3.new(0, 1, 0), "x") end, "dquat.from_axis_angle: angle must be a number")
    expect_error(function() dquat.new(1, 2, 3) end, "dquat.new: w must be a number")
end)
)lua");
}

TEST_CASE("dquat construction, rotation, composition and interpolation", "[luau][dmath]")
{
    RunLuau(R"lua(
local HALF_PI = math.pi / 2

test("axis angle rotates a vector exactly enough", function()
    local q = dquat.from_axis_angle(dvec3.new(0, 0, 1), HALF_PI)
    local r = dquat.rotate(q, dvec3.new(1, 0, 0))
    assert_near(r, dvec3.new(0, 1, 0), 1e-15)
    assert_near(q * dvec3.new(1, 0, 0), dvec3.new(0, 1, 0), 1e-15)
    assert_near(q:rotate(dvec3.new(0, 1, 0)), dvec3.new(-1, 0, 0), 1e-15)
    local far = dquat.rotate(q, dvec3.new(1e11, 0, 1e-3))
    assert_near(far.y, 1e11, 1e-4)
    assert_near(far.z, 1e-3, 1e-4)
end)

test("mul composes, inverse undoes, normalize fixes drift", function()
    local a = dquat.from_axis_angle(vector.create(0, 1, 0), 0.3)
    local b = dquat.from_axis_angle(vector.create(1, 0, 0), -1.1)
    local v = dvec3.new(1, 2, 3)
    assert_near(dquat.rotate(dquat.mul(a, b), v), dquat.rotate(a, dquat.rotate(b, v)), 1e-14)
    assert_near((a * b) * v, a * (b * v), 1e-14)
    assert_near(dquat.rotate(dquat.inverse(a), dquat.rotate(a, v)), v, 1e-14)
    assert_near(dquat.mul(a, dquat.inverse(a)), dquat.new(), 1e-15)
    assert_near(dquat.length(dquat.normalize(dquat.new(1, 2, 3, 4))), 1, 1e-15)
    assert_near(dquat.conjugate(dquat.new(1, 2, 3, 4)), dquat.new(-1, -2, -3, 4), 0)
    assert_eq(dquat.dot(dquat.new(1, 2, 3, 4), dquat.new(1, 1, 1, 1)), 10)
    assert_eq(a == dquat.from_axis_angle(vector.create(0, 1, 0), 0.3), true)
    assert_eq(typeof(a), "dquat")
end)

test("slerp takes the short arc and hits the endpoints", function()
    local a = dquat.new()
    local b = dquat.from_axis_angle(dvec3.new(0, 0, 1), math.pi / 2)
    assert_near(dquat.slerp(a, b, 0), a, 1e-15)
    assert_near(dquat.slerp(a, b, 1), b, 1e-15)
    local mid = dquat.slerp(a, b, 0.5)
    assert_near(mid, dquat.from_axis_angle(dvec3.new(0, 0, 1), math.pi / 4), 1e-15)
    local flipped = dquat.new(-b.x, -b.y, -b.z, -b.w)
    assert_near(dquat.slerp(a, flipped, 0.5), mid, 1e-15)
    assert_near(dquat.slerp(a, a, 0.3), a, 1e-15)
end)

test("euler matches the scene rotation convention and quat conversions", function()
    local q = dquat.from_euler(30, 40, 50)
    local e = dquat.to_euler(q)
    assert_near(e, dvec3.new(30, 40, 50), 1e-12)
    assert_near(dquat.from_euler(dvec3.new(30, 40, 50)), q, 1e-15)
    assert_near(dquat.from_euler(vector.create(30, 40, 50)), q, 1e-6)
    local narrow = dquat.to_quat(q)
    assert_near(narrow, vec4(q.x, q.y, q.z, q.w), 0)
    assert_near(dquat.from_quat(narrow), q, 1e-6)
    assert_near(dquat.to_euler(dquat.from_euler(0, 90, 0)).y, 90, 1e-9)
end)
)lua");
}

TEST_CASE("scene.get64 and set64 round trip 64-bit positions and rotations", "[luau][dmath][bindings]")
{
    Rig rig;
    const glm::dvec3 far(1.0e11 + 1.0e-3, -2.5e11 + 2.0e-3, 3.0e11 - 4.0e-3);
    rig.Scene.Registry().get<TransformComponent>(rig.Entity).Position = far;
    rig.Scene.Registry().get<VelocityComponent>(rig.Entity).Linear = glm::dvec3(2.9e4 + 1e-3, 0.0, 0.0);

    auto result = rig.Run(R"lua(
        local p = scene.get64(e, "Transform", "Position")
        assert(typeof(p) == "dvec3")
        assert(p.x == 1.0e11 + 1.0e-3)
        assert(scene.get64(e, "Velocity", "Linear").x == 2.9e4 + 1e-3)
        assert(typeof(scene.get64(e, "Velocity", "Angular")) == "dvec3")
        assert(typeof(scene.get64(e, "Transform", "Scale")) == "dvec3")
        assert(typeof(scene.get64(e, "Transform", "Rotation")) == "dquat")
        assert(scene.get(e, "Transform", "Position").x ~= p.x)
        assert(typeof(scene.get(e, "Transform", "Position")) == "vector")
        assert(scene.get64(e, "Transform", "Nope") == nil)
        assert(scene.set64(e, "Transform", "Position", p + dvec3.new(1e-3, 0, -1e-3)))
        assert(scene.set64(e, "Transform", "Rotation", dquat.from_axis_angle(dvec3.new(0, 1, 0), 1.0)))
        assert(scene.set64(e, "Transform", "Scale", dvec3.new(2, 3, 4)))
        assert(not scene.set64(e, "Transform", "Nope", p))
        assert(not scene.set64(e, "Velocity", "Linear", p))
    )lua");
    INFO((result ? "" : result.error().Message));
    REQUIRE(result.has_value());
    REQUIRE(rig.Queued.size() == 3);
    CHECK(rig.Scene.Registry().get<TransformComponent>(rig.Entity).Position == far);
    rig.Flush();
    const auto& transform = rig.Scene.Registry().get<TransformComponent>(rig.Entity);
    CHECK(transform.Position.x == far.x + 1.0e-3);
    CHECK(transform.Position.y == far.y);
    CHECK(transform.Position.z == far.z - 1.0e-3);
    CHECK(transform.Scale == glm::vec3(2.0f, 3.0f, 4.0f));
    const glm::quat expected = glm::quat(std::cos(0.5f), 0.0f, std::sin(0.5f), 0.0f);
    CHECK(std::abs(glm::dot(transform.Rotation, expected)) == Approx(1.0).margin(1e-6));

    result = rig.Run(R"lua(
        local q = scene.get64(e, "Transform", "Rotation")
        local back = dquat.rotate(q, dvec3.new(1, 0, 0))
        assert(math.abs(back.x - math.cos(1.0)) < 1e-6)
        assert(math.abs(back.z + math.sin(1.0)) < 1e-6)
    )lua");
    INFO((result ? "" : result.error().Message));
    CHECK(result.has_value());
}

TEST_CASE("scene.set64 accepts vectors for narrow fields and reports type mismatches", "[luau][dmath][bindings]")
{
    Rig rig;
    auto result = rig.Run(R"lua(
        assert(scene.set64(e, "Transform", "Position", vector.create(1, 2, 3)))
        assert(scene.set64(e, "Transform", "Scale", vector.create(5, 6, 7)))
        assert(scene.set64(e, "Transform", "Rotation", dvec3.new(0, 90, 0)))
    )lua");
    INFO((result ? "" : result.error().Message));
    REQUIRE(result.has_value());
    rig.Flush();
    CHECK(rig.Scene.Registry().get<TransformComponent>(rig.Entity).Position == glm::dvec3(1.0, 2.0, 3.0));
    CHECK(rig.Scene.Registry().get<TransformComponent>(rig.Entity).Scale == glm::vec3(5.0f, 6.0f, 7.0f));

    const auto fail = [&](const std::string& body, const std::string& needle)
    {
        const auto outcome = rig.Run(body);
        REQUIRE_FALSE(outcome.has_value());
        INFO(outcome.error().Message);
        CHECK(outcome.error().Message.find(needle) != std::string::npos);
    };
    fail("scene.set64(e, 'Transform', 'Position', 5)", "scene.set64: Transform.Position needs a dvec3 or vector, got number");
    fail("scene.set64(e, 'Transform', 'Position', {x = 1, y = 2, z = 3})", "got table");
    fail("scene.set64(e, 'Transform', 'Rotation', 'up')", "Transform.Rotation needs a dquat or dvec3, got string");
    fail("scene.set64(e, 'Transform', 'Position', dquat.new())", "got dquat");
    fail("scene.set64(e, 'Transform', 'Position')", "got no value");
}

TEST_CASE("scene.set64 goes through the command route and keeps the double", "[luau][dmath][bindings]")
{
    Rig rig;
    std::vector<Command> commands;
    rig.Context->WriteCommand = [&commands](Command command) { commands.push_back(std::move(command)); };
    const auto result = rig.Run(R"lua(
        assert(scene.set64(e, "Transform", "Position", dvec3.new(1e11 + 1e-3, 0, 0)))
        assert(scene.set(e, "Transform", "Position", dvec3.new(2e11 + 1e-3, 0, 0)))
    )lua");
    INFO((result ? "" : result.error().Message));
    REQUIRE(result.has_value());
    REQUIRE(commands.size() == 2);
    REQUIRE(rig.Queued.empty());
    for (std::size_t i = 0; i < commands.size(); ++i)
    {
        const auto* set = std::get_if<SetFieldCommand>(&commands[i]);
        REQUIRE(set != nullptr);
        CHECK(set->Guid == rig.Guid);
        CHECK(set->Component == "Transform");
        const auto* value = std::get_if<glm::dvec3>(&set->Value);
        REQUIRE(value != nullptr);
        CHECK(value->x == (i == 0 ? 1.0e11 : 2.0e11) + 1.0e-3);
    }
}

TEST_CASE("dvec3 events keep full precision from emit to delivery", "[luau][dmath][events]")
{
    RunLuau(R"lua(
test("a dvec3 argument crosses the event bus unchanged", function()
    local p = dvec3.new(1e11 + 1e-3, -3e11, 7.25)
    sim.emit("far", p, 5)
    sim.step(1)
    local got = sim.events("far")
    assert_eq(#got, 1)
    assert_eq(typeof(got[1].args[1]), "dvec3")
    assert_eq(got[1].args[1] == p, true)
    assert_eq(got[1].args[2], 5)
end)

test("a dquat argument arrives as a float32 vec4", function()
    sim.emit("turn", dquat.from_axis_angle(dvec3.new(0, 0, 1), 1))
    sim.step(1)
    local q = sim.events("turn")[1].args[1]
    assert_near(q.z, math.sin(0.5), 1e-6)
    assert_near(q.w, math.cos(0.5), 1e-6)
end)
)lua");
}

TEST_CASE("dvec3 math is deterministic across repeated runs", "[luau][dmath][determinism]")
{
    const auto run = []
    {
        std::string out;
        LuauVm vm;
        vm.Register("test", "out", [&out](LuauNativeCall& call)
        {
            out += call.String(1) + "\n";
            return 0;
        });
        RegisterMathBindings(vm);
        vm.Seal();
        auto handle = vm.Load(R"(local M = {}
function M.run()
    local p = dvec3.new(1.5e11, -2e11, 9e10)
    local v = dvec3.new(29780.5, -1234.125, 77.0625)
    local q = dquat.from_euler(12, 34, 56)
    for i = 1, 5000 do
        p = p + v * (1 / 60) + dquat.rotate(q, dvec3.new(1e-3, 0, 0))
        q = dquat.normalize(dquat.mul(q, dquat.from_axis_angle(dvec3.normalize(dvec3.new(1, 2, 3)), 1e-3)))
        v = dvec3.lerp(v, dquat.rotate(q, v), 0.01)
    end
    test.out(tostring(p))
    test.out(tostring(q))
    test.out(tostring(v))
    test.out(tostring(dquat.slerp(q, dquat.new(0, 1, 0, 0), 0.37)))
end
return M)", "=det");
        REQUIRE(handle.has_value());
        const auto result = vm.Call(*handle, "run");
        INFO((result ? "" : result.error().Message));
        REQUIRE(result.has_value());
        return out;
    };
    const auto first = run();
    CHECK_FALSE(first.empty());
    CHECK(first == run());
    CHECK(first == run());
}

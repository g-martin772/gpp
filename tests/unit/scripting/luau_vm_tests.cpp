#include <catch2/catch_test_macros.hpp>

import GPP;
import std;

using namespace GPP;

namespace
{
    int LoadOk(LuauVm& vm, const std::string& source)
    {
        auto handle = vm.Load(source, "=test");
        REQUIRE(handle.has_value());
        return *handle;
    }
}

TEST_CASE("The VM runs a chunk, calls its functions and registers natives", "[luau][vm]")
{
    LuauVm vm;
    double captured = 0.0;
    vm.Register("host", "capture", [&captured](LuauNativeCall& call)
    {
        captured = call.Number(1);
        call.PushNumber(captured * 2.0);
        return 1;
    });
    vm.Seal();
    const int handle = LoadOk(vm, "local M = {}\nfunction M.run(x) host.capture(x + 1) end\nreturn M");
    REQUIRE(vm.Call(handle, "run", std::array{41.0}).has_value());
    CHECK(captured == 42.0);
    CHECK_FALSE(vm.Call(handle, "missing").has_value());
}

TEST_CASE("The sandbox exposes no io, os, require or dynamic loading and freezes the globals", "[luau][vm][sandbox]")
{
    LuauVm vm;
    vm.Seal();
    const int handle = LoadOk(vm, R"(
        local M = {}
        function M.check()
            assert(io == nil and os == nil and require == nil and loadstring == nil and debug == nil and dofile == nil)
            assert(math.sqrt(4) == 2)
            assert(not pcall(function() math.sqrt = nil end))
            assert(not pcall(function() string.len = nil end))
        end
        return M)");
    auto result = vm.Call(handle, "check");
    INFO((result ? "" : result.error().Message));
    CHECK(result.has_value());
}

TEST_CASE("Runaway scripts and unbounded allocation are stopped", "[luau][vm][limits]")
{
    LuauVm vm;
    vm.SetLimits(LuauLimits{.MaxSafepoints = 100000, .MaxSeconds = 5.0, .MaxBytes = 32ull * 1024 * 1024});
    vm.Seal();
    const int handle = LoadOk(vm, R"(
        local M = {}
        function M.spin() while true do end end
        function M.fill() local t = {} for i = 1, 1e9 do t[i] = {i} end end
        function M.fine() local n = 0 for i = 1, 1000 do n += i end end
        return M)");
    auto spin = vm.Call(handle, "spin");
    REQUIRE_FALSE(spin.has_value());
    CHECK(spin.error().Message.find("instruction limit") != std::string::npos);
    CHECK_FALSE(vm.Call(handle, "fill").has_value());
    CHECK(vm.Call(handle, "fine").has_value());
}

TEST_CASE("Script errors carry the generated line", "[luau][vm]")
{
    LuauVm vm;
    vm.Seal();
    const int handle = LoadOk(vm, "local M = {}\nfunction M.boom()\n  local x = nil\n  return x.y\nend\nreturn M");
    auto result = vm.Call(handle, "boom");
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().Line == 4);
    CHECK_FALSE(vm.Load("local = 3", "=bad").has_value());
}

TEST_CASE("Source maps locate generated lines and unknown lines map to zero", "[luau][vm]")
{
    LuauSourceMap map;
    map.Add(7);
    map.Add(9);
    CHECK(map.Locate(1) == 7);
    CHECK(map.Locate(2) == 9);
    CHECK(map.Locate(3) == 0);
    map.Shift(2);
    CHECK(map.Locate(3) == 7);
}

TEST_CASE("Math bindings give vec2/vec4 operators and scripts load from the Scripts asset directory", "[luau][bindings]")
{
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "gpp_luau_scripts_test";
    fs::create_directories(root);
    {
        std::ofstream(root / "adder.luau") << "local M = {}\nfunction M.sum() local v = vec2(1, 2) + vec2(3, 4) * 2 ; out.set(v.x + v.y) end\nreturn M";
    }
    AssetOptions options;
    options.Kinds["Scripts"] = AssetKindOptions{{root}, {".luau"}};
    AssetDirectories assets(options);

    LuauVm vm;
    double result = 0;
    vm.Register("out", "set", [&result](LuauNativeCall& call)
    {
        result = call.Number(1);
        return 0;
    });
    RegisterMathBindings(vm);
    vm.Seal();
    auto handle = LoadScript(vm, assets, "adder");
    REQUIRE(handle.has_value());
    REQUIRE(vm.Call(*handle, "sum").has_value());
    CHECK(result == 17.0);
    CHECK_FALSE(LoadScript(vm, assets, "missing").has_value());
    CHECK(ListScripts(assets) == std::vector<std::string>{"adder"});
    fs::remove_all(root);
}

TEST_CASE("Scene bindings read fields directly and route writes through the queue", "[luau][bindings]")
{
    RegisterBaseComponents();
    Scene scene("ScriptScene");
    const auto entity = scene.CreateEntity("Thing");
    scene.Registry().emplace<TransformComponent>(entity).Position = glm::vec3(1.0f, 2.0f, 3.0f);
    const auto guid = scene.GuidOf(entity);

    auto context = std::make_shared<SceneBindings>();
    context->Read = &scene;
    std::vector<std::function<void(Scene&)>> queued;
    context->Write = [&queued](std::function<void(Scene&)> write) { queued.push_back(std::move(write)); };

    LuauVm vm;
    vm.Register("test", "guid", [guid](LuauNativeCall& call)
    {
        call.PushEntity(guid);
        return 1;
    });
    RegisterSceneBindings(vm, context);
    vm.Seal();
    auto handle = vm.Load(R"(local M = {}
function M.run()
    local e = test.guid()
    assert(scene.exists(e))
    assert(scene.get(e, "Transform", "Position").y == 2)
    assert(scene.get(e, "Transform", "Nope") == nil)
    assert(scene.set(e, "Transform", "Position", vector.create(9, 8, 7)))
    assert(not scene.set(e, "Transform", "Position", 5))
end
return M)", "=bind");
    REQUIRE(handle.has_value());
    auto result = vm.Call(*handle, "run");
    INFO((result ? "" : result.error().Message));
    REQUIRE(result.has_value());
    REQUIRE(queued.size() == 1);
    CHECK(scene.Registry().get<TransformComponent>(entity).Position.x == 1.0f);
    queued[0](scene);
    CHECK(scene.Registry().get<TransformComponent>(entity).Position == glm::vec3(9.0f, 8.0f, 7.0f));
}

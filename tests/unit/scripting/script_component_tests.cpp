#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <yaml-cpp/yaml.h>

import GPP;
import std;

using namespace GPP;
using Catch::Approx;
namespace fs = std::filesystem;

namespace
{
    struct ScriptDir
    {
        fs::path Root;
        AssetOptions Options;

        explicit ScriptDir(const std::string& name)
        {
            Root = fs::temp_directory_path() / ("gpp_script_components_" + name);
            fs::remove_all(Root);
            fs::create_directories(Root);
            Options.Kinds["Scripts"] = AssetKindOptions{{Root}, {".luau"}};
        }

        ~ScriptDir() { fs::remove_all(Root); }

        void Write(const std::string& script, const std::string& source, const int bumpSeconds = 0) const
        {
            const auto path = Root / (script + ".luau");
            std::ofstream(path, std::ios::trunc) << source;
            if (bumpSeconds != 0) fs::last_write_time(path, fs::file_time_type::clock::now() + std::chrono::seconds(bumpSeconds));
        }
    };

    constexpr const char* kOrbiter = R"(
return {
    properties = {
        { name = "Target", type = "entity" },
        { name = "Speed", type = "float", default = 2, min = 0, max = 10 },
        { name = "Steps", type = "int", default = 3 },
        { name = "Active", type = "bool", default = true },
        { name = "Offset", type = "vec3", default = vector.create(1, 2, 3) },
        { name = "Tint", type = "color" },
        { name = "Label", type = "string", default = "hi" },
    },
    OnStart = function(self) self.angle = 0 end,
    OnTick = function(self, dt)
        self.angle += self.props.Speed * dt
        scene.set(self.entity, "Transform", "Position", vector.create(self.angle, 0, 0))
    end,
})";
}

TEST_CASE("A script header declares typed properties with defaults and ranges", "[scripting][components]")
{
    ScriptDir dir("catalog");
    dir.Write("orbiter", kOrbiter);
    AssetDirectories assets(dir.Options);
    ScriptCatalog catalog(assets, std::chrono::milliseconds(0));

    const auto descriptor = catalog.Describe("orbiter");
    INFO(descriptor->Error);
    REQUIRE(descriptor->Ok());
    REQUIRE(descriptor->Properties.size() == 7);
    const auto* speed = descriptor->Find("Speed");
    REQUIRE(speed != nullptr);
    CHECK(speed->Type == ScriptPropType::Float);
    CHECK(std::get<float>(speed->Default) == 2.0f);
    CHECK(speed->HasRange);
    CHECK(speed->Max == 10.0f);
    CHECK(std::get<int>(descriptor->Find("Steps")->Default) == 3);
    CHECK(std::get<bool>(descriptor->Find("Active")->Default));
    CHECK(std::get<glm::vec3>(descriptor->Find("Offset")->Default) == glm::vec3(1.0f, 2.0f, 3.0f));
    CHECK(std::get<glm::vec3>(descriptor->Find("Tint")->Default) == glm::vec3(1.0f));
    CHECK(std::get<std::string>(descriptor->Find("Label")->Default) == "hi");
    CHECK(std::get<std::uint64_t>(descriptor->Find("Target")->Default) == 0);
    CHECK(PropertyMeta(*descriptor->Find("Tint")).Kind == FieldKind::Color);
    CHECK(ToFieldType(ScriptPropType::Entity) == FieldType::Entity);
}

TEST_CASE("Bad headers and missing scripts produce descriptor errors", "[scripting][components]")
{
    ScriptDir dir("badheader");
    dir.Write("badtype", "return { properties = { { name = 'A', type = 'quaternion' } } }");
    dir.Write("baddefault", "return { properties = { { name = 'A', type = 'float', default = 'x' } } }");
    dir.Write("dup", "return { properties = { { name = 'A', type = 'int' }, { name = 'A', type = 'int' } } }");
    dir.Write("syntax", "return {");
    dir.Write("plain", "return { OnTick = function() end }");
    AssetDirectories assets(dir.Options);
    ScriptCatalog catalog(assets, std::chrono::milliseconds(0));

    CHECK(catalog.Describe("badtype")->Error.find("unknown type") != std::string::npos);
    CHECK(catalog.Describe("baddefault")->Error.find("default") != std::string::npos);
    CHECK(catalog.Describe("dup")->Error.find("duplicate") != std::string::npos);
    CHECK_FALSE(catalog.Describe("syntax")->Ok());
    CHECK_FALSE(catalog.Describe("nothing")->Ok());
    const auto plain = catalog.Describe("plain");
    CHECK(plain->Ok());
    CHECK(plain->Properties.empty());
}

TEST_CASE("The catalog re-reads a script when its source changes", "[scripting][components]")
{
    ScriptDir dir("recatalog");
    dir.Write("s", "return { properties = { { name = 'A', type = 'int' } } }");
    AssetDirectories assets(dir.Options);
    ScriptCatalog catalog(assets, std::chrono::milliseconds(0));
    REQUIRE(catalog.Describe("s")->Properties.size() == 1);
    dir.Write("s", "return { properties = { { name = 'A', type = 'int' }, { name = 'B', type = 'bool' } } }");
    CHECK(catalog.Describe("s")->Properties.size() == 2);
}

TEST_CASE("Property values coerce to the declared type", "[scripting][components]")
{
    CHECK(std::get<float>(CoerceProperty(3, ScriptPropType::Float)) == 3.0f);
    CHECK(std::get<int>(CoerceProperty(2.9f, ScriptPropType::Int)) == 2);
    CHECK(std::get<bool>(CoerceProperty(1, ScriptPropType::Bool)));
    CHECK(std::get<std::uint64_t>(CoerceProperty(42, ScriptPropType::Entity)) == 42);
    CHECK(std::get<glm::vec3>(CoerceProperty(glm::vec4(1, 2, 3, 4), ScriptPropType::Color)) == glm::vec3(1, 2, 3));
    CHECK(std::get<std::string>(CoerceProperty(5, ScriptPropType::String)) == "5");
    CHECK(std::holds_alternative<std::monostate>(CoerceProperty(std::string("x"), ScriptPropType::Float)));
    CHECK(std::holds_alternative<std::monostate>(CoerceProperty(glm::vec3(1.0f), ScriptPropType::Bool)));
}

TEST_CASE("Resolved props overlay the entry on the defaults, clamp ranges and drop unknown names", "[scripting][components]")
{
    ScriptDescriptor descriptor;
    descriptor.Properties.push_back({"Speed", ScriptPropType::Float, 2.0f, 0.0f, 10.0f, true});
    descriptor.Properties.push_back({"Steps", ScriptPropType::Int, 3, 0.0f, 0.0f, false});
    descriptor.Properties.push_back({"Target", ScriptPropType::Entity, std::uint64_t{0}, 0.0f, 0.0f, false});
    ScriptEntry entry{"x", {{"Speed", 99}, {"Ghost", 1}, {"Target", 7}}};
    const auto resolved = ResolveProps(descriptor, entry);
    REQUIRE(resolved.size() == 3);
    CHECK(std::get<float>(resolved[0].second) == 10.0f);
    CHECK(std::get<int>(resolved[1].second) == 3);
    CHECK(std::get<std::uint64_t>(resolved[2].second) == 7);
    CHECK(std::holds_alternative<std::monostate>(ResolveProps(descriptor, ScriptEntry{"x", {}})[2].second));
}

TEST_CASE("Script entries round-trip through YAML and through the scene file", "[scripting][components][persistence]")
{
    RegisterBaseComponents();
    RegisterScriptComponents();

    ScriptsComponent scripts;
    scripts.Entries.push_back(ScriptEntry{"SpringArm", {{"ArmLength", 8.5f}, {"Steps", 4}, {"On", true}, {"Offset", glm::vec3(1, 2, 3)},
                                                        {"Label", std::string("cam")}, {"Target", std::uint64_t{12345678901234567890ull}}}});
    scripts.Entries.push_back(ScriptEntry{"Other", {}});
    const auto decoded = ScriptsFromYaml(ScriptsToYaml(scripts));
    REQUIRE(decoded.Entries.size() == 2);
    CHECK(decoded.Entries[0].Name == "SpringArm");
    CHECK(std::get<float>(decoded.Entries[0].Props.at("ArmLength")) == 8.5f);
    CHECK(std::get<int>(decoded.Entries[0].Props.at("Steps")) == 4);
    CHECK(std::get<bool>(decoded.Entries[0].Props.at("On")));
    CHECK(std::get<glm::vec3>(decoded.Entries[0].Props.at("Offset")) == glm::vec3(1, 2, 3));
    CHECK(std::get<std::string>(decoded.Entries[0].Props.at("Label")) == "cam");
    CHECK(std::get<std::uint64_t>(decoded.Entries[0].Props.at("Target")) == 12345678901234567890ull);
    CHECK(decoded.Entries[1].Props.empty());

    Scene scene("Persist");
    const auto entity = scene.CreateEntity("Camera");
    scene.Registry().emplace<ScriptsComponent>(entity, scripts);
    const std::string yaml = scene.SerializeToYaml();
    CHECK(yaml.find("Scripts:") != std::string::npos);
    CHECK(yaml.find("ArmLength") != std::string::npos);

    Scene loaded("Loaded");
    loaded.DeserializeFromYaml(yaml);
    const auto restored = loaded.FindByGuid(scene.GuidOf(entity));
    REQUIRE(loaded.IsValid(restored));
    const auto& back = loaded.Registry().get<ScriptsComponent>(restored);
    REQUIRE(back.Entries.size() == 2);
    CHECK(std::get<float>(back.Entries[0].Props.at("ArmLength")) == 8.5f);
    CHECK(std::get<std::uint64_t>(back.Entries[0].Props.at("Target")) == 12345678901234567890ull);
}

TEST_CASE("Scripts written by hand in YAML load with coerced props", "[scripting][components][persistence]")
{
    RegisterBaseComponents();
    RegisterScriptComponents();
    Scene scene("Hand");
    scene.DeserializeFromYaml(R"(Scene: Hand
Entities:
  - Guid: 5
    Components:
      Metadata: {Guid: 5, Name: Cam, TypeTag: Entity}
      Scripts:
        - Name: Foo
          Props: {Length: 8, Target: 7, Flag: true, Where: [1, 2, 3]}
)");
    const auto& scripts = scene.Registry().get<ScriptsComponent>(scene.FindByGuid(5));
    REQUIRE(scripts.Entries.size() == 1);
    CHECK(std::get<int>(scripts.Entries[0].Props.at("Length")) == 8);
    CHECK(std::get<bool>(scripts.Entries[0].Props.at("Flag")));
    CHECK(std::get<glm::vec3>(scripts.Entries[0].Props.at("Where")) == glm::vec3(1, 2, 3));
}

namespace
{
    struct HostRig
    {
        LuauVm Vm;
        std::shared_ptr<LuauTaskErrors> Errors = std::make_shared<LuauTaskErrors>();
        std::shared_ptr<SceneBindings> Bindings = std::make_shared<SceneBindings>();
        std::vector<std::function<void(Scene&)>> Queue;
        std::unique_ptr<AssetDirectories> Assets;
        std::unique_ptr<ScriptHost> Host;
        Scene World{"Host"};

        explicit HostRig(const ScriptDir& dir)
        {
            RegisterBaseComponents();
            RegisterScriptComponents();
            Assets = std::make_unique<AssetDirectories>(dir.Options);
            Bindings->Write = [this](std::function<void(Scene&)> write) { Queue.push_back(std::move(write)); };
            RegisterMathBindings(Vm);
            RegisterSceneBindings(Vm, Bindings);
            RegisterTaskBindings(Vm, Errors);
            auto context = RegisterScriptHostBindings(Vm);
            Vm.Seal();
            Host = std::make_unique<ScriptHost>(Vm, context, Errors, *Assets);
            Bindings->Read = &World;
            Assets->Poll();
        }

        entt::entity Spawn(const std::string& name, std::vector<ScriptEntry> entries)
        {
            const auto entity = World.CreateEntity(name);
            World.Registry().emplace<TransformComponent>(entity);
            World.Registry().emplace<ScriptsComponent>(entity, ScriptsComponent{std::move(entries)});
            return entity;
        }

        void Tick(const float dt)
        {
            Host->Tick(World, dt);
            for (auto& write : std::exchange(Queue, {})) write(World);
        }

        glm::vec3 PositionOf(const entt::entity entity) { return World.Registry().get<TransformComponent>(entity).Position; }
    };
}

TEST_CASE("The host runs OnStart then OnTick with props, entity and queued scene writes", "[scripting][host]")
{
    ScriptDir dir("host");
    dir.Write("orbiter", kOrbiter);
    HostRig rig(dir);
    const auto entity = rig.Spawn("Mover", {ScriptEntry{"orbiter", {{"Speed", 4.0f}}}});
    rig.Host->Start(rig.World);
    rig.Tick(0.5f);
    rig.Tick(0.5f);
    CHECK(rig.PositionOf(entity).x == Approx(4.0f));
    CHECK(rig.Host->TakeErrors().empty());
    CHECK(rig.Host->InstanceCount() == 1);

    rig.World.Registry().get<ScriptsComponent>(entity).Entries[0].Props["Speed"] = 8.0f;
    rig.Tick(0.5f);
    CHECK(rig.PositionOf(entity).x == Approx(8.0f));
}

TEST_CASE("Two entities and two script entries get independent state", "[scripting][host]")
{
    ScriptDir dir("host_multi");
    dir.Write("orbiter", kOrbiter);
    HostRig rig(dir);
    const auto a = rig.Spawn("A", {ScriptEntry{"orbiter", {{"Speed", 1.0f}}}});
    const auto b = rig.Spawn("B", {ScriptEntry{"orbiter", {{"Speed", 3.0f}}}});
    for (int i = 0; i < 4; ++i) rig.Tick(0.25f);
    CHECK(rig.PositionOf(a).x == Approx(1.0f));
    CHECK(rig.PositionOf(b).x == Approx(3.0f));
    CHECK(rig.Host->InstanceCount() == 2);
    rig.World.DestroyEntity(b);
    rig.Tick(0.25f);
    CHECK(rig.Host->InstanceCount() == 1);
}

TEST_CASE("OnStart may wait and OnTick may spawn tasks on the instance scheduler", "[scripting][host]")
{
    ScriptDir dir("host_tasks");
    dir.Write("latent", R"(
return {
    OnStart = function(self)
        task.wait(1)
        scene.set(self.entity, "Transform", "Position", vector.create(1, 0, 0))
        task.wait_event("go")
        scene.set(self.entity, "Transform", "Position", vector.create(2, 0, 0))
    end,
    OnTick = function(self, dt)
        if not self.fired then
            self.fired = true
            task.spawn(function() task.wait(0.5) scene.set(self.entity, "Transform", "Scale", vector.create(5, 5, 5)) end)
        end
    end,
})");
    HostRig rig(dir);
    const auto entity = rig.Spawn("L", {ScriptEntry{"latent", {}}});
    rig.Host->Start(rig.World);
    rig.Tick(0.25f);
    rig.Tick(0.25f);
    CHECK(rig.World.Registry().get<TransformComponent>(entity).Scale.x == Approx(1.0f));
    rig.Tick(0.25f);
    CHECK(rig.World.Registry().get<TransformComponent>(entity).Scale.x == Approx(5.0f));
    CHECK(rig.PositionOf(entity).x == Approx(0.0f));
    rig.Tick(0.25f);
    CHECK(rig.PositionOf(entity).x == Approx(1.0f));
    CHECK(rig.Host->TakeErrors().empty());
}

TEST_CASE("Script errors are reported once and shown in the status until the script recovers", "[scripting][host]")
{
    ScriptDir dir("host_errors");
    dir.Write("boom", "return { OnTick = function(self, dt) local x = nil; return x.y end }");
    dir.Write("missing_ok", "return { OnTick = function() end }");
    HostRig rig(dir);
    rig.Spawn("Bad", {ScriptEntry{"boom", {}}, ScriptEntry{"does_not_exist", {}}, ScriptEntry{"missing_ok", {}}});
    rig.Host->Start(rig.World);
    for (int i = 0; i < 3; ++i) rig.Tick(0.1f);
    const auto errors = rig.Host->TakeErrors();
    REQUIRE(errors.size() == 2);
    CHECK(errors[0].Script == "does_not_exist");
    CHECK(errors[0].Message.find("not found") != std::string::npos);
    CHECK(errors[1].Script == "boom");
    CHECK(errors[1].Line == 1);
    const auto statuses = rig.Host->Statuses();
    REQUIRE(statuses.size() == 3);
    CHECK_FALSE(statuses[0].Message.empty());
    CHECK_FALSE(statuses[1].Message.empty());
    CHECK(statuses[2].Message.empty());
}

TEST_CASE("Entries named by the ignore predicate are left to someone else", "[scripting][host]")
{
    ScriptDir dir("host_ignore");
    HostRig rig(dir);
    rig.Host->SetIgnored([](const std::string& name) { return name == "GraphThing"; });
    rig.Spawn("G", {ScriptEntry{"GraphThing", {}}});
    rig.Host->Start(rig.World);
    rig.Tick(0.1f);
    CHECK(rig.Host->InstanceCount() == 0);
    CHECK(rig.Host->TakeErrors().empty());
}

TEST_CASE("Hot reload rebuilds the script, keeps self and props, and restarts it", "[scripting][host][hotreload]")
{
    ScriptDir dir("host_reload");
    dir.Write("s", R"(return {
        OnStart = function(self) self.count = (self.count or 0) + 1 end,
        OnTick = function(self, dt) scene.set(self.entity, "Transform", "Position", vector.create(self.props.Speed, self.count, 1)) end,
        properties = { { name = "Speed", type = "float", default = 1 } },
    })");
    HostRig rig(dir);
    const auto entity = rig.Spawn("R", {ScriptEntry{"s", {{"Speed", 6.0f}}}});
    rig.Host->Start(rig.World);
    rig.Tick(0.1f);
    CHECK(rig.PositionOf(entity) == glm::vec3(6.0f, 1.0f, 1.0f));

    dir.Write("s", R"(return {
        OnStart = function(self) self.count = (self.count or 0) + 1 end,
        OnTick = function(self, dt) scene.set(self.entity, "Transform", "Position", vector.create(self.props.Speed, self.count, 2)) end,
        properties = { { name = "Speed", type = "float", default = 1 } },
    })", 5);
    rig.Assets->Poll();
    rig.Tick(0.1f);
    CHECK(rig.PositionOf(entity) == glm::vec3(6.0f, 2.0f, 2.0f));
    CHECK(rig.World.Registry().get<ScriptsComponent>(entity).Entries[0].Props.at("Speed") == FieldValue{6.0f});

    dir.Write("s", "return { OnTick = function( }", 10);
    rig.Assets->Poll();
    rig.Tick(0.1f);
    CHECK(rig.PositionOf(entity) == glm::vec3(6.0f, 2.0f, 2.0f));
    const auto errors = rig.Host->TakeErrors();
    CHECK_FALSE(errors.empty());
}

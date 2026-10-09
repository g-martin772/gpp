#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <yaml-cpp/yaml.h>

import GPP;
import std;

using namespace GPP;

namespace
{
    enum class Mode : std::uint8_t { Idle, Active };

    struct ReflectedComponent
    {
        float Gain{2.0f};
        int Count{3};
        bool Enabled{true};
        glm::vec3 Tint{1.0f, 0.0f, 0.0f};
        glm::quat Orientation{1.0f, 0.0f, 0.0f, 0.0f};
        std::string Label;
        std::uint64_t Target{0};
        Mode State{Mode::Idle};
    };

    void RegisterReflected()
    {
        static std::once_flag flag;
        std::call_once(flag, []
        {
            RegisterComponent<ReflectedComponent>("Reflected", ComponentDescription<ReflectedComponent>{
                .DisplayName = "Reflected Thing",
                .Fields = {
                    Field("Gain", &ReflectedComponent::Gain, {.Min = 0.0f, .Max = 10.0f}),
                    Field("Count", &ReflectedComponent::Count),
                    Field("Enabled", &ReflectedComponent::Enabled),
                    Field("Tint", &ReflectedComponent::Tint, {.Kind = FieldKind::Color}),
                    Field("Orientation", &ReflectedComponent::Orientation, {.Kind = FieldKind::Angle}),
                    Field("Label", &ReflectedComponent::Label),
                    Field("Target", &ReflectedComponent::Target, {.Kind = FieldKind::EntityRef}),
                    Field("State", &ReflectedComponent::State, {.Options = {"Idle", "Active"}}),
                },
                .Defaults = {.Gain = 5.0f}});
        });
    }
}

TEST_CASE ("Described component round-trips through scene YAML", "[simulation][reflection]")
{
    RegisterReflected();

    Scene scene("Reflection");
    const auto entity = scene.CreateEntity("Thing");
    scene.Registry().emplace<ReflectedComponent>(
        entity, ReflectedComponent{.Gain = 7.0f, .Count = 9, .Enabled = false, .Tint = {0.1f, 0.2f, 0.3f},
                                   .Label = "hello", .Target = 42, .State = Mode::Active});

    const auto yaml = scene.SerializeToYaml();
    CHECK(yaml.find("State: Active") != std::string::npos);

    Scene loaded;
    loaded.DeserializeFromYaml(yaml);
    const auto& c = loaded.Registry().get<ReflectedComponent>(loaded.FindByGuid(scene.GuidOf(entity)));
    CHECK(c.Gain == 7.0f);
    CHECK(c.Count == 9);
    CHECK_FALSE(c.Enabled);
    CHECK(c.Tint == glm::vec3(0.1f, 0.2f, 0.3f));
    CHECK(c.Label == "hello");
    CHECK(c.Target == 42);
    CHECK(c.State == Mode::Active);
}

TEST_CASE ("Described component exposes runtime fields and an add factory", "[simulation][reflection]")
{
    RegisterReflected();

    const auto* info = ComponentRegistry::Instance().FindByName("Reflected");
    REQUIRE(info != nullptr);
    CHECK(info->DisplayName == "Reflected Thing");
    REQUIRE(info->Fields.size() == 8);
    CHECK(info->Fields[0].Type == FieldType::Float);
    CHECK(info->Fields[3].Meta.Kind == FieldKind::Color);
    CHECK(info->Fields[4].Type == FieldType::Vec3);
    CHECK(info->Fields[6].Type == FieldType::Entity);
    CHECK(info->Fields[7].Type == FieldType::Enum);

    Scene scene("Runtime");
    const auto entity = scene.CreateEntity("Thing");
    CHECK(std::holds_alternative<std::monostate>(info->Fields[0].Get(scene.Registry(), entity)));

    info->Add(scene.Registry(), entity);
    CHECK(std::get<float>(info->Fields[0].Get(scene.Registry(), entity)) == 5.0f);

    CHECK(info->Fields[0].Set(scene.Registry(), entity, FieldValue{8.0f}));
    CHECK(info->Fields[1].Set(scene.Registry(), entity, FieldValue{4.5f}));
    CHECK(info->Fields[7].Set(scene.Registry(), entity, FieldValue{1}));
    CHECK_FALSE(info->Fields[5].Set(scene.Registry(), entity, FieldValue{1.0f}));
    const auto& c = scene.Registry().get<ReflectedComponent>(entity);
    CHECK(c.Gain == 8.0f);
    CHECK(c.Count == 4);
    CHECK(c.State == Mode::Active);
}

TEST_CASE ("Base components are described", "[simulation][reflection]")
{
    RegisterBaseComponents();

    const auto* transform = ComponentRegistry::Instance().FindByName("Transform");
    REQUIRE(transform != nullptr);
    CHECK(transform->Fields.size() == 3);

    Scene scene("Base");
    const auto entity = scene.CreateEntity("T");
    transform->Add(scene.Registry(), entity);
    CHECK(transform->Fields[1].Set(scene.Registry(), entity, FieldValue{glm::vec3(0.0f, 90.0f, 0.0f)}));
    const auto euler = std::get<glm::vec3>(transform->Fields[1].Get(scene.Registry(), entity));
    CHECK(euler.y == Catch::Approx(90.0f).margin(0.1));
}

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

import GPP;
import std;

using namespace GPP;
using Catch::Approx;

namespace
{
    entt::entity AddLight(Scene& scene, const std::string& name, const glm::dvec3& position = {0.0, 0.0, 0.0},
                          const glm::quat& rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f))
    {
        const auto entity = scene.CreateEntity(name);
        scene.Registry().emplace_or_replace<TransformComponent>(entity, TransformComponent{.Position = position,
                                                                                            .Rotation = rotation});
        return entity;
    }
}

TEST_CASE("Light components register with descriptors", "[lights][reflection]")
{
    RegisterBaseComponents();
    for (const char* name : {"DirectionalLight", "PointLight", "SpotLight", "AreaLight"})
    {
        const auto* info = ComponentRegistry::Instance().FindByName(name);
        REQUIRE(info != nullptr);
        CHECK(info->Inspectable);
        CHECK(info->GraphExposed);
        CHECK(info->Serializable);
        CHECK(info->Fields.size() >= 6);
    }

    const auto* sun = ComponentRegistry::Instance().FindByName("DirectionalLight");
    const auto irradiance = std::ranges::find(sun->Fields, "Irradiance", &FieldInfo::Name);
    REQUIRE(irradiance != sun->Fields.end());
    CHECK(irradiance->Meta.Dim == Units::Dimension::Irradiance);

    const auto* spot = ComponentRegistry::Instance().FindByName("SpotLight");
    const auto intensity = std::ranges::find(spot->Fields, "Intensity", &FieldInfo::Name);
    REQUIRE(intensity != spot->Fields.end());
    CHECK(intensity->Meta.Dim == Units::Dimension::RadiantIntensity);
}

TEST_CASE("Lights round-trip through scene YAML with SI storage", "[lights][yaml]")
{
    RegisterBaseComponents();
    Scene scene("Lights");
    const auto sun = AddLight(scene, "Sun");
    scene.Registry().emplace<DirectionalLightComponent>(
        sun, DirectionalLightComponent{.Irradiance = 1361.0, .ColorMode = LightColorMode::Blackbody,
                                       .Temperature = 5778.0, .AngularDiameter = 0.533f});
    const auto lamp = AddLight(scene, "Lamp");
    scene.Registry().emplace<SpotLightComponent>(
        lamp, SpotLightComponent{.Mode = PunctualMagnitude::Power, .Power = 40.0, .Radius = 0.02, .InnerAngle = 10.0f,
                                 .OuterAngle = 25.0f, .Color = {1.0f, 0.5f, 0.25f}});
    const auto panel = AddLight(scene, "Panel");
    scene.Registry().emplace<AreaLightComponent>(
        panel, AreaLightComponent{.Shape = AreaShape::Disk, .Radius = 0.3, .Mode = AreaMagnitude::Power, .Power = 12.0,
                                  .TwoSided = true});
    const auto bulb = AddLight(scene, "Bulb");
    scene.Registry().emplace<PointLightComponent>(bulb, PointLightComponent{.Intensity = 7.5, .Radius = 0.05});

    const auto yaml = scene.SerializeToYaml();
    CHECK(yaml.find("ColorMode: Blackbody") != std::string::npos);
    CHECK(yaml.find("Shape: Disk") != std::string::npos);

    Scene loaded;
    loaded.DeserializeFromYaml(yaml);
    const auto& registry = loaded.Registry();

    const auto& s = registry.get<DirectionalLightComponent>(loaded.FindByGuid(scene.GuidOf(sun)));
    CHECK(s.Irradiance == 1361.0);
    CHECK(s.ColorMode == LightColorMode::Blackbody);
    CHECK(s.Temperature == 5778.0);
    CHECK(s.AngularDiameter == Approx(0.533f));

    const auto& l = registry.get<SpotLightComponent>(loaded.FindByGuid(scene.GuidOf(lamp)));
    CHECK(l.Mode == PunctualMagnitude::Power);
    CHECK(l.Power == 40.0);
    CHECK(l.Radius == 0.02);
    CHECK(l.InnerAngle == 10.0f);
    CHECK(l.OuterAngle == 25.0f);
    CHECK(l.Color == glm::vec3(1.0f, 0.5f, 0.25f));

    const auto& a = registry.get<AreaLightComponent>(loaded.FindByGuid(scene.GuidOf(panel)));
    CHECK(a.Shape == AreaShape::Disk);
    CHECK(a.Radius == 0.3);
    CHECK(a.Mode == AreaMagnitude::Power);
    CHECK(a.TwoSided);

    const auto& p = registry.get<PointLightComponent>(loaded.FindByGuid(scene.GuidOf(bulb)));
    CHECK(p.Intensity == 7.5);
    CHECK(p.Radius == 0.05);
}

TEST_CASE("Light fields accept unit-tagged YAML", "[lights][yaml][units]")
{
    RegisterBaseComponents();
    const auto* info = ComponentRegistry::Instance().FindByName("DirectionalLight");
    REQUIRE(info != nullptr);
    entt::registry registry;
    const auto entity = registry.create();
    info->Decode(registry, entity, YAML::Load("Irradiance: 128 kW/m^2\nTemperature: 6500 K\n"));
    const auto& light = registry.get<DirectionalLightComponent>(entity);
    CHECK(light.Irradiance == Approx(128000.0));

    info->Decode(registry, entity, YAML::Load("Irradiance: {value: 127000, unit: lx}\n"));
    CHECK(registry.get<DirectionalLightComponent>(entity).Irradiance == Approx(127000.0 / 683.0));
}

TEST_CASE("Light magnitudes convert between power and intensity or radiance", "[lights]")
{
    constexpr double pi = std::numbers::pi;
    PointLightComponent point{.Mode = PunctualMagnitude::Power, .Power = 4.0 * pi * 10.0};
    CHECK(PointLightIntensity(point) == Approx(10.0));
    point.Mode = PunctualMagnitude::Intensity;
    point.Intensity = 3.0;
    CHECK(PointLightIntensity(point) == Approx(3.0));

    SpotLightComponent spot{.Mode = PunctualMagnitude::Power, .Power = 1.0, .OuterAngle = 60.0f};
    CHECK(SpotLightIntensity(spot) == Approx(1.0 / (2.0 * pi * 0.5)).epsilon(1e-6));

    AreaLightComponent disk{.Shape = AreaShape::Disk, .Radius = 2.0, .Mode = AreaMagnitude::Power, .Power = 8.0 * pi * pi};
    CHECK(AreaLightArea(disk) == Approx(4.0 * pi));
    CHECK(AreaLightRadiance(disk) == Approx(2.0).epsilon(1e-9));
    CHECK(AreaLightPower(disk) == Approx(disk.Power));
    disk.TwoSided = true;
    CHECK(AreaLightRadiance(disk) == Approx(1.0).epsilon(1e-9));
    CHECK(AreaLightPower(disk) == Approx(disk.Power));

    AreaLightComponent rect{.Shape = AreaShape::Rect, .Width = 2.0, .Height = 3.0, .Radiance = 5.0};
    CHECK(AreaLightPower(rect) == Approx(pi * 5.0 * 6.0));
    AreaLightComponent sphere{.Shape = AreaShape::Sphere, .Radius = 1.0, .Radiance = 2.0, .TwoSided = true};
    CHECK(AreaLightPower(sphere) == Approx(pi * 2.0 * 4.0 * pi));
}

TEST_CASE("Light buffer is deterministic, capped and tracks transforms", "[lights][buffer]")
{
    RegisterBaseComponents();
    Scene scene("Buffer");
    const glm::quat facingPlusX = glm::quat(glm::vec3(0.0f, glm::radians(-90.0f), 0.0f));

    const auto lamp = AddLight(scene, "Lamp", {1.0e11, 2.0, 3.0});
    scene.Registry().emplace<PointLightComponent>(lamp, PointLightComponent{.Intensity = 50.0, .Radius = 0.1});
    const auto sun = AddLight(scene, "Sun", {}, facingPlusX);
    scene.Registry().emplace<DirectionalLightComponent>(sun, DirectionalLightComponent{.Irradiance = 1000.0});
    const auto off = AddLight(scene, "Off");
    scene.Registry().emplace<PointLightComponent>(off, PointLightComponent{.Enabled = false});
    const auto panel = AddLight(scene, "Panel");
    scene.Registry().emplace<AreaLightComponent>(
        panel, AreaLightComponent{.Shape = AreaShape::Rect, .Width = 2.0, .Height = 4.0, .Radiance = 10.0, .TwoSided = true});

    const auto buffer = BuildLightBuffer(scene, {.OriginM = {1.0e11, 0.0, 0.0}});
    REQUIRE(buffer.Count == 3);
    CHECK(buffer.Dropped == 0);
    CHECK(buffer.DirectionalCount == 1);

    CHECK(buffer.Lights[0].Kind == static_cast<std::uint32_t>(LightKind::Directional));
    CHECK(buffer.Lights[0].Color.x == Approx(1000.0f));
    CHECK(buffer.Lights[0].Direction.x == Approx(1.0f).margin(1e-6));
    CHECK(buffer.Lights[0].Direction.z == Approx(0.0f).margin(1e-6));
    CHECK(buffer.Lights[0].CosOuter == Approx(std::cos(glm::radians(0.533f * 0.5f))));

    CHECK(buffer.Lights[1].Kind == static_cast<std::uint32_t>(LightKind::Point));
    CHECK(buffer.Lights[1].Position == glm::vec3(0.0f, 2.0f, 3.0f));
    CHECK(buffer.Lights[1].Color == glm::vec3(50.0f));
    CHECK(buffer.Lights[1].Radius == Approx(0.1f));
    CHECK(buffer.Lights[1].Flux == Approx(50.0f * 4.0f * std::numbers::pi_v<float>));

    CHECK(buffer.Lights[2].Kind == static_cast<std::uint32_t>(LightKind::Rect));
    CHECK((buffer.Lights[2].Flags & kLightFlagTwoSided) != 0);
    CHECK(buffer.Lights[2].HalfExtent == glm::vec2(1.0f, 2.0f));
    CHECK(buffer.Lights[2].Flux == Approx(std::numbers::pi_v<float> * 10.0f * 8.0f * 2.0f));
    CHECK(buffer.UsedBytes() == 16 + 3 * sizeof(LightGpu));

    const auto again = BuildLightBuffer(scene, {.OriginM = {1.0e11, 0.0, 0.0}});
    CHECK(std::memcmp(&buffer, &again, sizeof(LightsGpu)) == 0);
}

TEST_CASE("Light buffer enforces the hard cap and keeps suns first", "[lights][buffer]")
{
    RegisterBaseComponents();
    Scene scene("Cap");
    for (std::uint32_t i = 0; i < kMaxLights + 10; ++i)
    {
        const auto e = AddLight(scene, "P" + std::to_string(i));
        scene.Registry().emplace<PointLightComponent>(e, PointLightComponent{.Intensity = 1.0 + i});
    }
    const auto sun = AddLight(scene, "Sun");
    scene.Registry().emplace<DirectionalLightComponent>(sun, DirectionalLightComponent{});

    const auto buffer = BuildLightBuffer(scene);
    CHECK(buffer.Count == kMaxLights);
    CHECK(buffer.Dropped == 11);
    CHECK(buffer.DirectionalCount == 1);
    CHECK(buffer.Lights[0].Kind == static_cast<std::uint32_t>(LightKind::Directional));
    CHECK(buffer.Lights[0].Color.x == Approx(static_cast<float>(Units::kSolarConstant)));

    const auto sorted = std::ranges::is_sorted(std::span(buffer.Lights).first(buffer.Count), {}, &LightGpu::Kind);
    CHECK(sorted);

    CHECK(BuildLightBuffer(Scene("Empty")).Count == 0);
}

TEST_CASE("Blackbody lights multiply chroma by their magnitude", "[lights]")
{
    RegisterBaseComponents();
    Scene scene("Chroma");
    const auto e = AddLight(scene, "Star");
    scene.Registry().emplace<PointLightComponent>(
        e, PointLightComponent{.Intensity = 20.0, .ColorMode = LightColorMode::Blackbody, .Temperature = 2700.0});
    const auto buffer = BuildLightBuffer(scene);
    REQUIRE(buffer.Count == 1);
    const auto expected = Radiometry::BlackbodyColor(2700.0) * 20.0f;
    CHECK(buffer.Lights[0].Color.x == Approx(expected.x));
    CHECK(buffer.Lights[0].Color.z == Approx(expected.z));
    CHECK(Radiometry::RelativeLuminance(buffer.Lights[0].Color) == Approx(20.0f).epsilon(1e-5));
}

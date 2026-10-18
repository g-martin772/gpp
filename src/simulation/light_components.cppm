module;
#include <glm/glm.hpp>
export module GPP.Simulation:LightComponents;

import std;
import GPP.Core;
import :Reflection;
import :ComponentRegistry;

export namespace GPP
{
    enum class LightColorMode : std::uint8_t { Rgb, Blackbody };
    enum class PunctualMagnitude : std::uint8_t { Intensity, Power };
    enum class AreaShape : std::uint8_t { Disk, Rect, Sphere };
    enum class AreaMagnitude : std::uint8_t { Radiance, Power };

    // Emission axis is the entity's local -Z. E_perp: irradiance on a surface facing the light.
    struct DirectionalLightComponent
    {
        bool Enabled{true};
        double Irradiance{Units::kSolarConstant}; // W/m^2
        glm::vec3 Color{1.0f};
        LightColorMode ColorMode{LightColorMode::Rgb};
        double Temperature{Units::kSolarEffectiveTemperature}; // K
        float AngularDiameter{0.533f};                          // deg
    };

    struct PointLightComponent
    {
        bool Enabled{true};
        PunctualMagnitude Mode{PunctualMagnitude::Intensity};
        double Intensity{100.0}; // W/sr
        double Power{1000.0};    // W
        double Radius{0.0};      // m
        glm::vec3 Color{1.0f};
        LightColorMode ColorMode{LightColorMode::Rgb};
        double Temperature{2700.0}; // K
    };

    struct SpotLightComponent
    {
        bool Enabled{true};
        PunctualMagnitude Mode{PunctualMagnitude::Intensity};
        double Intensity{100.0}; // W/sr
        double Power{1000.0};    // W
        double Radius{0.0};      // m
        float InnerAngle{30.0f}; // deg, half angle
        float OuterAngle{45.0f}; // deg, half angle
        glm::vec3 Color{1.0f};
        LightColorMode ColorMode{LightColorMode::Rgb};
        double Temperature{2700.0}; // K
    };

    struct AreaLightComponent
    {
        bool Enabled{true};
        AreaShape Shape{AreaShape::Rect};
        double Width{1.0};   // m, Rect
        double Height{1.0};  // m, Rect
        double Radius{0.5};  // m, Disk and Sphere
        AreaMagnitude Mode{AreaMagnitude::Radiance};
        double Radiance{100.0}; // W/m^2/sr
        double Power{1000.0};   // W
        bool TwoSided{false};
        glm::vec3 Color{1.0f};
        LightColorMode ColorMode{LightColorMode::Rgb};
        double Temperature{4000.0}; // K
    };

    inline void RegisterLightComponents()
    {
        static std::once_flag flag;
        std::call_once(flag, []
        {
            using Units::Dimension;
            const FieldMeta temperature{.Min = Radiometry::kBlackbodyMinKelvin, .Max = Radiometry::kBlackbodyMaxKelvin,
                                        .Speed = 50.0, .VisibleField = "ColorMode", .VisibleMask = 0b10,
                                        .Dim = Dimension::Temperature};
            const FieldMeta rgb{.Kind = FieldKind::Color, .VisibleField = "ColorMode", .VisibleMask = 0b01};
            const FieldMeta colorMode{.Label = "Color Mode", .Options = {"Rgb", "Blackbody"}};

            RegisterComponent<DirectionalLightComponent>("DirectionalLight", ComponentDescription<DirectionalLightComponent>{
                .DisplayName = "Directional Light",
                .Fields = {
                    Field("Enabled", &DirectionalLightComponent::Enabled),
                    Field("Irradiance", &DirectionalLightComponent::Irradiance,
                          {.Min = 0.0, .Speed = 10.0, .Dim = Dimension::Irradiance}),
                    Field("ColorMode", &DirectionalLightComponent::ColorMode, colorMode),
                    Field("Color", &DirectionalLightComponent::Color, rgb),
                    Field("Temperature", &DirectionalLightComponent::Temperature, temperature),
                    Field("AngularDiameter", &DirectionalLightComponent::AngularDiameter,
                          {.Label = "Angular Diameter (deg)", .Min = 0.0f, .Max = 90.0f, .Speed = 0.01f}),
                }});

            RegisterComponent<PointLightComponent>("PointLight", ComponentDescription<PointLightComponent>{
                .DisplayName = "Point Light",
                .Fields = {
                    Field("Enabled", &PointLightComponent::Enabled),
                    Field("Mode", &PointLightComponent::Mode, {.Options = {"Intensity", "Power"}}),
                    Field("Intensity", &PointLightComponent::Intensity,
                          {.Min = 0.0, .Speed = 1.0, .VisibleField = "Mode", .VisibleMask = 0b01,
                           .Dim = Dimension::RadiantIntensity}),
                    Field("Power", &PointLightComponent::Power,
                          {.Min = 0.0, .Speed = 1.0, .VisibleField = "Mode", .VisibleMask = 0b10,
                           .Dim = Dimension::Power}),
                    Field("Radius", &PointLightComponent::Radius,
                          {.Min = 0.0, .Speed = 0.01, .Dim = Dimension::Length}),
                    Field("ColorMode", &PointLightComponent::ColorMode, colorMode),
                    Field("Color", &PointLightComponent::Color, rgb),
                    Field("Temperature", &PointLightComponent::Temperature, temperature),
                }});

            RegisterComponent<SpotLightComponent>("SpotLight", ComponentDescription<SpotLightComponent>{
                .DisplayName = "Spot Light",
                .Fields = {
                    Field("Enabled", &SpotLightComponent::Enabled),
                    Field("Mode", &SpotLightComponent::Mode, {.Options = {"Intensity", "Power"}}),
                    Field("Intensity", &SpotLightComponent::Intensity,
                          {.Min = 0.0, .Speed = 1.0, .VisibleField = "Mode", .VisibleMask = 0b01,
                           .Dim = Dimension::RadiantIntensity}),
                    Field("Power", &SpotLightComponent::Power,
                          {.Min = 0.0, .Speed = 1.0, .VisibleField = "Mode", .VisibleMask = 0b10,
                           .Dim = Dimension::Power}),
                    Field("Radius", &SpotLightComponent::Radius,
                          {.Min = 0.0, .Speed = 0.01, .Dim = Dimension::Length}),
                    Field("InnerAngle", &SpotLightComponent::InnerAngle,
                          {.Label = "Inner Angle (deg)", .Min = 0.0f, .Max = 89.0f, .Speed = 0.1f}),
                    Field("OuterAngle", &SpotLightComponent::OuterAngle,
                          {.Label = "Outer Angle (deg)", .Min = 0.1f, .Max = 89.9f, .Speed = 0.1f}),
                    Field("ColorMode", &SpotLightComponent::ColorMode, colorMode),
                    Field("Color", &SpotLightComponent::Color, rgb),
                    Field("Temperature", &SpotLightComponent::Temperature, temperature),
                }});

            RegisterComponent<AreaLightComponent>("AreaLight", ComponentDescription<AreaLightComponent>{
                .DisplayName = "Area Light",
                .Fields = {
                    Field("Enabled", &AreaLightComponent::Enabled),
                    Field("Shape", &AreaLightComponent::Shape, {.Options = {"Disk", "Rect", "Sphere"}}),
                    Field("Width", &AreaLightComponent::Width,
                          {.Min = 1e-6, .Speed = 0.01, .VisibleField = "Shape", .VisibleMask = 0b010,
                           .Dim = Dimension::Length}),
                    Field("Height", &AreaLightComponent::Height,
                          {.Min = 1e-6, .Speed = 0.01, .VisibleField = "Shape", .VisibleMask = 0b010,
                           .Dim = Dimension::Length}),
                    Field("Radius", &AreaLightComponent::Radius,
                          {.Min = 1e-6, .Speed = 0.01, .VisibleField = "Shape", .VisibleMask = 0b101,
                           .Dim = Dimension::Length}),
                    Field("Mode", &AreaLightComponent::Mode, {.Options = {"Radiance", "Power"}}),
                    Field("Radiance", &AreaLightComponent::Radiance,
                          {.Min = 0.0, .Speed = 1.0, .VisibleField = "Mode", .VisibleMask = 0b01,
                           .Dim = Dimension::Radiance}),
                    Field("Power", &AreaLightComponent::Power,
                          {.Min = 0.0, .Speed = 1.0, .VisibleField = "Mode", .VisibleMask = 0b10,
                           .Dim = Dimension::Power}),
                    Field("TwoSided", &AreaLightComponent::TwoSided, {.Label = "Two Sided"}),
                    Field("ColorMode", &AreaLightComponent::ColorMode, colorMode),
                    Field("Color", &AreaLightComponent::Color, rgb),
                    Field("Temperature", &AreaLightComponent::Temperature, temperature),
                }});
        });
    }
}

module;
#include <cstddef>
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
export module GPP.Simulation:Lights;

import std;
import GPP.Core;
import :Ecs;
import :Components;
import :LightComponents;
import :Scene;

export namespace GPP
{
    inline constexpr std::uint32_t kMaxLights = 64;
    inline constexpr std::uint32_t kLightFlagTwoSided = 1u << 0;

    enum class LightKind : std::uint32_t { Directional = 0, Point = 1, Spot = 2, Disk = 3, Rect = 4, Sphere = 5 };

    // std430, 80 bytes. Position in metres relative to the build origin. Direction is the propagation direction
    // of the emitted light (local -Z); area lights emit into the hemisphere around it. Color is the radiometric RGB
    // magnitude: E_perp [W/m^2] (Directional), I [W/sr] (Point, Spot), L [W/m^2/sr] (Disk, Rect, Sphere).
    // HalfExtent: Directional angular radius [rad]; Disk (x) radius [m]; Rect half width/height [m].
    // Flux [W] is the emitted power (0 for Directional). CosOuter/CosInner are the spot cone half-angle cosines.
    struct LightGpu
    {
        glm::vec3 Position{0.0f};
        float Radius{0.0f};
        glm::vec3 Direction{0.0f, 0.0f, -1.0f};
        std::uint32_t Kind{0};
        glm::vec3 Color{0.0f};
        float Flux{0.0f};
        glm::vec3 Tangent{1.0f, 0.0f, 0.0f};
        std::uint32_t Flags{0};
        glm::vec2 HalfExtent{0.0f};
        float CosOuter{-1.0f};
        float CosInner{-1.0f};
    };
    static_assert(sizeof(LightGpu) == 80);
    static_assert(offsetof(LightGpu, Direction) == 16);
    static_assert(offsetof(LightGpu, Color) == 32);
    static_assert(offsetof(LightGpu, Tangent) == 48);
    static_assert(offsetof(LightGpu, HalfExtent) == 64);

    // Lights[0..Count) are valid; Dropped counts lights beyond kMaxLights that did not fit.
    struct LightsGpu
    {
        std::uint32_t Count{0};
        std::uint32_t Dropped{0};
        std::uint32_t DirectionalCount{0};
        std::uint32_t Pad{0};
        std::array<LightGpu, kMaxLights> Lights{};

        [[nodiscard]] constexpr std::size_t UsedBytes() const noexcept
        {
            return offsetof(LightsGpu, Lights) + static_cast<std::size_t>(Count) * sizeof(LightGpu);
        }
    };
    static_assert(offsetof(LightsGpu, Lights) == 16);
    static_assert(sizeof(LightsGpu) == 16 + kMaxLights * 80);

    struct LightBuildOptions
    {
        glm::dvec3 OriginM{0.0};
    };

    [[nodiscard]] inline glm::vec3 LightChroma(const LightColorMode mode, const glm::vec3& color, const double kelvin)
    {
        return mode == LightColorMode::Blackbody ? Radiometry::BlackbodyColor(kelvin) : color;
    }

    [[nodiscard]] inline double PointLightIntensity(const PointLightComponent& light)
    {
        constexpr double fourPi = 4.0 * static_cast<double>(Units::Pi());
        return light.Mode == PunctualMagnitude::Power ? light.Power / fourPi : light.Intensity;
    }

    [[nodiscard]] inline double SpotLightIntensity(const SpotLightComponent& light)
    {
        if (light.Mode != PunctualMagnitude::Power) return light.Intensity;
        const double cone = 2.0 * static_cast<double>(Units::Pi())
                            * (1.0 - std::cos(glm::radians(static_cast<double>(light.OuterAngle))));
        return light.Power / cone;
    }

    [[nodiscard]] inline double AreaLightArea(const AreaLightComponent& light)
    {
        const double pi = static_cast<double>(Units::Pi());
        switch (light.Shape)
        {
        case AreaShape::Disk: return pi * light.Radius * light.Radius;
        case AreaShape::Rect: return light.Width * light.Height;
        case AreaShape::Sphere: return 4.0 * pi * light.Radius * light.Radius;
        }
        return 0.0;
    }

    // Lambertian emitter: Phi = pi L A per emitting side (two sides double it, a sphere has one).
    [[nodiscard]] inline double AreaLightSides(const AreaLightComponent& light)
    {
        return light.TwoSided && light.Shape != AreaShape::Sphere ? 2.0 : 1.0;
    }

    [[nodiscard]] inline double AreaLightRadiance(const AreaLightComponent& light)
    {
        if (light.Mode != AreaMagnitude::Power) return light.Radiance;
        const double area = AreaLightArea(light);
        return area > 0.0
                   ? light.Power / (static_cast<double>(Units::Pi()) * area * AreaLightSides(light))
                   : 0.0;
    }

    [[nodiscard]] inline double AreaLightPower(const AreaLightComponent& light)
    {
        return static_cast<double>(Units::Pi()) * AreaLightRadiance(light) * AreaLightArea(light)
               * AreaLightSides(light);
    }

    [[nodiscard]] inline LightsGpu BuildLightBuffer(const Scene& scene, const LightBuildOptions& options = {})
    {
        const auto& registry = scene.Registry();
        struct Entry
        {
            std::uint32_t Kind;
            std::uint64_t Guid;
            LightGpu Light;
        };
        std::vector<Entry> entries;
        const auto placement = [&](const entt::entity entity)
        {
            const auto* transform = registry.try_get<TransformComponent>(entity);
            const TransformComponent identity;
            const auto& t = transform ? *transform : identity;
            const glm::quat rotation = glm::normalize(t.Rotation);
            struct Placement
            {
                glm::vec3 Position;
                glm::vec3 Direction;
                glm::vec3 Tangent;
            };
            return Placement{glm::vec3(t.Position - options.OriginM), rotation * glm::vec3(0.0f, 0.0f, -1.0f),
                             rotation * glm::vec3(1.0f, 0.0f, 0.0f)};
        };
        const auto push = [&](const entt::entity entity, LightGpu light)
        {
            entries.push_back({light.Kind, scene.GuidOf(entity), light});
        };
        const auto fill = [&](const entt::entity entity, const LightKind kind)
        {
            const auto p = placement(entity);
            LightGpu light;
            light.Position = p.Position;
            light.Direction = p.Direction;
            light.Tangent = p.Tangent;
            light.Kind = static_cast<std::uint32_t>(kind);
            return light;
        };

        for (const auto [entity, c] : registry.view<const DirectionalLightComponent>().each())
        {
            if (!c.Enabled || c.Irradiance <= 0.0) continue;
            auto light = fill(entity, LightKind::Directional);
            light.Color = LightChroma(c.ColorMode, c.Color, c.Temperature) * static_cast<float>(c.Irradiance);
            const float angularRadius = glm::radians(c.AngularDiameter) * 0.5f;
            light.HalfExtent = {angularRadius, 0.0f};
            light.CosOuter = std::cos(angularRadius);
            push(entity, light);
        }
        for (const auto [entity, c] : registry.view<const PointLightComponent>().each())
        {
            const double intensity = PointLightIntensity(c);
            if (!c.Enabled || intensity <= 0.0) continue;
            auto light = fill(entity, LightKind::Point);
            light.Radius = static_cast<float>(c.Radius);
            light.Color = LightChroma(c.ColorMode, c.Color, c.Temperature) * static_cast<float>(intensity);
            light.Flux = static_cast<float>(intensity * 4.0 * static_cast<double>(Units::Pi()));
            push(entity, light);
        }
        for (const auto [entity, c] : registry.view<const SpotLightComponent>().each())
        {
            const double intensity = SpotLightIntensity(c);
            if (!c.Enabled || intensity <= 0.0) continue;
            auto light = fill(entity, LightKind::Spot);
            const float outer = std::max(c.OuterAngle, 0.01f);
            const float inner = std::clamp(c.InnerAngle, 0.0f, outer);
            light.Radius = static_cast<float>(c.Radius);
            light.Color = LightChroma(c.ColorMode, c.Color, c.Temperature) * static_cast<float>(intensity);
            light.CosOuter = std::cos(glm::radians(outer));
            light.CosInner = std::cos(glm::radians(inner));
            light.Flux = static_cast<float>(intensity * 2.0 * static_cast<double>(Units::Pi())
                                            * (1.0 - static_cast<double>(light.CosOuter)));
            push(entity, light);
        }
        for (const auto [entity, c] : registry.view<const AreaLightComponent>().each())
        {
            const double radiance = AreaLightRadiance(c);
            if (!c.Enabled || radiance <= 0.0 || AreaLightArea(c) <= 0.0) continue;
            const auto kind = c.Shape == AreaShape::Disk ? LightKind::Disk
                              : c.Shape == AreaShape::Rect ? LightKind::Rect : LightKind::Sphere;
            auto light = fill(entity, kind);
            light.Color = LightChroma(c.ColorMode, c.Color, c.Temperature) * static_cast<float>(radiance);
            light.Flux = static_cast<float>(AreaLightPower(c));
            if (c.TwoSided && c.Shape != AreaShape::Sphere) light.Flags |= kLightFlagTwoSided;
            if (c.Shape == AreaShape::Rect)
            {
                light.HalfExtent = {static_cast<float>(c.Width * 0.5), static_cast<float>(c.Height * 0.5)};
            }
            else
            {
                light.Radius = static_cast<float>(c.Radius);
                light.HalfExtent = {static_cast<float>(c.Radius), static_cast<float>(c.Radius)};
            }
            push(entity, light);
        }

        std::ranges::stable_sort(entries, [](const Entry& a, const Entry& b)
        {
            return std::tie(a.Kind, a.Guid) < std::tie(b.Kind, b.Guid);
        });

        LightsGpu out;
        for (const auto& entry : entries)
        {
            if (out.Count == kMaxLights)
            {
                ++out.Dropped;
                continue;
            }
            out.Lights[out.Count++] = entry.Light;
            if (entry.Kind == static_cast<std::uint32_t>(LightKind::Directional)) ++out.DirectionalCount;
        }
        return out;
    }
}

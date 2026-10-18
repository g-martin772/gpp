export module GPP.Simulation:Particles.Kernels;

import std;
import glm;
import :Particles.Types;
import :Particles.Model;

export namespace GPP::ParticleKernel
{
    constexpr float kTwoPi = 6.28318530717958647692f;

    // Neumaier-compensated sum: the rounding error of every add is kept in `comp` and the true value is sum + comp
    inline void CompensatedAdd(glm::vec3& sum, glm::vec3& comp, const glm::vec3& increment)
    {
        for (int c = 0; c < 3; ++c)
        {
            const float total = sum[c] + increment[c];
            comp[c] += std::fabs(sum[c]) >= std::fabs(increment[c]) ? (sum[c] - total) + increment[c]
                                                                    : (increment[c] - total) + sum[c];
            sum[c] = total;
        }
    }

    [[nodiscard]] inline glm::vec3 Newtonian(const glm::vec3& position, const float mu, const float softening)
    {
        const float r2 = glm::dot(position, position) + softening * softening;
        const float inverse = 1.0f / std::sqrt(r2);
        return -mu * position * (inverse * inverse * inverse);
    }

    [[nodiscard]] inline float PaczynskiWiitaDistance(const float r, const float schwarzschildRadius)
    {
        return std::max(r - schwarzschildRadius, 0.001f * schwarzschildRadius);
    }

    [[nodiscard]] inline glm::vec3 PaczynskiWiita(const glm::vec3& position, const float mu, const float schwarzschildRadius)
    {
        const float r = std::max(glm::length(position), 1e-30f);
        const float d = PaczynskiWiitaDistance(r, schwarzschildRadius);
        return -mu * position / (r * d * d);
    }

    [[nodiscard]] inline glm::vec3 Acceleration(const ParticleForceParams& force, const ParticleHooks& hooks,
                                                const glm::vec3& position, const glm::vec3& velocity)
    {
        switch (static_cast<ParticleForceKind>(force.Kind))
        {
        case ParticleForceKind::Newtonian: return Newtonian(position, force.Mu, force.Softening);
        case ParticleForceKind::PaczynskiWiita: return PaczynskiWiita(position, force.Mu, force.SchwarzschildRadius);
        case ParticleForceKind::Custom:
            return hooks.Acceleration ? hooks.Acceleration(position, velocity, force) : glm::vec3(0.0f);
        default: return glm::vec3(0.0f);
        }
    }

    [[nodiscard]] inline float Potential(const ParticleForceParams& force, const ParticleHooks& hooks,
                                         const glm::vec3& position)
    {
        switch (static_cast<ParticleForceKind>(force.Kind))
        {
        case ParticleForceKind::Newtonian:
            return -force.Mu / std::sqrt(glm::dot(position, position) + force.Softening * force.Softening);
        case ParticleForceKind::PaczynskiWiita:
            return -force.Mu / PaczynskiWiitaDistance(glm::length(position), force.SchwarzschildRadius);
        case ParticleForceKind::Custom: return hooks.Potential ? hooks.Potential(position, force) : 0.0f;
        default: return 0.0f;
        }
    }

    // Fraction eta of the shorter of the local dynamical time and the crossing time, times 2 pi
    [[nodiscard]] inline float StableStep(const ParticleForceParams& force, const ParticleHooks& hooks,
                                          const glm::vec3& position, const glm::vec3& velocity, const float eta)
    {
        const float speed = glm::length(velocity) + 1e-30f;
        switch (static_cast<ParticleForceKind>(force.Kind))
        {
        case ParticleForceKind::Newtonian:
        {
            const float r = std::sqrt(glm::dot(position, position) + force.Softening * force.Softening);
            return eta * kTwoPi * std::min(std::sqrt(r * r * r / force.Mu), r / speed);
        }
        case ParticleForceKind::PaczynskiWiita:
        {
            const float r = std::max(glm::length(position), 1e-30f);
            const float d = PaczynskiWiitaDistance(r, force.SchwarzschildRadius);
            return eta * kTwoPi * std::min(std::sqrt(r * d * d / force.Mu), d / speed);
        }
        case ParticleForceKind::Custom:
            return hooks.MaxStableStep ? hooks.MaxStableStep(position, velocity, eta, force)
                                       : std::numeric_limits<float>::infinity();
        default: return std::numeric_limits<float>::infinity();
        }
    }

    // Inside test of a collider at time `elapsed` after the start of the macro step. On a hit, `normal` points out of the
    // shape and `surface` is the closest point on its boundary.
    [[nodiscard]] inline bool Inside(const ParticleColliderParams& collider, const glm::vec3& position, const float elapsed,
                                     glm::vec3& normal, glm::vec3& surface)
    {
        const glm::vec3 centre = glm::vec3(collider.CenterRadius) + glm::vec3(collider.VelocityRestitution) * elapsed;
        const glm::vec3 offset = position - centre;
        if (collider.HalfExtentsShape.w < 0.5f)
        {
            const float radius = collider.CenterRadius.w;
            const float distance = glm::length(offset);
            if (!(distance < radius)) return false;
            normal = distance > 0.0f ? offset / distance : glm::vec3(0.0f, 1.0f, 0.0f);
            surface = centre + normal * radius;
            return true;
        }
        const glm::vec3 half(collider.HalfExtentsShape);
        const glm::vec3 axes[3] = {glm::vec3(collider.Axis0), glm::vec3(collider.Axis1), glm::vec3(collider.Axis2)};
        glm::vec3 local(glm::dot(axes[0], offset), glm::dot(axes[1], offset), glm::dot(axes[2], offset));
        if (!(std::abs(local.x) < half.x && std::abs(local.y) < half.y && std::abs(local.z) < half.z)) return false;
        const glm::vec3 penetration = half - glm::abs(local);
        int face = 0;
        if (penetration.y < penetration[face]) face = 1;
        if (penetration.z < penetration[face]) face = 2;
        const float side = local[face] < 0.0f ? -1.0f : 1.0f;
        normal = axes[face] * side;
        local[face] = side * half[face];
        surface = centre + axes[0] * local.x + axes[1] * local.y + axes[2] * local.z;
        return true;
    }

    // Projected area of a collider along a unit direction, used when the component leaves the reference area at zero
    [[nodiscard]] inline float ProjectedArea(const ParticleColliderParams& collider, const glm::vec3& direction)
    {
        if (collider.HalfExtentsShape.w < 0.5f)
        {
            return 3.14159265358979f * collider.CenterRadius.w * collider.CenterRadius.w;
        }
        const glm::vec3 half(collider.HalfExtentsShape);
        const glm::vec3 local(glm::dot(glm::vec3(collider.Axis0), direction), glm::dot(glm::vec3(collider.Axis1), direction),
                              glm::dot(glm::vec3(collider.Axis2), direction));
        return 4.0f * (half.y * half.z * std::abs(local.x) + half.x * half.z * std::abs(local.y)
                       + half.x * half.y * std::abs(local.z));
    }

    // F = -1/2 rho Cd A |v| v on the body, v = body velocity relative to the gas
    [[nodiscard]] inline glm::dvec3 DragForceOnBody(const double density, const double dragCoefficient, const double area,
                                                    const glm::dvec3& relativeVelocity)
    {
        return -0.5 * density * dragCoefficient * area * glm::length(relativeVelocity) * relativeVelocity;
    }

    struct EmitterSample
    {
        glm::vec3 Position{0.0f};
        glm::vec3 Velocity{0.0f};
        float Temperature{0.0f};
        float Weight{0.0f};
        float Age{0.0f};
        std::uint32_t Serial{0};
    };

    [[nodiscard]] inline EmitterSample SampleEmitter(const ParticleEmitterParams& emitter, const std::uint32_t index,
                                                     const float baseStep)
    {
        const std::uint32_t serial = emitter.Batch.x + index;
        const ParticleRng::Key key{emitter.Ids.x, emitter.Ids.y};
        const std::uint32_t slot = emitter.Ids.z;
        std::array<float, 12> u{};
        for (std::uint32_t round = 0; round < 3; ++round)
        {
            const auto bits = ParticleRng::Philox4x32({serial, round, slot, 0u}, key);
            for (int i = 0; i < 4; ++i) u[round * 4 + i] = ParticleRng::ToUnit(bits[i]);
        }

        const auto shape = static_cast<ParticleEmitterShape>(static_cast<std::uint32_t>(emitter.PositionShape.w));
        const auto mode = static_cast<ParticleVelocityMode>(static_cast<std::uint32_t>(emitter.Params1.w));
        const glm::vec3 axis(emitter.AxisRadius);
        const float radius = emitter.AxisRadius.w;
        const float inner = emitter.Params0.x;
        const float thickness = emitter.Params0.y;
        const float halfAngle = emitter.Params0.z;
        const float speed = emitter.Params0.w;

        const auto frame = [](const glm::vec3& direction)
        {
            const glm::vec3 helper = std::abs(direction.z) < 0.999f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
            const glm::vec3 first = glm::normalize(glm::cross(helper, direction));
            return std::pair{first, glm::cross(direction, first)};
        };

        glm::vec3 offset(0.0f);
        glm::vec3 direction = axis;
        switch (shape)
        {
        case ParticleEmitterShape::Sphere:
        case ParticleEmitterShape::Shell:
        {
            const float z = 2.0f * u[0] - 1.0f;
            const float phi = kTwoPi * u[1];
            const float s = std::sqrt(std::max(0.0f, 1.0f - z * z));
            const float low = shape == ParticleEmitterShape::Shell ? inner : 0.0f;
            const float volume = low * low * low + (radius * radius * radius - low * low * low) * u[2];
            offset = glm::vec3(s * std::cos(phi), s * std::sin(phi), z) * std::cbrt(volume);
            break;
        }
        case ParticleEmitterShape::Disk:
        case ParticleEmitterShape::Ring:
        {
            const auto [first, second] = frame(axis);
            const float phi = kTwoPi * u[0];
            const float ringRadius = shape == ParticleEmitterShape::Disk
                                         ? std::sqrt(inner * inner + (radius * radius - inner * inner) * u[1])
                                         : radius + (u[1] - 0.5f) * thickness;
            offset = ringRadius * (std::cos(phi) * first + std::sin(phi) * second) + (u[2] - 0.5f) * thickness * axis;
            break;
        }
        case ParticleEmitterShape::Cone:
        {
            const auto [first, second] = frame(axis);
            const float cosine = std::cos(halfAngle) + (1.0f - std::cos(halfAngle)) * u[0];
            const float sine = std::sqrt(std::max(0.0f, 1.0f - cosine * cosine));
            const float phi = kTwoPi * u[1];
            direction = cosine * axis + sine * (std::cos(phi) * first + std::sin(phi) * second);
            break;
        }
        case ParticleEmitterShape::Stream:
        {
            const glm::vec3 toTarget = glm::vec3(emitter.Target) - glm::vec3(emitter.PositionShape);
            const float length = glm::length(toTarget);
            direction = length > 0.0f ? toTarget / length : axis;
            const auto [first, second] = frame(direction);
            const float phi = kTwoPi * u[1];
            offset = radius * std::sqrt(u[0]) * (std::cos(phi) * first + std::sin(phi) * second);
            break;
        }
        case ParticleEmitterShape::Point: break;
        }

        glm::vec3 pushDirection = direction;
        if (mode == ParticleVelocityMode::Radial)
        {
            const float length = glm::length(offset);
            if (length > 0.0f) pushDirection = offset / length;
        }
        else if (mode == ParticleVelocityMode::Azimuthal)
        {
            const glm::vec3 around = glm::cross(axis, offset);
            const float length = glm::length(around);
            if (length > 0.0f) pushDirection = around / length;
        }

        const auto [n0, n1] = ParticleRng::Normal(u[4], u[5]);
        const auto [n2, unused] = ParticleRng::Normal(u[6], u[7]);
        EmitterSample sample;
        sample.Velocity = glm::vec3(emitter.Bulk) + speed * pushDirection + emitter.Params1.x * glm::vec3(n0, n1, n2);
        float lag = 0.0f;
        if (index >= emitter.Batch.w)
        {
            const std::uint32_t continuous = std::max(emitter.Batch.y - emitter.Batch.w, 1u);
            const float phase = (static_cast<float>(index - emitter.Batch.w) + 0.5f) / static_cast<float>(continuous);
            lag = (1.0f - phase) * baseStep;
        }
        sample.Position = glm::vec3(emitter.PositionShape) + offset + sample.Velocity * lag;
        sample.Temperature = emitter.Params1.y;
        sample.Weight = emitter.Params1.z;
        sample.Age = lag;
        sample.Serial = serial;
        return sample;
    }
}

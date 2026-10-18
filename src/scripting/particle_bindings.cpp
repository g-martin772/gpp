module GPP.Scripting;

import std;
import glm;
import GPP.Core;
import GPP.Simulation;
import :Luau;
import :ParticleBindings;

namespace GPP
{
    namespace
    {
        constexpr std::array<std::pair<std::string_view, ParticleEmitterShape>, 7> kShapes{{
            {"point", ParticleEmitterShape::Point}, {"sphere", ParticleEmitterShape::Sphere},
            {"shell", ParticleEmitterShape::Shell}, {"disk", ParticleEmitterShape::Disk},
            {"ring", ParticleEmitterShape::Ring}, {"cone", ParticleEmitterShape::Cone},
            {"stream", ParticleEmitterShape::Stream}}};

        constexpr std::array<std::pair<std::string_view, ParticleVelocityMode>, 3> kVelocityModes{{
            {"axial", ParticleVelocityMode::Axial}, {"radial", ParticleVelocityMode::Radial},
            {"azimuthal", ParticleVelocityMode::Azimuthal}}};

        glm::quat Facing(const glm::dvec3& direction)
        {
            const double length = glm::length(direction);
            if (!(length > 0.0)) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            const glm::dvec3 unit = direction / length;
            const glm::dvec3 axis = glm::cross(glm::dvec3(0.0, 0.0, 1.0), unit);
            const double sine = glm::length(axis);
            const double cosine = unit.z;
            if (sine < 1e-12)
            {
                return cosine > 0.0 ? glm::quat(1.0f, 0.0f, 0.0f, 0.0f) : glm::quat(0.0f, 1.0f, 0.0f, 0.0f);
            }
            const double half = 0.5 * std::atan2(sine, cosine);
            const glm::dvec3 normal = axis / sine;
            return glm::quat(static_cast<float>(std::cos(half)), static_cast<float>(normal.x * std::sin(half)),
                             static_cast<float>(normal.y * std::sin(half)), static_cast<float>(normal.z * std::sin(half)));
        }

        std::uint64_t DeterministicSeed(const std::uint64_t id)
        {
            return ((id & ~kTransientParticleIdBit) + 1) * 0x9E3779B97F4A7C15ull;
        }

        std::optional<glm::dvec3> ReadVector(LuauNativeCall& call, const int first)
        {
            if (call.Count() >= first + 2 && call.IsNumber(first) && call.IsNumber(first + 1) && call.IsNumber(first + 2))
            {
                return glm::dvec3(call.Number(first), call.Number(first + 1), call.Number(first + 2));
            }
            return std::nullopt;
        }

        // Luau side: public functions normalise vectors and tables to x, y, z numbers before the natives see them
        constexpr std::string_view kPrelude = R"lua(
local function xyz(value)
    if type(value) == "table" or typeof(value) == "vector" then return value.x, value.y, value.z end
    return nil
end
local vector_keys = { from = true, position = true, origin = true, toward = true, target = true, direction = true,
    axis = true, bulk = true, velocity_offset = true }
function particles.emitter(spec)
    local id = particles.__create(spec.shape or "point")
    for key, value in spec do
        if key ~= "shape" then
            if vector_keys[key] then
                local x, y, z = xyz(value)
                particles.__set_xyz(id, key, x, y, z)
            else
                particles.__set(id, key, value)
            end
        end
    end
    particles.__seal(id, spec.seed ~= nil)
    return id
end
function particles.set_origin(id, a, b, c)
    local x, y, z
    if b == nil then x, y, z = xyz(a) else x, y, z = a, b, c end
    return particles.__set_xyz(id, "origin", x, y, z)
end
function particles.set_rate(id, rate) return particles.__set(id, "rate_or_mass_rate", rate) end
function particles.pause(id) return particles.__set(id, "active", false) end
function particles.resume(id) return particles.__set(id, "active", true) end
function particles.region_stats(center, radius, b, c, d)
    local x, y, z
    if d ~= nil then x, y, z, radius = center, radius, b, c else x, y, z = xyz(center) end
    local count, mass, temperature, vx, vy, vz = particles.__region(x, y, z, radius)
    if count == nil then return nil end
    return { count = count, mass = mass, mean_temperature = temperature, mean_velocity = vector.create(vx, vy, vz) }
end
)lua";
    }

    bool ApplyParticleEmitterKey(ParticleEmitterDef& def, const std::string_view key, const LuauValue& value)
    {
        auto& p = def.Params;
        if (const auto* text = std::get_if<std::string>(&value))
        {
            if (key == "shape")
            {
                const auto it = std::ranges::find(kShapes, *text, &decltype(kShapes)::value_type::first);
                if (it == kShapes.end()) return false;
                p.Shape = it->second;
                return true;
            }
            if (key == "velocity_mode" || key == "velocity")
            {
                const auto it = std::ranges::find(kVelocityModes, *text, &decltype(kVelocityModes)::value_type::first);
                if (it == kVelocityModes.end()) return false;
                p.VelocityMode = it->second;
                return true;
            }
            return false;
        }
        if (const auto* flag = std::get_if<bool>(&value))
        {
            if (key == "active") p.Active = *flag;
            else if (key == "thermal") p.ThermalVelocity = *flag;
            else return false;
            return true;
        }
        if (const auto* vector = std::get_if<glm::dvec3>(&value))
        {
            if (key == "from" || key == "position" || key == "origin") def.Position = *vector;
            else if (key == "toward" || key == "target") def.Target = p.TargetPosition = *vector;
            else if (key == "direction" || key == "axis") def.Rotation = Facing(*vector);
            else if (key == "bulk") p.BulkVelocity = *vector;
            else return false;
            return true;
        }
        if (const auto* entity = std::get_if<std::uint64_t>(&value))
        {
            if (key != "attached") return false;
            p.AttachedTo = *entity;
            return true;
        }
        double number = 0.0;
        if (const auto* d = std::get_if<double>(&value)) number = *d;
        else if (const auto* f = std::get_if<float>(&value)) number = *f;
        else if (const auto* i = std::get_if<int>(&value)) number = *i;
        else return false;

        if (key == "rate") { p.Rate = number; p.RateMode = ParticleRateMode::ParticleRate; }
        else if (key == "mass_rate") { p.MassRate = number; p.RateMode = ParticleRateMode::MassRate; }
        else if (key == "rate_or_mass_rate")
        {
            (p.RateMode == ParticleRateMode::MassRate ? p.MassRate : p.Rate) = number;
        }
        else if (key == "particle_mass") p.ParticleMass = number;
        else if (key == "burst") p.Burst = static_cast<int>(number);
        else if (key == "seed") p.Seed = static_cast<std::uint64_t>(number);
        else if (key == "radius") p.Radius = number;
        else if (key == "inner_radius") p.InnerRadius = number;
        else if (key == "thickness") p.Thickness = number;
        else if (key == "half_angle") p.HalfAngle = static_cast<float>(number);
        else if (key == "speed") p.Speed = number;
        else if (key == "dispersion") p.Dispersion = number;
        else if (key == "mean_molecular_weight") p.MeanMolecularWeight = number;
        else if (key == "temperature") p.Temperature = number;
        else return false;
        return true;
    }

    void RegisterParticleBindings(LuauVm& vm, const std::shared_ptr<ParticleHub>& hub)
    {
        vm.Register("particles", "__create", [hub](LuauNativeCall& call)
        {
            ParticleEmitterComponent params;
            ParticleEmitterDef probe;
            ApplyParticleEmitterKey(probe, "shape", call.String(1));
            params.Shape = probe.Params.Shape;
            params.Rate = 0.0;
            const auto id = hub->CreateEmitter(params, glm::dvec3(0.0));
            call.PushEntity(id);
            return 1;
        });
        vm.Register("particles", "__set", [hub](LuauNativeCall& call)
        {
            const std::string key = call.String(2);
            LuauValue value;
            if (key == "active" || key == "thermal") value = call.ToValue(3, LuauType::Bool);
            else if (key == "shape" || key == "velocity" || key == "velocity_mode") value = call.String(3);
            else if (key == "attached") value = call.Entity(3);
            else value = call.Number(3);
            const bool ok = hub->Edit(call.Entity(1), [&](ParticleEmitterDef& def)
            {
                ApplyParticleEmitterKey(def, key, value);
            });
            call.PushBoolean(ok);
            return 1;
        });
        vm.Register("particles", "__set_xyz", [hub](LuauNativeCall& call)
        {
            const auto vector = ReadVector(call, 3);
            const std::string key = call.String(2);
            bool ok = false;
            if (vector)
            {
                ok = hub->Edit(call.Entity(1), [&](ParticleEmitterDef& def)
                {
                    ApplyParticleEmitterKey(def, key, LuauValue{*vector});
                });
            }
            call.PushBoolean(ok);
            return 1;
        });
        vm.Register("particles", "__seal", [hub](LuauNativeCall& call)
        {
            const std::uint64_t id = call.Entity(1);
            const bool hasSeed = call.ToValue(2, LuauType::Bool) == LuauValue{true};
            hub->Edit(id, [&](ParticleEmitterDef& def)
            {
                if (!hasSeed) def.Params.Seed = DeterministicSeed(id);
            });
            return 0;
        });
        vm.Register("particles", "burst", [hub](LuauNativeCall& call)
        {
            hub->Burst(call.Entity(1), static_cast<std::uint32_t>(std::max(call.Number(2), 0.0)));
            call.PushBoolean(true);
            return 1;
        });
        vm.Register("particles", "destroy", [hub](LuauNativeCall& call)
        {
            call.PushBoolean(hub->Destroy(call.Entity(1)));
            return 1;
        });
        vm.Register("particles", "count", [hub](LuauNativeCall& call)
        {
            call.PushNumber(static_cast<double>(hub->Board()->Count()));
            return 1;
        });
        vm.Register("particles", "__region", [hub](LuauNativeCall& call)
        {
            const auto center = ReadVector(call, 1);
            if (!center) return 0;
            const std::size_t slot = hub->Region(*center, call.Number(4));
            const auto data = hub->Board()->Latest();
            const auto stats = hub->RegionStats(slot);
            if (!stats || !data) return 0;
            const double velocity = data->Units.Velocity();
            call.PushNumber(static_cast<double>(stats->Count));
            call.PushNumber(stats->Mass);
            call.PushNumber(stats->MeanTemperature);
            call.PushNumber(stats->MeanVelocity.x * velocity);
            call.PushNumber(stats->MeanVelocity.y * velocity);
            call.PushNumber(stats->MeanVelocity.z * velocity);
            return 6;
        });
        if (auto result = vm.RunTrusted(kPrelude, "=gpp_particles"); !result)
        {
            throw std::runtime_error("gpp particles prelude failed: " + result.error().Message);
        }
    }
}

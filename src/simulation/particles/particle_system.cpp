module GPP.Simulation;

import std;
import glm;
import GPP.Core;
import :Particles.Types;
import :Particles.Model;
import :Particles.Stats;
import :Particles.Kernels;
import :Particles.Backend;
import :Particles.Components;
import :Particles.Scene;
import :Particles.System;

namespace GPP
{
    ParticleSystem::ParticleSystem(std::unique_ptr<IParticleBackend> backend, ParticleSystemConfig config,
                                   std::shared_ptr<ParticleStatsBoard> board)
        : m_Backend(std::move(backend)), m_Config(std::move(config)), m_Board(std::move(board))
    {
        m_Backend->Configure(m_Config.Settings);
    }

    void ParticleSystem::SetHooks(ParticleHooks hooks)
    {
        m_Backend->SetHooks(std::move(hooks));
    }

    std::pair<double, double> ParticleSystem::RateAndWeight(const ParticleEmitterComponent& emitter)
    {
        const double rate = std::max(emitter.Rate, 0.0);
        const double weight = emitter.RateMode == ParticleRateMode::MassRate
                                  ? (rate > 0.0 ? emitter.MassRate / rate : 0.0)
                                  : emitter.ParticleMass;
        return {rate, weight};
    }

    ParticleEmitterParams ParticleSystem::MakeEmitterParams(const ParticleEmitterDef& def, const ParticleFrame& frame,
                                                            const std::uint32_t slot, const std::uint64_t firstSerial)
    {
        const auto& p = def.Params;
        const auto& units = frame.Units;
        ParticleEmitterParams out;
        out.PositionShape = glm::vec4(units.ToSim(def.Position - frame.Origin), static_cast<float>(p.Shape));
        const glm::vec3 axis = glm::normalize(def.Rotation * glm::vec3(0.0f, 0.0f, 1.0f));
        out.AxisRadius = glm::vec4(axis, static_cast<float>(p.Radius / units.Length));
        out.Params0 = glm::vec4(static_cast<float>(p.InnerRadius / units.Length),
                                static_cast<float>(p.Thickness / units.Length), p.HalfAngle,
                                static_cast<float>(p.Speed / units.Velocity()));
        double sigma = p.Dispersion;
        if (p.ThermalVelocity && p.MeanMolecularWeight > 0.0)
        {
            const double thermal = Units::kBoltzmannConstant * p.Temperature / (p.MeanMolecularWeight * Units::kProtonMass);
            sigma = std::sqrt(sigma * sigma + thermal);
        }
        const auto [rate, weight] = RateAndWeight(p);
        out.Params1 = glm::vec4(static_cast<float>(sigma / units.Velocity()), static_cast<float>(p.Temperature),
                                static_cast<float>(weight), static_cast<float>(p.VelocityMode));
        out.Bulk = glm::vec4(units.VelocityToSim(def.Velocity + p.BulkVelocity), 0.0f);
        out.Target = glm::vec4(units.ToSim(def.Target - frame.Origin), 0.0f);
        out.Ids = glm::uvec4(static_cast<std::uint32_t>(p.Seed), static_cast<std::uint32_t>(p.Seed >> 32), slot, 0u);
        out.Batch = glm::uvec4(static_cast<std::uint32_t>(firstSerial), 0u, 0u, 0u);
        return out;
    }

    std::uint32_t ParticleSystem::SlotFor(std::vector<std::uint64_t>& table, const std::uint64_t id, const std::uint32_t limit)
    {
        for (std::uint32_t i = 0; i < table.size(); ++i)
        {
            if (table[i] == id) return i;
        }
        if (table.size() >= limit) return limit;
        table.push_back(id);
        return static_cast<std::uint32_t>(table.size() - 1);
    }

    void ParticleSystem::Reset()
    {
        m_Backend->Reset();
        m_Emitters.clear();
        m_SinkSlots.clear();
        m_ColliderSlots.clear();
        m_LastBurst = 0;
        m_EmittedCount = 0;
        m_EmittedMass = 0.0;
        m_SimTime = 0.0;
        m_Pending = 0.0;
        Publish();
    }

    void ParticleSystem::Consume(const ParticleSceneState& state)
    {
        const bool reset = state.Epoch != m_Epoch || m_Epoch == std::numeric_limits<std::uint64_t>::max();
        if (reset)
        {
            m_Epoch = state.Epoch;
            Reset();
        }
        m_State = state;

        std::vector<EmitterState> next;
        next.reserve(state.Emitters.size());
        for (const auto& def : state.Emitters)
        {
            const auto it = std::ranges::find(m_Emitters, def.Id, &EmitterState::Id);
            if (it != m_Emitters.end())
            {
                next.push_back(*it);
                continue;
            }
            EmitterState fresh;
            fresh.Id = def.Id;
            std::uint32_t slot = 0;
            while (std::ranges::any_of(m_Emitters, [slot](const EmitterState& e) { return e.Slot == slot; })
                   || std::ranges::any_of(next, [slot](const EmitterState& e) { return e.Slot == slot; }))
            {
                ++slot;
            }
            fresh.Slot = slot;
            next.push_back(fresh);
        }
        m_Emitters = std::move(next);
    }

    ParticleAdvanceReport ParticleSystem::Advance(const double seconds)
    {
        ParticleAdvanceReport report;
        if (!(seconds > 0.0)) return report;

        double base = seconds;
        std::uint32_t steps = 1;
        if (m_Config.BaseStep > 0.0)
        {
            base = m_Config.BaseStep;
            m_Pending += seconds;
            const double wanted = std::floor(m_Pending / base + 1e-9);
            steps = static_cast<std::uint32_t>(std::min<double>(wanted, m_Config.MaxMacroSteps));
            report.Limited = wanted > m_Config.MaxMacroSteps;
        }

        for (std::uint32_t s = 0; s < steps; ++s) RunMacroStep(base, s + 1 == steps, report);

        if (m_Config.BaseStep > 0.0)
        {
            m_Pending -= report.Simulated;
            report.Backlog = std::max(m_Pending, 0.0);
            if (report.Backlog < 1e-9 * base) m_Pending = 0.0;
        }
        Publish();
        return report;
    }

    ParticleAdvanceReport ParticleSystem::Prime()
    {
        ParticleAdvanceReport report;
        RunMacroStep(0.0, true, report);
        Publish();
        return report;
    }

    void ParticleSystem::RunMacroStep(const double base, const bool collectStats, ParticleAdvanceReport& report)
    {
        const ParticleFrame& frame = m_State.Frame;
        const auto& units = frame.Units;
        const double timeUnit = units.Time > 0.0 ? units.Time : 1.0;
        {
            ParticleMacroStep step;
            step.BaseStep = static_cast<float>(base / timeUnit);
            step.SimTime = m_SimTime / timeUnit;
            step.Force = m_Force;
            step.CollectStats = collectStats;

            std::uint32_t newParticles = 0;
            std::uint64_t highestBurst = m_LastBurst;
            for (const auto& def : m_State.Emitters)
            {
                const auto it = std::ranges::find(m_Emitters, def.Id, &EmitterState::Id);
                if (it == m_Emitters.end()) continue;
                std::uint64_t count = 0;
                std::uint64_t bursts = 0;
                const auto [rate, weight] = RateAndWeight(def.Params);
                if (def.Params.Active)
                {
                    if (!it->Released)
                    {
                        it->Released = true;
                        bursts += static_cast<std::uint64_t>(std::max(def.Params.Burst, 0));
                    }
                    it->Credit += rate * base;
                    const double whole = std::floor(it->Credit + 1e-9);
                    it->Credit -= whole;
                    count += static_cast<std::uint64_t>(whole);
                }
                for (const auto& burst : m_State.Bursts)
                {
                    if (burst.Emitter != def.Id || burst.Serial <= m_LastBurst) continue;
                    bursts += burst.Count;
                    highestBurst = std::max(highestBurst, burst.Serial);
                }
                count += bursts;
                if (count == 0 || step.Emitters.size() >= kMaxParticleEmitters) continue;
                auto params = MakeEmitterParams(def, frame, it->Slot, it->NextSerial);
                params.Batch.y = static_cast<std::uint32_t>(count);
                params.Batch.z = newParticles;
                params.Batch.w = static_cast<std::uint32_t>(bursts);
                step.Emitters.push_back(params);
                newParticles += params.Batch.y;
                it->NextSerial += count;
                it->Emitted += count;
                m_EmittedCount += count;
                m_EmittedMass += static_cast<double>(count) * weight;
                report.Emitted += count;
            }
            if (highestBurst != m_LastBurst)
            {
                m_LastBurst = highestBurst;
                if (m_Hub) m_Hub->Acknowledge(m_LastBurst);
            }

            for (const auto& def : m_State.Sinks)
            {
                const std::uint32_t slot = SlotFor(m_SinkSlots, def.Id, kMaxParticleSinks);
                if (!def.Params.Active || slot >= kMaxParticleSinks) continue;
                ParticleSinkParams sink;
                sink.CenterRadius = glm::vec4(units.ToSim(def.Position - frame.Origin),
                                              static_cast<float>(def.Params.Radius / units.Length));
                sink.Info = glm::uvec4(static_cast<std::uint32_t>(def.Params.Mode), def.Params.Accounting ? 1u : 0u, slot, 0u);
                step.Sinks.push_back(sink);
            }

            for (const auto& def : m_State.Colliders)
            {
                const std::uint32_t slot = SlotFor(m_ColliderSlots, def.Id, kMaxParticleColliders);
                const auto& c = def.Params;
                if (!c.Active || c.Response == ParticleColliderResponse::Ignore || slot >= kMaxParticleColliders) continue;
                ParticleColliderParams collider;
                collider.CenterRadius = glm::vec4(units.ToSim(def.Position - frame.Origin),
                                                  static_cast<float>(c.Radius / units.Length));
                collider.HalfExtentsShape = glm::vec4(units.ToSim(c.HalfExtents), c.Shape == ParticleColliderShape::Box ? 1.0f : 0.0f);
                collider.Axis0 = glm::vec4(def.Rotation * glm::vec3(1.0f, 0.0f, 0.0f), 0.0f);
                collider.Axis1 = glm::vec4(def.Rotation * glm::vec3(0.0f, 1.0f, 0.0f), 0.0f);
                collider.Axis2 = glm::vec4(def.Rotation * glm::vec3(0.0f, 0.0f, 1.0f), 0.0f);
                collider.VelocityRestitution = glm::vec4(units.VelocityToSim(def.Velocity), c.Restitution);
                collider.Info = glm::uvec4(static_cast<std::uint32_t>(c.Response), slot, 0u, 0u);
                if (c.Response == ParticleColliderResponse::Drag)
                {
                    const double density = m_Ambient.Density ? m_Ambient.Density(def.Position) : m_Ambient.ConstantDensity;
                    const glm::dvec3 gas = m_Ambient.Velocity ? m_Ambient.Velocity(def.Position) : m_Ambient.ConstantVelocity;
                    const glm::dvec3 relative = def.Velocity - gas;
                    const double speed = glm::length(relative);
                    double area = c.ReferenceArea;
                    if (!(area > 0.0))
                    {
                        const glm::vec3 direction = speed > 0.0 ? glm::vec3(relative / speed) : glm::vec3(1.0f, 0.0f, 0.0f);
                        area = ParticleKernel::ProjectedArea(collider, direction) * units.Length * units.Length;
                    }
                    const glm::dvec3 onBody = ParticleKernel::DragForceOnBody(density, c.DragCoefficient, area, relative);
                    collider.DragForce = glm::vec4(glm::vec3(-onBody / units.Force()), 0.0f);
                }
                step.Colliders.push_back(collider);
            }

            for (std::size_t r = 0; r < m_State.Regions.size() && r < kMaxParticleRegions; ++r)
            {
                const auto& region = m_State.Regions[r];
                step.Regions.push_back({glm::vec4(units.ToSim(region.Center - frame.Origin),
                                                  static_cast<float>(region.Radius / units.Length))});
            }

            m_Backend->MacroStep(step);
            m_SimTime += base;
            ++report.MacroSteps;
            report.Simulated += base;
        }
    }

    std::uint64_t ParticleSystem::EmittedBy(const std::uint64_t emitterId) const
    {
        const auto it = std::ranges::find(m_Emitters, emitterId, &EmitterState::Id);
        return it != m_Emitters.end() ? it->Emitted : 0;
    }

    void ParticleSystem::Publish()
    {
        ParticleBoardData data;
        data.Stats = m_Backend->Stats();
        if (m_State.Frame.Units.Time > 0.0) data.Stats.SimTime *= m_State.Frame.Units.Time;
        data.Units = m_State.Frame.Units;
        data.Epoch = m_Epoch;
        data.Emitted = m_EmittedCount;
        data.EmittedMass = m_EmittedMass;
        data.SinkIds = m_SinkSlots;
        data.ColliderIds = m_ColliderSlots;
        for (const auto& emitter : m_Emitters) data.EmitterAlive.emplace_back(emitter.Id, emitter.Emitted);
        m_Board->Publish(std::move(data));
    }
}

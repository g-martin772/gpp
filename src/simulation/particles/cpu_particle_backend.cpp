module GPP.Simulation;

import std;
import glm;
import :Particles.Types;
import :Particles.Model;
import :Particles.Stats;
import :Particles.Kernels;
import :Particles.Backend;

namespace GPP
{
    namespace
    {
        template <typename Fn>
        void ParallelRanges(const std::uint32_t threads, const std::size_t count, Fn&& fn)
        {
            if (threads <= 1 || count < 2048)
            {
                if (count > 0) fn(std::size_t{0}, count, std::uint32_t{0});
                return;
            }
            const std::size_t chunk = (count + threads - 1) / threads;
            std::vector<std::jthread> workers;
            workers.reserve(threads);
            for (std::uint32_t t = 0; t < threads; ++t)
            {
                const std::size_t begin = t * chunk;
                const std::size_t end = std::min(count, begin + chunk);
                if (begin >= end) break;
                workers.emplace_back([&fn, begin, end, t] { fn(begin, end, t); });
            }
        }
    }

    struct CpuParticleBackend::Tally
    {
        ParticleAccumulatorBank Sinks;
        ParticleAccumulatorBank Colliders;
    };

    CpuParticleBackend::CpuParticleBackend(const std::uint32_t threads)
        : m_Threads(threads != 0 ? threads : std::max(1u, std::thread::hardware_concurrency()))
    {
    }

    void CpuParticleBackend::Configure(const ParticleSettings& settings)
    {
        m_Settings = settings;
        m_Settings.MaxBin = std::min(m_Settings.MaxBin, kMaxParticleBins);
        Reset();
        m_Particles.PosMass.reserve(std::min<std::size_t>(settings.Capacity, 1u << 16));
    }

    void CpuParticleBackend::Reset()
    {
        m_Particles.Clear();
        m_Scratch.Clear();
        m_BinStart.assign(m_Settings.MaxBin + 3, 0);
        m_DragMass.assign(kMaxParticleColliders, 0.0f);
        m_SinkBank.Clear();
        m_ColliderBank.Clear();
        m_SinkBank.Resize(kMaxParticleSinks);
        m_ColliderBank.Resize(kMaxParticleColliders);
        m_Stats = {};
        m_Dropped = 0;
        m_Serial = 0;
        m_SimTime = 0.0;
    }

    void CpuParticleBackend::MacroStep(const ParticleMacroStep& step)
    {
        m_SimTime = step.SimTime + step.BaseStep;
        ++m_Serial;

        Emit(step);
        SortIntoBins(step);

        const float inverseScale = 1.0f / AccumulatorScale();
        std::ranges::fill(m_DragMass, 0.0f);
        for (std::size_t c = 0; c < step.Colliders.size(); ++c)
        {
            if (static_cast<ParticleColliderResponse>(step.Colliders[c].Info.x) != ParticleColliderResponse::Drag) continue;
            FixedPoint inside;
            glm::vec3 normal;
            glm::vec3 surface;
            for (std::size_t i = 0; i < m_Particles.Size(); ++i)
            {
                if (ParticleKernel::Inside(step.Colliders[c], glm::vec3(m_Particles.PosMass[i]), 0.0f, normal, surface))
                {
                    inside.Add(m_Particles.PosMass[i].w, inverseScale);
                }
            }
            m_DragMass[c] = static_cast<float>(inside.ToDouble(AccumulatorScale()));
        }

        std::vector<Tally> tallies(m_Threads);
        for (std::uint32_t bin = 0; bin <= m_Settings.MaxBin; ++bin)
        {
            const std::size_t first = m_BinStart[bin];
            const std::size_t last = m_BinStart[bin + 1];
            ParallelRanges(m_Threads, last - first, [&](const std::size_t begin, const std::size_t end, const std::uint32_t t)
            {
                if (tallies[t].Sinks.Fixed.empty())
                {
                    tallies[t].Sinks.Resize(m_SinkBank.Fixed.size());
                    tallies[t].Colliders.Resize(m_ColliderBank.Fixed.size());
                }
                Advance(step, first + begin, first + end, bin, tallies[t]);
            });
        }
        for (const auto& tally : tallies)
        {
            if (tally.Sinks.Fixed.empty()) continue;
            m_SinkBank.Merge(tally.Sinks);
            m_ColliderBank.Merge(tally.Colliders);
        }

        if (m_Hooks.PostMacroStep)
        {
            ParallelRanges(m_Threads, m_Particles.Size(), [&](const std::size_t begin, const std::size_t end, std::uint32_t)
            {
                for (std::size_t i = begin; i < end; ++i)
                {
                    if (!ParticleArrays::Alive(m_Particles.Meta[i])) continue;
                    glm::vec3 position(m_Particles.PosMass[i]);
                    glm::vec3 velocity(m_Particles.VelTemp[i]);
                    float age = ParticleArrays::Age(m_Particles.Meta[i]);
                    ParticleView view{position, velocity, m_Particles.PosMass[i].w, m_Particles.VelTemp[i].w, age,
                                      ParticleArrays::Species(m_Particles.Meta[i]), ParticleArrays::Owner(m_Particles.Meta[i]),
                                      m_Particles.Meta[i].w};
                    m_Hooks.PostMacroStep(view, step.BaseStep, step.Force);
                    m_Particles.PosMass[i] = glm::vec4(position, m_Particles.PosMass[i].w);
                    m_Particles.VelTemp[i] = glm::vec4(velocity, m_Particles.VelTemp[i].w);
                    m_Particles.Meta[i].z = std::bit_cast<std::uint32_t>(age);
                }
            });
        }

        if (step.CollectStats) CollectStats(step);
    }

    void CpuParticleBackend::Emit(const ParticleMacroStep& step)
    {
        for (const auto& emitter : step.Emitters)
        {
            for (std::uint32_t j = 0; j < emitter.Batch.y; ++j)
            {
                if (m_Particles.Size() >= m_Settings.Capacity)
                {
                    ++m_Dropped;
                    continue;
                }
                const auto sample = ParticleKernel::SampleEmitter(emitter, j, step.BaseStep);
                m_Particles.PosMass.emplace_back(sample.Position, sample.Weight);
                m_Particles.VelTemp.emplace_back(sample.Velocity, sample.Temperature);
                m_Particles.Meta.emplace_back(emitter.Ids.z, ParticleFlag::kAlive, std::bit_cast<std::uint32_t>(sample.Age),
                                              sample.Serial);
            }
        }
    }

    void CpuParticleBackend::SortIntoBins(const ParticleMacroStep& step)
    {
        const std::size_t count = m_Particles.Size();
        const std::uint32_t maxBin = m_Settings.MaxBin;
        std::vector<std::uint8_t> bins(count);
        ParallelRanges(m_Threads, count, [&](const std::size_t begin, const std::size_t end, std::uint32_t)
        {
            for (std::size_t i = begin; i < end; ++i)
            {
                if (!ParticleArrays::Alive(m_Particles.Meta[i]))
                {
                    bins[i] = static_cast<std::uint8_t>(maxBin + 1);
                    continue;
                }
                const float stable = ParticleKernel::StableStep(step.Force, m_Hooks, glm::vec3(m_Particles.PosMass[i]),
                                                                glm::vec3(m_Particles.VelTemp[i]), m_Settings.Eta);
                bins[i] = static_cast<std::uint8_t>(AssignParticleBin(step.BaseStep, stable, maxBin));
            }
        });

        m_BinStart.assign(maxBin + 3, 0);
        for (std::size_t i = 0; i < count; ++i) ++m_BinStart[bins[i] + 1];
        for (std::size_t k = 1; k < m_BinStart.size(); ++k) m_BinStart[k] += m_BinStart[k - 1];
        const std::size_t alive = m_BinStart[maxBin + 1];

        m_Scratch.Resize(alive);
        std::vector<std::size_t> cursor(m_BinStart.begin(), m_BinStart.end());
        for (std::size_t i = 0; i < count; ++i)
        {
            if (bins[i] > maxBin) continue;
            const std::size_t target = cursor[bins[i]]++;
            m_Scratch.PosMass[target] = m_Particles.PosMass[i];
            m_Scratch.VelTemp[target] = m_Particles.VelTemp[i];
            m_Scratch.Meta[target] = m_Particles.Meta[i];
            m_Scratch.Meta[target].y = ParticleFlag::kAlive | (static_cast<std::uint32_t>(bins[i]) << ParticleFlag::kBinShift);
        }
        std::swap(m_Particles, m_Scratch);
    }

    void CpuParticleBackend::Advance(const ParticleMacroStep& step, const std::size_t first, const std::size_t last,
                                     const std::uint32_t bin, Tally& tally)
    {
        const float inverseScale = 1.0f / AccumulatorScale();
        const std::uint32_t substeps = 1u << bin;
        const float dt = step.BaseStep / static_cast<float>(substeps);
        const float halfDt = 0.5f * dt;
        const auto& force = step.Force;

        for (std::size_t i = first; i < last; ++i)
        {
            glm::vec3 position(m_Particles.PosMass[i]);
            glm::vec3 velocity(m_Particles.VelTemp[i]);
            float weight = m_Particles.PosMass[i].w;
            float temperature = m_Particles.VelTemp[i].w;
            float age = ParticleArrays::Age(m_Particles.Meta[i]);
            const std::uint32_t species = ParticleArrays::Species(m_Particles.Meta[i]);
            const std::uint32_t owner = ParticleArrays::Owner(m_Particles.Meta[i]);
            const std::uint32_t serial = m_Particles.Meta[i].w;
            bool alive = true;
            float elapsed = 0.0f;
            glm::vec3 positionComp(0.0f);
            glm::vec3 velocityComp(0.0f);

            const auto fold = [&]
            {
                position += positionComp;
                velocity += velocityComp;
                positionComp = glm::vec3(0.0f);
                velocityComp = glm::vec3(0.0f);
            };

            const auto dragKick = [&](const float at, const float duration)
            {
                glm::vec3 normal;
                glm::vec3 surface;
                for (std::size_t c = 0; c < step.Colliders.size(); ++c)
                {
                    const auto& collider = step.Colliders[c];
                    if (static_cast<ParticleColliderResponse>(collider.Info.x) != ParticleColliderResponse::Drag) continue;
                    if (!(m_DragMass[c] > 0.0f) || !ParticleKernel::Inside(collider, position, at, normal, surface)) continue;
                    const glm::vec3 delta = glm::vec3(collider.DragForce) / m_DragMass[c] * duration;
                    ParticleKernel::CompensatedAdd(velocity, velocityComp, delta);
                    for (int q = 0; q < 3; ++q) tally.Colliders.Fixed[collider.Info.y][4 + q].Add(weight * delta[q], inverseScale);
                }
            };

            for (std::uint32_t s = 0; s < substeps && alive; ++s)
            {
                bool handled = false;
                if (m_Hooks.Step)
                {
                    fold();
                    ParticleView view{position, velocity, weight, temperature, age, species, owner, serial};
                    handled = m_Hooks.Step(view, dt, force);
                }
                if (!handled)
                {
                    ParticleKernel::CompensatedAdd(velocity, velocityComp,
                                                   ParticleKernel::Acceleration(force, m_Hooks, position, velocity) * halfDt);
                    dragKick(elapsed, halfDt);
                    ParticleKernel::CompensatedAdd(position, positionComp, (velocity + velocityComp) * dt);
                    elapsed += dt;
                    ParticleKernel::CompensatedAdd(velocity, velocityComp,
                                                   ParticleKernel::Acceleration(force, m_Hooks, position, velocity) * halfDt);
                    dragKick(elapsed, halfDt);
                }
                else
                {
                    elapsed += dt;
                }
                age += dt;

                glm::vec3 probe = position + positionComp;
                for (std::size_t c = 0; c < step.Colliders.size() && alive; ++c)
                {
                    const auto& collider = step.Colliders[c];
                    const auto response = static_cast<ParticleColliderResponse>(collider.Info.x);
                    if (response != ParticleColliderResponse::Collide && response != ParticleColliderResponse::Absorb) continue;
                    glm::vec3 normal;
                    glm::vec3 surface;
                    if (!ParticleKernel::Inside(collider, probe, elapsed, normal, surface)) continue;
                    fold();
                    auto& fixed = tally.Colliders.Fixed[collider.Info.y];
                    if (response == ParticleColliderResponse::Absorb)
                    {
                        ++tally.Colliders.Count[collider.Info.y];
                        fixed[0].Add(weight, inverseScale);
                        for (int q = 0; q < 3; ++q) fixed[1 + q].Add(weight * velocity[q], inverseScale);
                        alive = false;
                        continue;
                    }
                    const glm::vec3 bodyVelocity(collider.VelocityRestitution);
                    glm::vec3 relative = velocity - bodyVelocity;
                    const float approach = glm::dot(relative, normal);
                    if (approach < 0.0f)
                    {
                        relative -= (1.0f + collider.VelocityRestitution.w) * approach * normal;
                        const glm::vec3 next = bodyVelocity + relative;
                        const glm::vec3 delta = next - velocity;
                        for (int q = 0; q < 3; ++q) fixed[4 + q].Add(weight * delta[q], inverseScale);
                        velocity = next;
                    }
                    position = surface;
                    probe = surface;
                }

                for (std::size_t k = 0; k < step.Sinks.size() && alive; ++k)
                {
                    const auto& sink = step.Sinks[k];
                    const glm::vec3 centre(sink.CenterRadius);
                    const glm::vec3 offset = probe - centre;
                    if (!(glm::dot(offset, offset) < sink.CenterRadius.w * sink.CenterRadius.w)) continue;
                    fold();
                    alive = false;
                    const std::uint32_t slot = sink.Info.z;
                    if (static_cast<ParticleSinkMode>(sink.Info.x) == ParticleSinkMode::Kill)
                    {
                        ++tally.Sinks.Killed[slot];
                        continue;
                    }
                    ++tally.Sinks.Count[slot];
                    if (sink.Info.y == 0) continue;
                    std::array<float, kParticleAccumulators> quantities{};
                    ParticleView final{position, velocity, weight, temperature, age, species, owner, serial};
                    if (m_Hooks.AbsorbQuantities)
                    {
                        m_Hooks.AbsorbQuantities(final, centre, force, quantities);
                    }
                    else
                    {
                        const glm::vec3 angular = glm::cross(position - centre, velocity);
                        quantities = {1.0f, velocity.x, velocity.y, velocity.z, angular.x, angular.y, angular.z,
                                      0.5f * glm::dot(velocity, velocity) + ParticleKernel::Potential(force, m_Hooks, position)};
                    }
                    for (std::uint32_t q = 0; q < kParticleAccumulators; ++q)
                    {
                        tally.Sinks.Fixed[slot][q].Add(weight * quantities[q], inverseScale);
                    }
                }
            }
            fold();

            auto& particles = m_Particles;
            particles.PosMass[i] = glm::vec4(position, weight);
            particles.VelTemp[i] = glm::vec4(velocity, temperature);
            particles.Meta[i].z = std::bit_cast<std::uint32_t>(age);
            if (!alive) particles.Meta[i].y &= ~ParticleFlag::kAlive;
        }
    }

    void CpuParticleBackend::CollectStats(const ParticleMacroStep& step)
    {
        ParticleStats stats;
        stats.Serial = m_Serial;
        stats.SimTime = m_SimTime;
        stats.Dropped = m_Dropped;
        double mass = 0.0;
        double massTemperature = 0.0;
        glm::dvec3 momentum(0.0);
        std::vector<ParticleRegionTotals> regions(step.Regions.size());
        std::vector<double> regionTemperature(step.Regions.size(), 0.0);
        std::vector<glm::dvec3> regionMomentum(step.Regions.size(), glm::dvec3(0.0));
        for (std::size_t i = 0; i < m_Particles.Size(); ++i)
        {
            if (!ParticleArrays::Alive(m_Particles.Meta[i])) continue;
            const double weight = m_Particles.PosMass[i].w;
            const glm::vec3 position(m_Particles.PosMass[i]);
            const glm::dvec3 velocity(m_Particles.VelTemp[i]);
            ++stats.Alive;
            mass += weight;
            massTemperature += weight * m_Particles.VelTemp[i].w;
            momentum += weight * velocity;
            for (std::size_t r = 0; r < step.Regions.size(); ++r)
            {
                const glm::vec3 offset = position - glm::vec3(step.Regions[r].CenterRadius);
                if (!(glm::dot(offset, offset) < step.Regions[r].CenterRadius.w * step.Regions[r].CenterRadius.w)) continue;
                ++regions[r].Count;
                regions[r].Mass += weight;
                regionTemperature[r] += weight * m_Particles.VelTemp[i].w;
                regionMomentum[r] += weight * velocity;
            }
        }
        stats.AliveMass = mass;
        if (mass > 0.0)
        {
            stats.MeanTemperature = massTemperature / mass;
            stats.MeanVelocity = momentum / mass;
        }
        for (std::size_t r = 0; r < regions.size(); ++r)
        {
            if (regions[r].Mass > 0.0)
            {
                regions[r].MeanTemperature = regionTemperature[r] / regions[r].Mass;
                regions[r].MeanVelocity = regionMomentum[r] / regions[r].Mass;
            }
        }
        stats.Regions = std::move(regions);
        for (std::size_t s = 0; s < m_SinkBank.Fixed.size(); ++s)
        {
            stats.Sinks.push_back(m_SinkBank.Totals(s, AccumulatorScale()));
        }
        for (std::size_t c = 0; c < m_ColliderBank.Fixed.size(); ++c)
        {
            stats.Colliders.push_back(m_ColliderBank.Totals(c, AccumulatorScale()));
        }
        m_Stats = std::move(stats);
    }

    void CpuParticleBackend::Download(ParticleArrays& out)
    {
        out.Clear();
        for (std::size_t i = 0; i < m_Particles.Size(); ++i)
        {
            if (!ParticleArrays::Alive(m_Particles.Meta[i])) continue;
            out.PosMass.push_back(m_Particles.PosMass[i]);
            out.VelTemp.push_back(m_Particles.VelTemp[i]);
            out.Meta.push_back(m_Particles.Meta[i]);
        }
    }

    void CpuParticleBackend::Deposit(const ParticleGridSpec& spec, ParticleGrid& out)
    {
        out.Spec = spec;
        out.Mass.assign(spec.NodeCount(), 0.0f);
        out.Momentum.assign(spec.NodeCount(), glm::vec3(0.0f));
        out.MassTemperature.assign(spec.NodeCount(), 0.0f);
        out.OutsideMass = 0.0;
        for (std::size_t i = 0; i < m_Particles.Size(); ++i)
        {
            if (!ParticleArrays::Alive(m_Particles.Meta[i])) continue;
            const float weight = m_Particles.PosMass[i].w;
            const glm::vec3 grid = (glm::vec3(m_Particles.PosMass[i]) - spec.Origin) / spec.CellSize;
            const glm::vec3 base = glm::floor(grid);
            const glm::vec3 fraction = grid - base;
            const glm::ivec3 node(base);
            if (grid.x < 0.0f || grid.y < 0.0f || grid.z < 0.0f || node.x + 1 >= static_cast<int>(spec.Dimensions.x)
                || node.y + 1 >= static_cast<int>(spec.Dimensions.y) || node.z + 1 >= static_cast<int>(spec.Dimensions.z))
            {
                out.OutsideMass += weight;
                continue;
            }
            const glm::vec3 velocity(m_Particles.VelTemp[i]);
            for (int corner = 0; corner < 8; ++corner)
            {
                const int dx = corner & 1;
                const int dy = (corner >> 1) & 1;
                const int dz = (corner >> 2) & 1;
                const float share = (dx ? fraction.x : 1.0f - fraction.x) * (dy ? fraction.y : 1.0f - fraction.y)
                                    * (dz ? fraction.z : 1.0f - fraction.z);
                const std::size_t index = spec.Index(node.x + dx, node.y + dy, node.z + dz);
                out.Mass[index] += weight * share;
                out.Momentum[index] += weight * share * velocity;
                out.MassTemperature[index] += weight * share * m_Particles.VelTemp[i].w;
            }
        }
    }
}

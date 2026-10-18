module;
#include <entt/entt.hpp>
export module GPP.Simulation:Particles.Scene;

import std;
import glm;
import GPP.Core;
import :Ecs;
import :Components;
import :Scene;
import :Particles.Types;
import :Particles.Model;
import :Particles.Stats;
import :Particles.Components;

export namespace GPP
{
    // The frame particles live in: positions are stored relative to Origin in units of Units.Length.
    struct ParticleFrame
    {
        glm::dvec3 Origin{0.0, 0.0, 0.0}; // m
        ParticleUnits Units;
    };

    struct ParticleEmitterDef
    {
        std::uint64_t Id{0};
        ParticleEmitterComponent Params;
        glm::dvec3 Position{0.0};  // m, world frame
        glm::quat Rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::dvec3 Velocity{0.0};  // m/s
        glm::dvec3 Target{0.0};    // m, resolved stream target
    };

    struct ParticleSinkDef
    {
        std::uint64_t Id{0};
        ParticleSinkComponent Params;
        glm::dvec3 Position{0.0};
    };

    struct ParticleColliderDef
    {
        std::uint64_t Id{0};
        ParticleColliderComponent Params;
        glm::dvec3 Position{0.0};
        glm::quat Rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::dvec3 Velocity{0.0};
    };

    struct ParticleRegionQuery
    {
        glm::dvec3 Center{0.0}; // m, world frame
        double Radius{0.0};     // m; zero marks a free slot
    };

    struct ParticleBurstRequest
    {
        std::uint64_t Emitter{0};
        std::uint32_t Count{0};
        std::uint64_t Serial{0};
    };

    // Everything the render thread needs from the sim thread to run the particle system; plain data, copyable.
    struct ParticleSceneState
    {
        std::uint64_t Epoch{0};
        std::uint64_t SceneGeneration{0};
        std::uint64_t ContentHash{0};
        ParticleFrame Frame;
        std::vector<ParticleEmitterDef> Emitters;
        std::vector<ParticleSinkDef> Sinks;
        std::vector<ParticleColliderDef> Colliders;
        std::vector<ParticleRegionQuery> Regions;
        std::vector<ParticleBurstRequest> Bursts;
    };

    inline constexpr std::uint64_t kTransientParticleIdBit = 1ull << 63;

    struct ParticleHubSnapshot
    {
        std::uint64_t Epoch{0};
        std::uint64_t Version{0};
        std::vector<ParticleEmitterDef> Emitters;
        std::vector<ParticleRegionQuery> Regions;
        std::vector<ParticleBurstRequest> Bursts;
    };

    // Transient emitters made by scripts. Lives next to the scene, never saved; Reset() on Stop clears it and bumps the epoch
    // so the particle system drops every particle. Ids count up from a fixed base, so a replayed script gets the same ids.
    class ParticleHub
    {
    public:
        explicit ParticleHub(std::shared_ptr<ParticleStatsBoard> board = std::make_shared<ParticleStatsBoard>())
            : m_Board(std::move(board))
        {
        }

        [[nodiscard]] const std::shared_ptr<ParticleStatsBoard>& Board() const noexcept { return m_Board; }

        std::uint64_t CreateEmitter(ParticleEmitterComponent params, const glm::dvec3& position,
                                    const glm::quat& rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f))
        {
            std::scoped_lock lock(m_Mutex);
            const std::uint64_t id = kTransientParticleIdBit | ++m_NextId;
            ParticleEmitterDef def;
            def.Id = id;
            def.Params = std::move(params);
            def.Position = position;
            def.Rotation = rotation;
            def.Target = def.Params.TargetPosition;
            m_Emitters.push_back(std::move(def));
            ++m_Version;
            return id;
        }

        bool Destroy(const std::uint64_t id)
        {
            std::scoped_lock lock(m_Mutex);
            const auto count = std::erase_if(m_Emitters, [id](const ParticleEmitterDef& e) { return e.Id == id; });
            m_Version += count;
            return count > 0;
        }

        template <typename Fn>
        bool Edit(const std::uint64_t id, Fn&& edit)
        {
            std::scoped_lock lock(m_Mutex);
            for (auto& def : m_Emitters)
            {
                if (def.Id != id) continue;
                edit(def);
                ++m_Version;
                return true;
            }
            return false;
        }

        void Burst(const std::uint64_t emitter, const std::uint32_t count)
        {
            std::scoped_lock lock(m_Mutex);
            m_Bursts.push_back({emitter, count, ++m_BurstSerial});
            ++m_Version;
        }

        void Acknowledge(const std::uint64_t serial)
        {
            std::scoped_lock lock(m_Mutex);
            std::erase_if(m_Bursts, [serial](const ParticleBurstRequest& b) { return b.Serial <= serial; });
        }

        // Slot of a query region; the particle system reports the region in the same position
        std::size_t Region(const glm::dvec3& center, const double radius)
        {
            std::scoped_lock lock(m_Mutex);
            for (std::size_t i = 0; i < m_Regions.size(); ++i)
            {
                if (m_Regions[i].Center == center && m_Regions[i].Radius == radius) return i;
            }
            if (m_Regions.size() >= kMaxParticleRegions) return kMaxParticleRegions;
            m_Regions.push_back({center, radius});
            ++m_Version;
            return m_Regions.size() - 1;
        }

        [[nodiscard]] std::optional<ParticleRegionTotals> RegionStats(const std::size_t slot) const
        {
            const auto data = m_Board->Latest();
            if (!data || slot >= data->Stats.Regions.size()) return std::nullopt;
            return data->Stats.Regions[slot];
        }

        void Reset()
        {
            std::scoped_lock lock(m_Mutex);
            m_Emitters.clear();
            m_Regions.clear();
            m_Bursts.clear();
            m_NextId = 0;
            m_BurstSerial = 0;
            ++m_Epoch;
            ++m_Version;
        }

        [[nodiscard]] ParticleHubSnapshot Snapshot() const
        {
            std::scoped_lock lock(m_Mutex);
            return {m_Epoch, m_Version, m_Emitters, m_Regions, m_Bursts};
        }

    private:
        mutable std::mutex m_Mutex;
        std::shared_ptr<ParticleStatsBoard> m_Board;
        std::vector<ParticleEmitterDef> m_Emitters;
        std::vector<ParticleRegionQuery> m_Regions;
        std::vector<ParticleBurstRequest> m_Bursts;
        std::uint64_t m_NextId{0};
        std::uint64_t m_BurstSerial{0};
        std::uint64_t m_Epoch{0};
        std::uint64_t m_Version{0};
    };

    // Reads emitters, sinks and colliders from a scene snapshot, in entity guid order, merged with the script hub. Pure.
    [[nodiscard]] ParticleSceneState GatherParticleScene(const Scene& scene, const ParticleFrame& frame,
                                                         std::uint64_t epoch, std::uint64_t sceneGeneration,
                                                         const ParticleHubSnapshot* transient = nullptr);
}

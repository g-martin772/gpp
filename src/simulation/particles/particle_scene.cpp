module;
#include <entt/entt.hpp>
module GPP.Simulation;

import std;
import glm;
import :Ecs;
import :Components;
import :Scene;
import :Particles.Types;
import :Particles.Components;
import :Particles.Scene;

namespace GPP
{
    namespace
    {
        struct Hasher
        {
            std::uint64_t Value{1469598103934665603ull};

            void Mix(const std::uint64_t word)
            {
                for (int shift = 0; shift < 64; shift += 8)
                {
                    Value ^= (word >> shift) & 0xFFu;
                    Value *= 1099511628211ull;
                }
            }
            void Mix(const double value) { Mix(std::bit_cast<std::uint64_t>(value)); }
            void Mix(const float value) { Mix(static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(value))); }
            void Mix(const glm::dvec3& v) { Mix(v.x); Mix(v.y); Mix(v.z); }
            void Mix(const glm::quat& q) { Mix(q.x); Mix(q.y); Mix(q.z); Mix(q.w); }
            void Mix(const bool value) { Mix(static_cast<std::uint64_t>(value)); }
            void Mix(const int value) { Mix(static_cast<std::uint64_t>(static_cast<std::int64_t>(value))); }
            template <typename E> requires std::is_enum_v<E>
            void Mix(const E value) { Mix(static_cast<std::uint64_t>(value)); }
        };

        void MixEmitter(Hasher& h, const ParticleEmitterDef& e)
        {
            const auto& p = e.Params;
            h.Mix(e.Id); h.Mix(p.Active); h.Mix(p.Shape); h.Mix(p.RateMode); h.Mix(p.Rate); h.Mix(p.ParticleMass);
            h.Mix(p.MassRate); h.Mix(p.Burst); h.Mix(p.Seed); h.Mix(p.Radius); h.Mix(p.InnerRadius); h.Mix(p.Thickness);
            h.Mix(p.HalfAngle); h.Mix(p.VelocityMode); h.Mix(p.Speed); h.Mix(p.BulkVelocity); h.Mix(p.Dispersion);
            h.Mix(p.ThermalVelocity); h.Mix(p.MeanMolecularWeight); h.Mix(p.Temperature);
            h.Mix(e.Position); h.Mix(e.Rotation); h.Mix(e.Velocity); h.Mix(e.Target);
        }

        struct Pose
        {
            glm::dvec3 Position{0.0};
            glm::quat Rotation{1.0f, 0.0f, 0.0f, 0.0f};
            glm::dvec3 Velocity{0.0};
        };

        Pose PoseOf(const Scene& scene, const entt::entity entity)
        {
            Pose pose;
            if (!scene.IsValid(entity)) return pose;
            if (const auto* transform = scene.Registry().try_get<TransformComponent>(entity))
            {
                pose.Position = transform->Position;
                pose.Rotation = transform->Rotation;
            }
            if (const auto* velocity = scene.Registry().try_get<VelocityComponent>(entity)) pose.Velocity = velocity->Linear;
            return pose;
        }
    }

    ParticleSceneState GatherParticleScene(const Scene& scene, const ParticleFrame& frame, const std::uint64_t epoch,
                                           const std::uint64_t sceneGeneration, const ParticleHubSnapshot* transient)
    {
        ParticleSceneState state;
        state.Epoch = epoch + (transient ? transient->Epoch : 0);
        state.SceneGeneration = sceneGeneration;
        state.Frame = frame;
        const auto& registry = scene.Registry();

        for (const auto [entity, emitter] : registry.view<const ParticleEmitterComponent>().each())
        {
            ParticleEmitterDef def;
            def.Id = scene.GuidOf(entity);
            def.Params = emitter;
            const entt::entity source = emitter.AttachedTo != 0 ? scene.FindByGuid(emitter.AttachedTo) : entity;
            const Pose pose = PoseOf(scene, scene.IsValid(source) ? source : entity);
            def.Position = pose.Position;
            def.Rotation = pose.Rotation;
            def.Velocity = pose.Velocity;
            def.Target = emitter.TargetPosition;
            if (emitter.TargetEntity != 0)
            {
                const entt::entity target = scene.FindByGuid(emitter.TargetEntity);
                if (scene.IsValid(target)) def.Target = PoseOf(scene, target).Position;
            }
            state.Emitters.push_back(std::move(def));
        }
        std::ranges::sort(state.Emitters, {}, &ParticleEmitterDef::Id);

        for (const auto [entity, sink] : registry.view<const ParticleSinkComponent>().each())
        {
            state.Sinks.push_back({scene.GuidOf(entity), sink, PoseOf(scene, entity).Position});
        }
        std::ranges::sort(state.Sinks, {}, &ParticleSinkDef::Id);

        for (const auto [entity, collider] : registry.view<const ParticleColliderComponent>().each())
        {
            const Pose pose = PoseOf(scene, entity);
            state.Colliders.push_back({scene.GuidOf(entity), collider, pose.Position, pose.Rotation, pose.Velocity});
        }
        std::ranges::sort(state.Colliders, {}, &ParticleColliderDef::Id);

        if (transient)
        {
            for (auto def : transient->Emitters)
            {
                if (def.Params.AttachedTo != 0)
                {
                    const entt::entity source = scene.FindByGuid(def.Params.AttachedTo);
                    if (scene.IsValid(source))
                    {
                        const Pose pose = PoseOf(scene, source);
                        def.Position = pose.Position;
                        def.Rotation = pose.Rotation;
                        def.Velocity = pose.Velocity;
                    }
                }
                state.Emitters.push_back(std::move(def));
            }
            state.Regions = transient->Regions;
            state.Bursts = transient->Bursts;
        }

        Hasher hash;
        hash.Mix(state.Epoch);
        hash.Mix(frame.Origin);
        hash.Mix(frame.Units.Length);
        hash.Mix(frame.Units.Time);
        for (const auto& e : state.Emitters) MixEmitter(hash, e);
        for (const auto& s : state.Sinks)
        {
            hash.Mix(s.Id); hash.Mix(s.Params.Active); hash.Mix(s.Params.Radius); hash.Mix(s.Params.Mode);
            hash.Mix(s.Params.Accounting); hash.Mix(s.Position);
        }
        for (const auto& c : state.Colliders)
        {
            hash.Mix(c.Id); hash.Mix(c.Params.Active); hash.Mix(c.Params.Response); hash.Mix(c.Params.Shape);
            hash.Mix(c.Params.Radius); hash.Mix(c.Params.HalfExtents); hash.Mix(c.Params.Restitution);
            hash.Mix(c.Params.DragCoefficient); hash.Mix(c.Params.ReferenceArea);
            hash.Mix(c.Position); hash.Mix(c.Rotation); hash.Mix(c.Velocity);
        }
        for (const auto& r : state.Regions) { hash.Mix(r.Center); hash.Mix(r.Radius); }
        for (const auto& b : state.Bursts) { hash.Mix(b.Emitter); hash.Mix(static_cast<std::uint64_t>(b.Count)); hash.Mix(b.Serial); }
        state.ContentHash = hash.Value;
        return state;
    }
}

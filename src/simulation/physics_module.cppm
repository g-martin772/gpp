export module GPP.Simulation:PhysicsModule;

import std;
import glm;
import GPP.Core;
import :Ecs;
import :Components;
import :Scene;
import :Runner;
import :Physics;

namespace GPP
{
    export struct PhysicsContactEvent
    {
        std::uint64_t GuidA = 0;
        std::uint64_t GuidB = 0;
    };

    export struct PhysicsTriggerEvent
    {
        std::uint64_t TriggerGuid = 0;
        std::uint64_t OtherGuid = 0;
        bool Entered = true;
    };

    export class PhysicsSimulationModule : public ISimulationModule, public physx::PxSimulationEventCallback
    {
    public:
        using Dependencies = std::tuple<EventDispatcher, Logger>;
        PhysicsSimulationModule(std::shared_ptr<EventDispatcher> dispatcher, std::shared_ptr<Logger> logger);
        ~PhysicsSimulationModule() override;

        void OnInit(Scene& scene) override;
        void OnTick(Scene& scene, float deltaTime) override;
        void OnShutdown(Scene& scene) override;

        void onConstraintBreak(physx::PxConstraintInfo*, physx::PxU32) override {}
        void onWake(physx::PxActor**, physx::PxU32) override {}
        void onSleep(physx::PxActor**, physx::PxU32) override {}
        void onContact(const physx::PxContactPairHeader& pairHeader,
                       const physx::PxContactPair* pairs, physx::PxU32 count) override;
        void onTrigger(physx::PxTriggerPair* pairs, physx::PxU32 count) override;
        void onAdvance(const physx::PxRigidBody* const*, const physx::PxTransform*, physx::PxU32) override {}

        [[nodiscard]] physx::PxScene* GetPxScene() const noexcept { return m_Scene; }

        [[nodiscard]] physx::PxRigidActor* FindActor(entt::entity entity) const
        {
            const auto it = m_ActorsByEntity.find(entity);
            return it != m_ActorsByEntity.end() ? it->second : nullptr;
        }

    private:
        void SyncActors(Scene& scene);
        void StepPhysics(float deltaTime);
        void WriteBackTransforms(Scene& scene);
        [[nodiscard]] physx::PxRigidActor* CreateActor(entt::entity entity, const RigidBodyComponent& body,
                                                       const ColliderComponent& collider,
                                                       const TransformComponent& transform);
        void SeedVelocity(entt::entity entity, Scene& scene, physx::PxRigidActor* actor);
        [[nodiscard]] physx::PxConvexMesh* CookConvexMesh(const std::vector<glm::vec3>& points);
        void DestroyActor(physx::PxRigidActor* actor);
        [[nodiscard]] std::uint64_t GuidForActor(physx::PxRigidActor* actor) const;

        std::shared_ptr<EventDispatcher> m_Dispatcher;
        std::shared_ptr<Logger> m_Logger;

        physx::PxDefaultAllocator m_Allocator;
        physx::PxDefaultErrorCallback m_ErrorCallback;
        physx::PxFoundation* m_Foundation = nullptr;
        physx::PxPhysics* m_Physics = nullptr;
        physx::PxDefaultCpuDispatcher* m_CpuDispatcher = nullptr;
        physx::PxScene* m_Scene = nullptr;
        physx::PxMaterial* m_DefaultMaterial = nullptr;

        std::unordered_map<entt::entity, physx::PxRigidActor*> m_ActorsByEntity;
        std::unordered_map<physx::PxRigidActor*, entt::entity> m_EntitiesByActor;
        Scene* m_CurrentScene = nullptr;
    };
}

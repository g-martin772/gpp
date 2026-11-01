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
    // A = lower guid; normal B to A; RelativeVelocity = A - B pre-solve
    export struct PhysicsContactEvent
    {
        std::uint64_t GuidA = 0;
        std::uint64_t GuidB = 0;
        glm::dvec3 Point{0.0};          // m, world space
        glm::dvec3 Normal{0.0};
        double Impulse = 0.0;           // N s, summed
        glm::dvec3 RelativeVelocity{0.0}; // m/s
        int MaterialA = 0;
        int MaterialB = 0;
    };

    export struct PhysicsTriggerEvent
    {
        std::uint64_t TriggerGuid = 0;
        std::uint64_t OtherGuid = 0;
        bool Entered = true;
    };

    export struct PhysicsEventBatch
    {
        std::vector<PhysicsContactEvent> Contacts;
        std::vector<PhysicsTriggerEvent> Triggers;
    };

    export struct PhysicsHit
    {
        std::uint64_t Guid = 0;
        glm::dvec3 Point{0.0};
        glm::dvec3 Normal{0.0};
        double Distance = 0.0;
    };

    export enum class PhysicsCommandKind { Force, Torque, Impulse, ImpulseAtPoint, LinearVelocity, AngularVelocity, KinematicTarget };

    export struct PhysicsCommand
    {
        PhysicsCommandKind Kind = PhysicsCommandKind::Force;
        std::uint64_t Guid = 0;
        glm::dvec3 Vector{0.0};
        glm::dvec3 Point{0.0};
        std::optional<glm::quat> Rotation;
    };

    export enum class PhysicsJointKind { Fixed, Hinge, Distance };

    export struct PhysicsJointParams
    {
        std::optional<glm::dvec3> Anchor;
        glm::dvec3 Axis{1.0, 0.0, 0.0};
        std::optional<double> Distance;
        std::optional<double> MinDistance;
        double Stiffness = 0.0;
        double Damping = 0.0;
        double BreakForce = 0.0;
        double BreakTorque = 0.0;
    };

    export class PhysicsSimulationModule : public ISimulationModule, public physx::PxSimulationEventCallback
    {
    public:
        using Dependencies = std::tuple<EventDispatcher, Logger>;
        PhysicsSimulationModule(std::shared_ptr<EventDispatcher> dispatcher, std::shared_ptr<Logger> logger);
        ~PhysicsSimulationModule() override;

        [[nodiscard]] SimulationPhase Phase() const noexcept override { return SimulationPhase::Integrate; }
        [[nodiscard]] bool ReportsChanges() const noexcept override { return true; }

        void OnInit(Scene& scene) override;
        void OnTick(Scene& scene, float deltaTime) override;
        void OnShutdown(Scene& scene) override;

        void onConstraintBreak(physx::PxConstraintInfo* constraints, physx::PxU32 count) override;
        void onWake(physx::PxActor**, physx::PxU32) override {}
        void onSleep(physx::PxActor**, physx::PxU32) override {}
        void onContact(const physx::PxContactPairHeader& pairHeader,
                       const physx::PxContactPair* pairs, physx::PxU32 count) override;
        void onTrigger(physx::PxTriggerPair* pairs, physx::PxU32 count) override;
        void onAdvance(const physx::PxRigidBody* const*, const physx::PxTransform*, physx::PxU32) override {}

        [[nodiscard]] physx::PxScene* GetPxScene() const noexcept { return m_Scene; }

        void SetDebugVisualizationEnabled(bool enabled) noexcept
        {
            m_DebugVisualizationEnabled.store(enabled, std::memory_order_relaxed);
        }

        [[nodiscard]] physx::PxRigidActor* FindActor(entt::entity entity) const
        {
            const auto it = m_ActorsByEntity.find(entity);
            return it != m_ActorsByEntity.end() ? it->second : nullptr;
        }

        [[nodiscard]] glm::dvec3 Origin() const noexcept { return m_OriginM; }

        void SetEventCapture(const bool capture) noexcept { m_CaptureEvents = capture; }
        [[nodiscard]] PhysicsEventBatch TakeEvents() { return std::exchange(m_Events, {}); }

        [[nodiscard]] std::string SaveState() const override;
        void LoadState(const std::string& state) override;

        [[nodiscard]] std::optional<PhysicsHit> Raycast(const glm::dvec3& origin, const glm::dvec3& direction,
                                                        double maxDistance, std::uint32_t mask);
        [[nodiscard]] std::optional<PhysicsHit> Spherecast(const glm::dvec3& origin, double radius,
                                                           const glm::dvec3& direction, double maxDistance,
                                                           std::uint32_t mask);
        [[nodiscard]] std::vector<std::uint64_t> Overlap(const glm::dvec3& center, double radius, std::uint32_t mask);

        void QueueCommand(PhysicsCommand command) { m_Commands.push_back(std::move(command)); }
        [[nodiscard]] std::expected<std::uint32_t, std::string> CreateJoint(std::uint64_t guidA, std::uint64_t guidB,
                                                                             PhysicsJointKind kind,
                                                                             const PhysicsJointParams& params);
        bool BreakJoint(std::uint32_t id);
        [[nodiscard]] std::optional<bool> JointBroken(std::uint32_t id) const;

        [[nodiscard]] std::vector<physx::PxDebugLine> GetDebugLines() const
        {
            std::scoped_lock lock(m_DebugLinesMutex);
            return m_DebugLinesSnapshot;
        }

    private:
        struct JointSpec
        {
            PhysicsJointKind Kind = PhysicsJointKind::Fixed;
            std::uint64_t GuidA = 0;
            std::uint64_t GuidB = 0;
            physx::PxTransform FrameA;
            physx::PxTransform FrameB;
            float Distance = 0.0f;
            float MinDistance = -1.0f;
            float Stiffness = 0.0f;
            float Damping = 0.0f;
            float BreakForce = 0.0f;
            float BreakTorque = 0.0f;
        };

        struct JointRecord
        {
            JointSpec Spec;
            physx::PxJoint* Joint = nullptr;
            bool Broken = false;
        };

        struct KinematicTarget
        {
            glm::dvec3 Position{0.0};
            glm::quat Rotation{1.0f, 0.0f, 0.0f, 0.0f};
        };

        [[nodiscard]] bool PrepareQuery();
        [[nodiscard]] std::optional<PhysicsHit> ToHit(physx::PxRigidActor* actor, const physx::PxVec3& position,
                                                      const physx::PxVec3& normal, float distance) const;
        void ApplyCommands(Scene& scene);
        void InstantiatePendingJoints();
        [[nodiscard]] physx::PxJoint* BuildJoint(const JointSpec& spec);
        void ReapBrokenJoints();
        void ReleaseJoints(physx::PxRigidActor* actor);
        void SyncActors(Scene& scene);
        struct AppliedState
        {
            glm::dvec3 Position{0.0};
            glm::quat Rotation{1.0f, 0.0f, 0.0f, 0.0f};
            glm::dvec3 Linear{0.0};
            glm::dvec3 Angular{0.0};
        };

        void PushExternalChanges(Scene& scene, entt::entity entity, physx::PxRigidActor* actor);
        void StepPhysics(float deltaTime);
        void WriteBackTransforms(Scene& scene);
        void ApplyDebugVisualization(bool enabled);
        void CaptureDebugLines();
        [[nodiscard]] physx::PxRigidActor* CreateActor(entt::entity entity, const RigidBodyComponent& body,
                                                       const ColliderComponent& collider,
                                                       const TransformComponent& transform, const MeshComponent* mesh);
        void SeedVelocity(entt::entity entity, Scene& scene, physx::PxRigidActor* actor);
        [[nodiscard]] physx::PxConvexMesh* CookConvexMesh(const std::vector<glm::vec3>& points, double metresPerUnit);
        void DestroyActor(physx::PxRigidActor* actor);
        [[nodiscard]] std::uint64_t GuidForActor(physx::PxRigidActor* actor) const;

        std::shared_ptr<EventDispatcher> m_Dispatcher;
        std::shared_ptr<Logger> m_Logger;

        bool m_RuntimeHeld = false;
        physx::PxPhysics* m_Physics = nullptr;
        physx::PxDefaultCpuDispatcher* m_CpuDispatcher = nullptr;
        physx::PxScene* m_Scene = nullptr;
        physx::PxMaterial* m_DefaultMaterial = nullptr;

        std::unordered_map<entt::entity, physx::PxRigidActor*> m_ActorsByEntity;
        std::unordered_map<physx::PxRigidActor*, entt::entity> m_EntitiesByActor;
        std::unordered_map<entt::entity, AppliedState> m_Applied;
        Scene* m_CurrentScene = nullptr;
        Scene* m_Bound = nullptr;
        std::vector<PhysicsCommand> m_Commands;
        std::unordered_map<entt::entity, KinematicTarget> m_KinematicTargets;
        std::map<std::uint32_t, JointRecord> m_Joints;
        std::vector<std::pair<std::uint32_t, JointSpec>> m_PendingJoints;
        std::uint32_t m_NextJoint = 1;
        glm::dvec3 m_OriginM{0.0};
        bool m_CaptureEvents = false;
        PhysicsEventBatch m_Events;
        std::atomic<bool> m_DebugVisualizationEnabled{false};
        bool m_DebugVisualizationApplied = false;
        mutable std::mutex m_DebugLinesMutex;
        std::vector<physx::PxDebugLine> m_DebugLinesSnapshot;
    };
}

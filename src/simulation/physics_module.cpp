module GPP.Simulation;

import :PhysicsModule;
import std;

namespace GPP
{
    namespace
    {
        physx::PxVec3 ToPhysXPosition(const glm::dvec3& worldM, const glm::dvec3& originM)
        {
            const glm::dvec3 local = worldM - originM;
            return {static_cast<float>(local.x), static_cast<float>(local.y), static_cast<float>(local.z)};
        }

        glm::dvec3 FromPhysXPosition(const physx::PxVec3& local, const glm::dvec3& originM)
        {
            return originM + glm::dvec3(local.x, local.y, local.z);
        }

        physx::PxVec3 ToPhysXVector(const glm::dvec3& v)
        {
            return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)};
        }

        glm::dvec3 FromPhysXVector(const physx::PxVec3& v) { return {v.x, v.y, v.z}; }

        int ShapeMaterial(const physx::PxShape* shape)
        {
            return shape ? static_cast<int>(reinterpret_cast<std::uintptr_t>(shape->userData)) : 0;
        }

        struct SharedPhysX
        {
            physx::PxDefaultAllocator Allocator;
            physx::PxDefaultErrorCallback ErrorCallback;
            physx::PxFoundation* Foundation = nullptr;
            physx::PxPhysics* Physics = nullptr;
        };

        std::mutex g_SharedPhysXMutex;
        SharedPhysX* g_SharedPhysX = nullptr;
        int g_SharedPhysXRefs = 0;

        physx::PxPhysics* AcquirePhysX()
        {
            std::scoped_lock lock(g_SharedPhysXMutex);
            if (!g_SharedPhysX)
            {
                auto shared = std::make_unique<SharedPhysX>();
                shared->Foundation = PxCreateFoundation(physx::PxPhysicsVersion, shared->Allocator, shared->ErrorCallback);
                if (!shared->Foundation)
                {
                    throw std::runtime_error("PhysicsSimulationModule: PxCreateFoundation failed.");
                }
                shared->Physics = PxCreatePhysics(physx::PxPhysicsVersion, *shared->Foundation, physx::PxTolerancesScale());
                if (!shared->Physics)
                {
                    shared->Foundation->release();
                    throw std::runtime_error("PhysicsSimulationModule: PxCreatePhysics failed.");
                }
                g_SharedPhysX = shared.release();
            }
            ++g_SharedPhysXRefs;
            return g_SharedPhysX->Physics;
        }

        void ReleasePhysX()
        {
            std::scoped_lock lock(g_SharedPhysXMutex);
            if (--g_SharedPhysXRefs > 0) return;
            g_SharedPhysX->Physics->release();
            g_SharedPhysX->Foundation->release();
            delete g_SharedPhysX;
            g_SharedPhysX = nullptr;
        }

        float ToPhysXScalar(const double value) { return static_cast<float>(value); }

        physx::PxTransform IdentityPose() { return physx::PxTransform(physx::PxVec3(0.0f), physx::PxQuat(0.0f, 0.0f, 0.0f, 1.0f)); }

        bool LayersCollide(const physx::PxFilterData& a, const physx::PxFilterData& b)
        {
            return (a.word0 & b.word1) != 0 && (b.word0 & a.word1) != 0;
        }

        physx::PxQuat QuatFromTo(const physx::PxVec3& from, const physx::PxVec3& to)
        {
            const physx::PxVec3 axis = from.cross(to);
            const float w = 1.0f + from.dot(to);
            if (w < 1e-6f) return physx::PxQuat(0.0f, 0.0f, 1.0f, 0.0f);
            physx::PxQuat q(axis.x, axis.y, axis.z, w);
            q.normalize();
            return q;
        }

        physx::PxFilterFlags DefaultFilterShader(
            physx::PxFilterObjectAttributes attributes0, physx::PxFilterData filterData0,
            physx::PxFilterObjectAttributes attributes1, physx::PxFilterData filterData1,
            physx::PxPairFlags& pairFlags, const void*, physx::PxU32)
        {
            if (!LayersCollide(filterData0, filterData1)) return physx::PxFilterFlag::eKILL;
            if (physx::PxFilterObjectIsTrigger(attributes0) || physx::PxFilterObjectIsTrigger(attributes1))
            {
                pairFlags = physx::PxPairFlag::eTRIGGER_DEFAULT;
                return physx::PxFilterFlag::eDEFAULT;
            }
            pairFlags = physx::PxPairFlag::eCONTACT_DEFAULT | physx::PxPairFlag::eNOTIFY_TOUCH_FOUND |
                physx::PxPairFlag::eNOTIFY_TOUCH_LOST | physx::PxPairFlag::eNOTIFY_CONTACT_POINTS |
                physx::PxPairFlag::ePRE_SOLVER_VELOCITY;
            return physx::PxFilterFlag::eDEFAULT;
        }
    }

    PhysicsSimulationModule::PhysicsSimulationModule(std::shared_ptr<EventDispatcher> dispatcher,
                                                     std::shared_ptr<Logger> logger)
        : m_Dispatcher(std::move(dispatcher)), m_Logger(std::move(logger))
    {
    }

    PhysicsSimulationModule::~PhysicsSimulationModule()
    {
        if (m_Scene) { m_Scene->release(); m_Scene = nullptr; }
        if (m_CpuDispatcher) { m_CpuDispatcher->release(); m_CpuDispatcher = nullptr; }
        if (m_RuntimeHeld) { ReleasePhysX(); m_RuntimeHeld = false; m_Physics = nullptr; }
    }

    void PhysicsSimulationModule::OnInit(Scene& scene)
    {
        if (!m_RuntimeHeld)
        {
            m_Physics = AcquirePhysX();
            m_RuntimeHeld = true;
        }
        m_CpuDispatcher = physx::PxDefaultCpuDispatcherCreate(2);

        physx::PxSceneDesc sceneDesc(m_Physics->getTolerancesScale());
        sceneDesc.gravity = physx::PxVec3(0.0f, 0.0f, 0.0f);
        sceneDesc.cpuDispatcher = m_CpuDispatcher;
        sceneDesc.filterShader = DefaultFilterShader;
        sceneDesc.simulationEventCallback = this;
        m_Scene = m_Physics->createScene(sceneDesc);
        if (!m_Scene)
        {
            throw std::runtime_error("PhysicsSimulationModule: createScene failed.");
        }

        glm::dvec3 sum{0.0};
        std::size_t count = 0;
        for (auto [entity, body, collider, transform] :
             scene.Registry().view<const RigidBodyComponent, const ColliderComponent, const TransformComponent>().each())
        {
            sum += transform.Position;
            ++count;
        }
        m_OriginM = count > 0 ? sum / static_cast<double>(count) : glm::dvec3(0.0);

        m_Bound = &scene;
        m_NextJoint = 1;
        SyncActors(scene);
        if (m_Logger)
        {
            m_Logger->Info("PhysicsSimulationModule initialized ({} actor(s)).", m_ActorsByEntity.size());
        }
    }

    void PhysicsSimulationModule::OnTick(Scene& scene, const float deltaTime)
    {
        if (!m_Scene) return;
        m_CurrentScene = &scene;

        const bool debugViz = m_DebugVisualizationEnabled.load(std::memory_order_relaxed);
        if (debugViz != m_DebugVisualizationApplied)
        {
            ApplyDebugVisualization(debugViz);
            m_DebugVisualizationApplied = debugViz;
        }

        m_Events = {};
        ReapBrokenJoints();
        SyncActors(scene);
        InstantiatePendingJoints();
        ApplyCommands(scene);
        StepPhysics(static_cast<float>(static_cast<double>(deltaTime) * scene.Metadata().TimeScale));
        std::ranges::stable_sort(m_Events.Contacts, {},
                                 [](const PhysicsContactEvent& e) { return std::pair(e.GuidA, e.GuidB); });
        std::ranges::stable_sort(m_Events.Triggers, {},
                                 [](const PhysicsTriggerEvent& e) { return std::pair(e.TriggerGuid, e.OtherGuid); });

        if (debugViz)
        {
            CaptureDebugLines();
        }
        else
        {
            std::scoped_lock lock(m_DebugLinesMutex);
            m_DebugLinesSnapshot.clear();
        }

        WriteBackTransforms(scene);
        m_CurrentScene = nullptr;
    }

    void PhysicsSimulationModule::ApplyDebugVisualization(bool enabled)
    {
        m_Scene->setVisualizationParameter(physx::PxVisualizationParameter::eSCALE, enabled ? 1.0f : 0.0f);
        if (!enabled) return;

        static constexpr physx::PxVisualizationParameter::Enum kShapeRelatedParams[] = {
            physx::PxVisualizationParameter::eWORLD_AXES,
            physx::PxVisualizationParameter::eBODY_AXES,
            physx::PxVisualizationParameter::eBODY_MASS_AXES,
            physx::PxVisualizationParameter::eBODY_LIN_VELOCITY,
            physx::PxVisualizationParameter::eBODY_ANG_VELOCITY,
            physx::PxVisualizationParameter::eCONTACT_POINT,
            physx::PxVisualizationParameter::eCONTACT_NORMAL,
            physx::PxVisualizationParameter::eCONTACT_ERROR,
            physx::PxVisualizationParameter::eCONTACT_IMPULSE,
            physx::PxVisualizationParameter::eFRICTION_POINT,
            physx::PxVisualizationParameter::eFRICTION_NORMAL,
            physx::PxVisualizationParameter::eFRICTION_IMPULSE,
            physx::PxVisualizationParameter::eACTOR_AXES,
            physx::PxVisualizationParameter::eCOLLISION_AABBS,
            physx::PxVisualizationParameter::eCOLLISION_SHAPES,
            physx::PxVisualizationParameter::eCOLLISION_AXES,
            physx::PxVisualizationParameter::eCOLLISION_COMPOUNDS,
            physx::PxVisualizationParameter::eCOLLISION_FNORMALS,
            physx::PxVisualizationParameter::eCOLLISION_EDGES,
            physx::PxVisualizationParameter::eCOLLISION_STATIC,
            physx::PxVisualizationParameter::eCOLLISION_DYNAMIC,
        };
        for (const auto param : kShapeRelatedParams)
        {
            m_Scene->setVisualizationParameter(param, 1.0f);
        }
    }

    void PhysicsSimulationModule::CaptureDebugLines()
    {
        const auto& renderBuffer = m_Scene->getRenderBuffer();
        const auto* lines = renderBuffer.getLines();
        std::vector<physx::PxDebugLine> snapshot(lines, lines + renderBuffer.getNbLines());

        std::scoped_lock lock(m_DebugLinesMutex);
        m_DebugLinesSnapshot = std::move(snapshot);
    }

    void PhysicsSimulationModule::OnShutdown(Scene&)
    {
        for (const auto& [entity, actor] : m_ActorsByEntity)
        {
            DestroyActor(actor);
        }
        m_ActorsByEntity.clear();
        m_EntitiesByActor.clear();
        m_Applied.clear();
        m_Joints.clear();
        m_PendingJoints.clear();
        m_Commands.clear();
        m_KinematicTargets.clear();
        m_Bound = nullptr;

        if (m_Scene) { m_Scene->release(); m_Scene = nullptr; }
        if (m_CpuDispatcher) { m_CpuDispatcher->release(); m_CpuDispatcher = nullptr; }
        if (m_RuntimeHeld) { ReleasePhysX(); m_RuntimeHeld = false; m_Physics = nullptr; }
    }

    void PhysicsSimulationModule::SyncActors(Scene& scene)
    {
        std::unordered_set<entt::entity> current;
        auto view = scene.Registry().view<RigidBodyComponent, ColliderComponent, TransformComponent>();
        for (const auto entity : view)
        {
            current.insert(entity);
            if (const auto existing = m_ActorsByEntity.find(entity); existing != m_ActorsByEntity.end())
            {
                PushExternalChanges(scene, entity, existing->second);
                continue;
            }

            const auto& body = view.get<RigidBodyComponent>(entity);
            const auto& collider = view.get<ColliderComponent>(entity);
            const auto& transform = view.get<TransformComponent>(entity);
            if (auto* actor = CreateActor(entity, body, collider, transform, scene.Registry().try_get<MeshComponent>(entity)))
            {
                SeedVelocity(entity, scene, actor);
                AppliedState applied{.Position = transform.Position, .Rotation = transform.Rotation};
                if (const auto* velocity = scene.Registry().try_get<VelocityComponent>(entity))
                {
                    applied.Linear = velocity->Linear;
                    applied.Angular = velocity->Angular;
                }
                m_Applied.insert_or_assign(entity, applied);
                m_ActorsByEntity.emplace(entity, actor);
                m_EntitiesByActor.emplace(actor, entity);
            }
        }

        for (auto it = m_ActorsByEntity.begin(); it != m_ActorsByEntity.end();)
        {
            if (current.contains(it->first) && scene.IsValid(it->first))
            {
                ++it;
                continue;
            }
            DestroyActor(it->second);
            m_EntitiesByActor.erase(it->second);
            m_Applied.erase(it->first);
            it = m_ActorsByEntity.erase(it);
        }
    }

    void PhysicsSimulationModule::PushExternalChanges(Scene& scene, const entt::entity entity,
                                                      physx::PxRigidActor* actor)
    {
        auto& applied = m_Applied[entity];
        auto* dynamic = actor->is<physx::PxRigidDynamic>();
        const bool simulated = dynamic && !(dynamic->getRigidBodyFlags() & physx::PxRigidBodyFlag::eKINEMATIC);

        if (const auto* transform = scene.Registry().try_get<TransformComponent>(entity);
            transform && (transform->Position != applied.Position || transform->Rotation != applied.Rotation))
        {
            const auto length = glm::length(transform->Rotation);
            if (std::isfinite(length) && length > 1e-6f)
            {
                const auto q = transform->Rotation / length;
                actor->setGlobalPose(physx::PxTransform(
                    ToPhysXPosition(transform->Position, m_OriginM),
                    physx::PxQuat(q.x, q.y, q.z, q.w)));
                if (simulated)
                {
                    if (!scene.Registry().all_of<VelocityComponent>(entity))
                    {
                        dynamic->setLinearVelocity(physx::PxVec3(0.0f));
                        dynamic->setAngularVelocity(physx::PxVec3(0.0f));
                    }
                    dynamic->wakeUp();
                }
            }
            applied.Position = transform->Position;
            applied.Rotation = transform->Rotation;
        }

        if (!simulated) return;
        if (const auto* velocity = scene.Registry().try_get<VelocityComponent>(entity);
            velocity && (velocity->Linear != applied.Linear || velocity->Angular != applied.Angular))
        {
            dynamic->setLinearVelocity(ToPhysXVector(velocity->Linear));
            dynamic->setAngularVelocity(ToPhysXVector(velocity->Angular));
            dynamic->wakeUp();
            applied.Linear = velocity->Linear;
            applied.Angular = velocity->Angular;
        }
    }

    physx::PxConvexMesh* PhysicsSimulationModule::CookConvexMesh(const std::vector<glm::vec3>& points, const double metresPerUnit)
    {
        if (points.size() < 4)
        {
            if (m_Logger)
            {
                m_Logger->Warn("PhysicsSimulationModule: ConvexMesh collider needs at least 4 points "
                               "(got {}); skipping entity.", points.size());
            }
            return nullptr;
        }

        std::vector<physx::PxVec3> pxPoints;
        pxPoints.reserve(points.size());
        for (const auto& point : points)
        {
            pxPoints.emplace_back(ToPhysXScalar(point.x * metresPerUnit), ToPhysXScalar(point.y * metresPerUnit),
                                  ToPhysXScalar(point.z * metresPerUnit));
        }

        physx::PxConvexMeshDesc desc;
        desc.points.count = static_cast<physx::PxU32>(pxPoints.size());
        desc.points.stride = sizeof(physx::PxVec3);
        desc.points.data = pxPoints.data();
        desc.flags = physx::PxConvexFlag::eCOMPUTE_CONVEX;

        const physx::PxCookingParams cookingParams(m_Physics->getTolerancesScale());
        auto* convexMesh = PxCreateConvexMesh(cookingParams, desc, m_Physics->getPhysicsInsertionCallback());
        if (!convexMesh && m_Logger)
        {
            m_Logger->Error("PhysicsSimulationModule: failed to cook a convex mesh from {} points.",
                           points.size());
        }
        return convexMesh;
    }

    physx::PxRigidActor* PhysicsSimulationModule::CreateActor(
        entt::entity, const RigidBodyComponent& body, const ColliderComponent& collider,
        const TransformComponent& transform, const MeshComponent* mesh)
    {
        physx::PxConvexMesh* convexMesh = nullptr;
        if (collider.Shape == ColliderShape::ConvexMesh)
        {
            convexMesh = CookConvexMesh(collider.ConvexHullPoints, mesh ? mesh->MetresPerUnit : 1.0);
            if (!convexMesh) return nullptr;
        }

        const physx::PxTransform pxTransform(
            ToPhysXPosition(transform.Position, m_OriginM),
            physx::PxQuat(transform.Rotation.x, transform.Rotation.y, transform.Rotation.z,
                         transform.Rotation.w));

        physx::PxRigidActor* actor = nullptr;
        if (body.Type == RigidBodyType::Static)
        {
            actor = m_Physics->createRigidStatic(pxTransform);
        }
        else
        {
            auto* dynamic = m_Physics->createRigidDynamic(pxTransform);
            if (dynamic && body.Type == RigidBodyType::Kinematic)
            {
                dynamic->setRigidBodyFlag(physx::PxRigidBodyFlag::eKINEMATIC, true);
            }
            actor = dynamic;
        }
        if (!actor)
        {
            if (m_Logger) m_Logger->Error("PhysicsSimulationModule: failed to create a PxRigidActor.");
            return nullptr;
        }

        auto* material = m_Physics->createMaterial(collider.StaticFriction, collider.DynamicFriction,
                                                   collider.Restitution);
        physx::PxShape* shape = nullptr;
        switch (collider.Shape)
        {
        case ColliderShape::Sphere:
            shape = physx::PxRigidActorExt::createExclusiveShape(
                *actor, physx::PxSphereGeometry(ToPhysXScalar(collider.Radius)), *material);
            break;
        case ColliderShape::Capsule:
            shape = physx::PxRigidActorExt::createExclusiveShape(
                *actor, physx::PxCapsuleGeometry(ToPhysXScalar(collider.Radius), ToPhysXScalar(collider.HalfHeight)), *material);
            break;
        case ColliderShape::Plane:
            shape = physx::PxRigidActorExt::createExclusiveShape(
                *actor, physx::PxPlaneGeometry(), *material);
            break;
        case ColliderShape::ConvexMesh:
            shape = physx::PxRigidActorExt::createExclusiveShape(
                *actor, physx::PxConvexMeshGeometry(convexMesh), *material);
            convexMesh->release(); // the shape/geometry holds its own reference now
            break;
        case ColliderShape::Box:
        default:
            shape = physx::PxRigidActorExt::createExclusiveShape(
                *actor,
                physx::PxBoxGeometry(ToPhysXScalar(collider.HalfExtents.x), ToPhysXScalar(collider.HalfExtents.y),
                                     ToPhysXScalar(collider.HalfExtents.z)),
                *material);
            break;
        }
        material->release();

        if (shape) shape->userData = reinterpret_cast<void*>(static_cast<std::uintptr_t>(std::max(collider.MaterialId, 0)));
        if (shape)
        {
            const std::uint32_t layerBit = 1u << std::clamp(collider.Layer, 0, 31);
            shape->setSimulationFilterData(physx::PxFilterData(layerBit, static_cast<std::uint32_t>(collider.Mask), 0, 0));
            shape->setQueryFilterData(physx::PxFilterData(layerBit, 0, 0, 0));
        }

        if (!shape)
        {
            if (m_Logger) m_Logger->Error("PhysicsSimulationModule: failed to create a collider shape.");
            actor->release();
            return nullptr;
        }

        if (collider.IsTrigger)
        {
            shape->setFlag(physx::PxShapeFlag::eSCENE_QUERY_SHAPE, false);
            shape->setFlag(physx::PxShapeFlag::eSIMULATION_SHAPE, false);
            shape->setFlag(physx::PxShapeFlag::eTRIGGER_SHAPE, true);
        }
        else
        {
            shape->setFlag(physx::PxShapeFlag::eTRIGGER_SHAPE, false);
            shape->setFlag(physx::PxShapeFlag::eSIMULATION_SHAPE, true);
        }

        if (auto* dynamic = actor->is<physx::PxRigidDynamic>())
        {
            dynamic->setLinearDamping(body.LinearDamping);
            dynamic->setAngularDamping(body.AngularDamping);
            if (body.Type == RigidBodyType::Dynamic)
            {
                physx::PxRigidBodyExt::setMassAndUpdateInertia(*dynamic, ToPhysXScalar(std::clamp(body.Mass, 1e-4, 1e37)));
            }
        }

        m_Scene->addActor(*actor);
        return actor;
    }

    void PhysicsSimulationModule::SeedVelocity(entt::entity entity, Scene& scene, physx::PxRigidActor* actor)
    {
        auto* dynamic = actor->is<physx::PxRigidDynamic>();
        if (!dynamic || (dynamic->getRigidBodyFlags() & physx::PxRigidBodyFlag::eKINEMATIC))
        {
            return;
        }
        if (const auto* velocity = scene.Registry().try_get<VelocityComponent>(entity))
        {
            dynamic->setLinearVelocity(ToPhysXVector(velocity->Linear));
            dynamic->setAngularVelocity(ToPhysXVector(velocity->Angular));
        }
    }

    void PhysicsSimulationModule::DestroyActor(physx::PxRigidActor* actor)
    {
        if (!actor)
        {
            return;
        }
        ReleaseJoints(actor);
        if (m_Scene)
        {
            m_Scene->removeActor(*actor);
        }
        actor->release();
    }

    void PhysicsSimulationModule::StepPhysics(const float deltaTime)
    {
        if (!m_Scene || deltaTime <= 0.0f)
        {
            return;
        }
        m_Scene->simulate(deltaTime);
        m_Scene->fetchResults(true);
    }

    void PhysicsSimulationModule::WriteBackTransforms(Scene& scene)
    {
        for (const auto& [entity, actor] : m_ActorsByEntity)
        {
            auto* dynamic = actor->is<physx::PxRigidDynamic>();
            if (dynamic && (dynamic->getRigidBodyFlags() & physx::PxRigidBodyFlag::eKINEMATIC))
            {
                if (const auto target = m_KinematicTargets.find(entity); target != m_KinematicTargets.end() && scene.IsValid(entity))
                {
                    if (auto* transform = scene.Registry().try_get<TransformComponent>(entity))
                    {
                        transform->Position = target->second.Position;
                        transform->Rotation = target->second.Rotation;
                        auto& applied = m_Applied[entity];
                        applied.Position = transform->Position;
                        applied.Rotation = transform->Rotation;
                        scene.MarkDirty(entity);
                    }
                }
                continue;
            }
            if (!dynamic)
            {
                continue;
            }
            if (!scene.IsValid(entity))
            {
                continue;
            }
            auto* transform = scene.Registry().try_get<TransformComponent>(entity);
            if (!transform)
            {
                continue;
            }
            auto& applied = m_Applied[entity];
            const auto pose = actor->getGlobalPose();
            transform->Position = FromPhysXPosition(pose.p, m_OriginM);
            transform->Rotation = {pose.q.w, pose.q.x, pose.q.y, pose.q.z};
            bool changed = transform->Position != applied.Position || transform->Rotation != applied.Rotation;
            applied.Position = transform->Position;
            applied.Rotation = transform->Rotation;

            if (auto* velocity = scene.Registry().try_get<VelocityComponent>(entity))
            {
                const auto linear = dynamic->getLinearVelocity();
                const auto angular = dynamic->getAngularVelocity();
                velocity->Linear = FromPhysXVector(linear);
                velocity->Angular = FromPhysXVector(angular);
                changed = changed || velocity->Linear != applied.Linear || velocity->Angular != applied.Angular;
                applied.Linear = velocity->Linear;
                applied.Angular = velocity->Angular;
            }
            if (changed) scene.MarkDirty(entity);
        }
        m_KinematicTargets.clear();
    }

    std::uint64_t PhysicsSimulationModule::GuidForActor(physx::PxRigidActor* actor) const
    {
        if (!actor || !m_CurrentScene)
        {
            return 0;
        }
        const auto it = m_EntitiesByActor.find(actor);
        if (it == m_EntitiesByActor.end())
        {
            return 0;
        }
        return m_CurrentScene->GuidOf(it->second);
    }

    void PhysicsSimulationModule::onContact(const physx::PxContactPairHeader& pairHeader,
                                            const physx::PxContactPair* pairs, const physx::PxU32 count)
    {
        if (!m_CurrentScene || (!m_Dispatcher && !m_CaptureEvents))
        {
            return;
        }
        constexpr physx::PxU32 kMaxPoints = 16;
        std::array<physx::PxContactPairPoint, kMaxPoints> points;
        glm::dvec3 pointSum{0.0};
        glm::dvec3 normalSum{0.0};
        double impulse = 0.0;
        std::size_t pointCount = 0;
        int material0 = 0;
        int material1 = 0;
        bool touchFound = false;
        for (physx::PxU32 i = 0; i < count; ++i)
        {
            if (!(pairs[i].events & physx::PxPairFlag::eNOTIFY_TOUCH_FOUND))
            {
                continue;
            }
            if (!touchFound)
            {
                material0 = ShapeMaterial(pairs[i].shapes[0]);
                material1 = ShapeMaterial(pairs[i].shapes[1]);
            }
            touchFound = true;
            const auto extracted = pairs[i].extractContacts(points.data(), kMaxPoints);
            for (physx::PxU32 k = 0; k < extracted; ++k)
            {
                pointSum += FromPhysXPosition(points[k].position, m_OriginM);
                normalSum += FromPhysXVector(points[k].normal);
                impulse += points[k].impulse.magnitude();
                ++pointCount;
            }
        }
        if (!touchFound)
        {
            return;
        }

        auto guid0 = GuidForActor(static_cast<physx::PxRigidActor*>(pairHeader.actors[0]));
        auto guid1 = GuidForActor(static_cast<physx::PxRigidActor*>(pairHeader.actors[1]));
        if (guid0 == 0 || guid1 == 0)
        {
            return;
        }

        glm::dvec3 velocity0{0.0};
        glm::dvec3 velocity1{0.0};
        physx::PxContactPairExtraDataIterator extra(pairHeader.extraDataStream, pairHeader.extraDataStreamSize);
        while (extra.nextItemSet())
        {
            if (extra.preSolverVelocity)
            {
                velocity0 = FromPhysXVector(extra.preSolverVelocity->linearVelocity[0]);
                velocity1 = FromPhysXVector(extra.preSolverVelocity->linearVelocity[1]);
                break;
            }
        }

        PhysicsContactEvent event;
        event.Impulse = impulse;
        // normal: shape 1 to 0
        event.Normal = glm::length(normalSum) > 0.0 ? glm::normalize(normalSum) : glm::dvec3(0.0, 1.0, 0.0);
        if (pointCount > 0) event.Point = pointSum / static_cast<double>(pointCount);
        else
        {
            const auto pose0 = static_cast<physx::PxRigidActor*>(pairHeader.actors[0])->getGlobalPose().p;
            const auto pose1 = static_cast<physx::PxRigidActor*>(pairHeader.actors[1])->getGlobalPose().p;
            event.Point = FromPhysXPosition((pose0 + pose1) * 0.5f, m_OriginM);
        }
        event.RelativeVelocity = velocity0 - velocity1;
        event.GuidA = guid0;
        event.GuidB = guid1;
        event.MaterialA = material0;
        event.MaterialB = material1;
        if (guid0 > guid1)
        {
            std::swap(event.GuidA, event.GuidB);
            std::swap(event.MaterialA, event.MaterialB);
            event.Normal = -event.Normal;
            event.RelativeVelocity = -event.RelativeVelocity;
        }
        if (m_Dispatcher) m_Dispatcher->Publish(event);
        if (m_CaptureEvents) m_Events.Contacts.push_back(event);
    }

    void PhysicsSimulationModule::onTrigger(physx::PxTriggerPair* pairs, const physx::PxU32 count)
    {
        if (!m_CurrentScene || (!m_Dispatcher && !m_CaptureEvents))
        {
            return;
        }
        for (physx::PxU32 i = 0; i < count; ++i)
        {
            if (pairs[i].flags & (physx::PxTriggerPairFlag::eREMOVED_SHAPE_TRIGGER |
                                   physx::PxTriggerPairFlag::eREMOVED_SHAPE_OTHER))
            {
                continue;
            }
            const auto triggerGuid = GuidForActor(static_cast<physx::PxRigidActor*>(pairs[i].triggerActor));
            const auto otherGuid = GuidForActor(static_cast<physx::PxRigidActor*>(pairs[i].otherActor));
            if (triggerGuid == 0 || otherGuid == 0)
            {
                continue;
            }
            const PhysicsTriggerEvent event{triggerGuid, otherGuid, pairs[i].status == physx::PxPairFlag::eNOTIFY_TOUCH_FOUND};
            if (m_Dispatcher) m_Dispatcher->Publish(event);
            if (m_CaptureEvents) m_Events.Triggers.push_back(event);
        }
    }

    bool PhysicsSimulationModule::PrepareQuery()
    {
        if (!m_Scene || !m_Bound) return false;
        SyncActors(*m_Bound);
        InstantiatePendingJoints();
        m_Scene->flushQueryUpdates();
        return true;
    }

    std::optional<PhysicsHit> PhysicsSimulationModule::ToHit(physx::PxRigidActor* actor, const physx::PxVec3& position,
                                                              const physx::PxVec3& normal, const float distance) const
    {
        const auto it = m_EntitiesByActor.find(actor);
        if (it == m_EntitiesByActor.end()) return std::nullopt;
        PhysicsHit hit;
        hit.Guid = m_Bound->GuidOf(it->second);
        hit.Point = FromPhysXPosition(position, m_OriginM);
        hit.Normal = FromPhysXVector(normal);
        hit.Distance = distance;
        return hit;
    }

    std::optional<PhysicsHit> PhysicsSimulationModule::Raycast(const glm::dvec3& origin, const glm::dvec3& direction,
                                                               const double maxDistance, const std::uint32_t mask)
    {
        const double length = glm::length(direction);
        if (mask == 0 || !(length > 0.0) || !PrepareQuery()) return std::nullopt;
        physx::PxRaycastBuffer buffer;
        const physx::PxQueryFilterData filter(physx::PxFilterData(mask, 0, 0, 0),
                                              physx::PxQueryFlag::eSTATIC | physx::PxQueryFlag::eDYNAMIC);
        if (!m_Scene->raycast(ToPhysXPosition(origin, m_OriginM), ToPhysXVector(direction / length),
                              ToPhysXScalar(maxDistance), buffer, physx::PxHitFlag::eDEFAULT, filter) ||
            !buffer.hasBlock)
        {
            return std::nullopt;
        }
        return ToHit(buffer.block.actor, buffer.block.position, buffer.block.normal, buffer.block.distance);
    }

    std::optional<PhysicsHit> PhysicsSimulationModule::Spherecast(const glm::dvec3& origin, const double radius,
                                                                  const glm::dvec3& direction, const double maxDistance,
                                                                  const std::uint32_t mask)
    {
        const double length = glm::length(direction);
        if (mask == 0 || !(length > 0.0) || !PrepareQuery()) return std::nullopt;
        physx::PxSweepBuffer buffer;
        const physx::PxQueryFilterData filter(physx::PxFilterData(mask, 0, 0, 0),
                                              physx::PxQueryFlag::eSTATIC | physx::PxQueryFlag::eDYNAMIC);
        const physx::PxSphereGeometry sphere(ToPhysXScalar(radius));
        if (!m_Scene->sweep(sphere, physx::PxTransform(ToPhysXPosition(origin, m_OriginM)),
                            ToPhysXVector(direction / length), ToPhysXScalar(maxDistance), buffer,
                            physx::PxHitFlag::eDEFAULT, filter) ||
            !buffer.hasBlock)
        {
            return std::nullopt;
        }
        return ToHit(buffer.block.actor, buffer.block.position, buffer.block.normal, buffer.block.distance);
    }

    std::vector<std::uint64_t> PhysicsSimulationModule::Overlap(const glm::dvec3& center, const double radius,
                                                                const std::uint32_t mask)
    {
        std::vector<std::uint64_t> guids;
        if (mask == 0 || !PrepareQuery()) return guids;
        physx::PxOverlapBufferN<512> buffer;
        const physx::PxQueryFilterData filter(physx::PxFilterData(mask, 0, 0, 0),
                                              physx::PxQueryFlag::eSTATIC | physx::PxQueryFlag::eDYNAMIC |
                                                  physx::PxQueryFlag::eNO_BLOCK);
        const physx::PxSphereGeometry sphere(ToPhysXScalar(radius));
        m_Scene->overlap(sphere, physx::PxTransform(ToPhysXPosition(center, m_OriginM)), buffer, filter);
        for (physx::PxU32 i = 0; i < buffer.getNbTouches(); ++i)
        {
            const auto it = m_EntitiesByActor.find(buffer.getTouch(i).actor);
            if (it != m_EntitiesByActor.end()) guids.push_back(m_Bound->GuidOf(it->second));
        }
        std::ranges::sort(guids);
        guids.erase(std::ranges::unique(guids).begin(), guids.end());
        return guids;
    }

    void PhysicsSimulationModule::ApplyCommands(Scene& scene)
    {
        for (const auto& command : std::exchange(m_Commands, {}))
        {
            const auto entity = scene.FindByGuid(command.Guid);
            const auto found = scene.IsValid(entity) ? m_ActorsByEntity.find(entity) : m_ActorsByEntity.end();
            if (found == m_ActorsByEntity.end()) continue;
            auto* dynamic = found->second->is<physx::PxRigidDynamic>();
            if (!dynamic) continue;
            const bool kinematic = static_cast<bool>(dynamic->getRigidBodyFlags() & physx::PxRigidBodyFlag::eKINEMATIC);
            const auto vector = ToPhysXVector(command.Vector);
            if (command.Kind == PhysicsCommandKind::KinematicTarget)
            {
                if (!kinematic) continue;
                const glm::quat rotation = command.Rotation.value_or(glm::quat(
                    dynamic->getGlobalPose().q.w, dynamic->getGlobalPose().q.x, dynamic->getGlobalPose().q.y,
                    dynamic->getGlobalPose().q.z));
                dynamic->setKinematicTarget(physx::PxTransform(
                    ToPhysXPosition(command.Vector, m_OriginM),
                    physx::PxQuat(rotation.x, rotation.y, rotation.z, rotation.w)));
                m_KinematicTargets[entity] = {command.Vector, rotation};
                continue;
            }
            if (kinematic) continue;
            switch (command.Kind)
            {
            case PhysicsCommandKind::Force: dynamic->addForce(vector, physx::PxForceMode::eFORCE); break;
            case PhysicsCommandKind::Torque: dynamic->addTorque(vector, physx::PxForceMode::eFORCE); break;
            case PhysicsCommandKind::Impulse: dynamic->addForce(vector, physx::PxForceMode::eIMPULSE); break;
            case PhysicsCommandKind::ImpulseAtPoint:
                physx::PxRigidBodyExt::addForceAtPos(*dynamic, vector, ToPhysXPosition(command.Point, m_OriginM),
                                                     physx::PxForceMode::eIMPULSE);
                break;
            case PhysicsCommandKind::LinearVelocity: dynamic->setLinearVelocity(vector); break;
            case PhysicsCommandKind::AngularVelocity: dynamic->setAngularVelocity(vector); break;
            case PhysicsCommandKind::KinematicTarget: break;
            }
        }
    }

    physx::PxJoint* PhysicsSimulationModule::BuildJoint(const JointSpec& spec)
    {
        const auto actorFor = [this](const std::uint64_t guid) -> physx::PxRigidActor*
        {
            if (guid == 0 || !m_Bound) return nullptr;
            const auto entity = m_Bound->FindByGuid(guid);
            const auto it = m_Bound->IsValid(entity) ? m_ActorsByEntity.find(entity) : m_ActorsByEntity.end();
            return it != m_ActorsByEntity.end() ? it->second : nullptr;
        };
        auto* a = actorFor(spec.GuidA);
        auto* b = actorFor(spec.GuidB);
        if (!a || (spec.GuidB != 0 && !b)) return nullptr;

        physx::PxJoint* joint = nullptr;
        switch (spec.Kind)
        {
        case PhysicsJointKind::Fixed: joint = physx::PxFixedJointCreate(*m_Physics, a, spec.FrameA, b, spec.FrameB); break;
        case PhysicsJointKind::Hinge: joint = physx::PxRevoluteJointCreate(*m_Physics, a, spec.FrameA, b, spec.FrameB); break;
        case PhysicsJointKind::Distance:
            if (auto* distance = physx::PxDistanceJointCreate(*m_Physics, a, spec.FrameA, b, spec.FrameB))
            {
                distance->setMaxDistance(spec.Distance);
                distance->setDistanceJointFlag(physx::PxDistanceJointFlag::eMAX_DISTANCE_ENABLED, true);
                if (spec.MinDistance >= 0.0f)
                {
                    distance->setMinDistance(spec.MinDistance);
                    distance->setDistanceJointFlag(physx::PxDistanceJointFlag::eMIN_DISTANCE_ENABLED, true);
                }
                if (spec.Stiffness > 0.0f)
                {
                    distance->setStiffness(spec.Stiffness);
                    distance->setDamping(spec.Damping);
                    distance->setDistanceJointFlag(physx::PxDistanceJointFlag::eSPRING_ENABLED, true);
                }
                joint = distance;
            }
            break;
        }
        if (joint && (spec.BreakForce > 0.0f || spec.BreakTorque > 0.0f))
        {
            joint->setBreakForce(spec.BreakForce > 0.0f ? spec.BreakForce : std::numeric_limits<float>::max(),
                                 spec.BreakTorque > 0.0f ? spec.BreakTorque : std::numeric_limits<float>::max());
        }
        return joint;
    }

    std::expected<std::uint32_t, std::string> PhysicsSimulationModule::CreateJoint(
        const std::uint64_t guidA, const std::uint64_t guidB, const PhysicsJointKind kind, const PhysicsJointParams& params)
    {
        if (!PrepareQuery()) return std::unexpected("physics is not running");
        const auto actorFor = [this](const std::uint64_t guid) -> physx::PxRigidActor*
        {
            const auto entity = m_Bound->FindByGuid(guid);
            const auto it = m_Bound->IsValid(entity) ? m_ActorsByEntity.find(entity) : m_ActorsByEntity.end();
            return it != m_ActorsByEntity.end() ? it->second : nullptr;
        };
        auto* a = actorFor(guidA);
        auto* b = guidB != 0 ? actorFor(guidB) : nullptr;
        if (!a) return std::unexpected("first entity has no active rigid body");
        if (guidB != 0 && !b) return std::unexpected("second entity has no active rigid body");
        const auto simulated = [](physx::PxRigidActor* actor)
        {
            auto* dynamic = actor ? actor->is<physx::PxRigidDynamic>() : nullptr;
            return dynamic && !(dynamic->getRigidBodyFlags() & physx::PxRigidBodyFlag::eKINEMATIC);
        };
        if (!simulated(a) && !simulated(b)) return std::unexpected("a joint needs at least one dynamic body");

        JointSpec spec;
        spec.Kind = kind;
        spec.GuidA = guidA;
        spec.GuidB = guidB;
        spec.Stiffness = ToPhysXScalar(params.Stiffness);
        spec.Damping = ToPhysXScalar(params.Damping);
        spec.BreakForce = ToPhysXScalar(params.BreakForce);
        spec.BreakTorque = ToPhysXScalar(params.BreakTorque);
        spec.MinDistance = params.MinDistance ? ToPhysXScalar(*params.MinDistance) : -1.0f;

        const physx::PxTransform poseA = a->getGlobalPose();
        const physx::PxTransform poseB = b ? b->getGlobalPose() : IdentityPose();
        const physx::PxVec3 anchor = params.Anchor ? ToPhysXPosition(*params.Anchor, m_OriginM) : poseA.p;
        if (kind == PhysicsJointKind::Distance)
        {
            const physx::PxVec3 anchorB = b ? poseB.p : anchor;
            spec.FrameA = IdentityPose();
            spec.FrameB = b ? IdentityPose() : physx::PxTransform(anchorB);
            spec.Distance = params.Distance ? ToPhysXScalar(*params.Distance) : (anchorB - poseA.p).magnitude();
        }
        else
        {
            physx::PxQuat rotation(0.0f, 0.0f, 0.0f, 1.0f);
            if (kind == PhysicsJointKind::Hinge)
            {
                const double length = glm::length(params.Axis);
                if (!(length > 0.0)) return std::unexpected("hinge axis must be a non-zero vector");
                rotation = QuatFromTo(physx::PxVec3(1.0f, 0.0f, 0.0f), ToPhysXVector(params.Axis / length));
            }
            const physx::PxTransform world(anchor, rotation);
            spec.FrameA = poseA.transformInv(world);
            spec.FrameB = b ? poseB.transformInv(world) : world;
        }

        auto* joint = BuildJoint(spec);
        if (!joint) return std::unexpected("the joint could not be created");
        const auto id = m_NextJoint++;
        m_Joints[id] = JointRecord{spec, joint, false};
        return id;
    }

    bool PhysicsSimulationModule::BreakJoint(const std::uint32_t id)
    {
        const auto it = m_Joints.find(id);
        if (it == m_Joints.end()) return false;
        if (it->second.Joint) it->second.Joint->release();
        m_Joints.erase(it);
        return true;
    }

    std::optional<bool> PhysicsSimulationModule::JointBroken(const std::uint32_t id) const
    {
        const auto it = m_Joints.find(id);
        if (it == m_Joints.end()) return std::nullopt;
        return it->second.Broken;
    }

    void PhysicsSimulationModule::onConstraintBreak(physx::PxConstraintInfo* constraints, const physx::PxU32 count)
    {
        for (physx::PxU32 i = 0; i < count; ++i)
        {
            for (auto& [id, record] : m_Joints)
            {
                if (static_cast<void*>(record.Joint) == constraints[i].externalReference) record.Broken = true;
            }
        }
    }

    void PhysicsSimulationModule::ReapBrokenJoints()
    {
        for (auto& [id, record] : m_Joints)
        {
            if (record.Broken && record.Joint)
            {
                record.Joint->release();
                record.Joint = nullptr;
            }
        }
    }

    void PhysicsSimulationModule::ReleaseJoints(physx::PxRigidActor* actor)
    {
        for (auto it = m_Joints.begin(); it != m_Joints.end();)
        {
            physx::PxRigidActor* a = nullptr;
            physx::PxRigidActor* b = nullptr;
            if (it->second.Joint) it->second.Joint->getActors(a, b);
            if (a == actor || b == actor)
            {
                it->second.Joint->release();
                it = m_Joints.erase(it);
            }
            else ++it;
        }
    }

    void PhysicsSimulationModule::InstantiatePendingJoints()
    {
        for (auto& [id, spec] : std::exchange(m_PendingJoints, {}))
        {
            if (auto* joint = BuildJoint(spec)) m_Joints[id] = JointRecord{spec, joint, false};
        }
    }

    std::string PhysicsSimulationModule::SaveState() const
    {
        std::vector<std::pair<std::uint32_t, const JointSpec*>> specs;
        for (const auto& [id, record] : m_Joints)
        {
            if (record.Joint && !record.Broken) specs.emplace_back(id, &record.Spec);
        }
        for (const auto& [id, spec] : m_PendingJoints) specs.emplace_back(id, &spec);
        if (specs.empty()) return {};
        std::string out = std::format("{}\n", m_NextJoint);
        for (const auto& [id, spec] : specs)
        {
            const std::array<float, 20> values{
                spec->FrameA.p.x, spec->FrameA.p.y, spec->FrameA.p.z, spec->FrameA.q.x, spec->FrameA.q.y,
                spec->FrameA.q.z, spec->FrameA.q.w, spec->FrameB.p.x, spec->FrameB.p.y, spec->FrameB.p.z,
                spec->FrameB.q.x, spec->FrameB.q.y, spec->FrameB.q.z, spec->FrameB.q.w, spec->Distance,
                spec->MinDistance, spec->Stiffness, spec->Damping, spec->BreakForce, spec->BreakTorque};
            out += std::format("{} {} {} {}", id, static_cast<int>(spec->Kind), spec->GuidA, spec->GuidB);
            for (const float value : values) out += std::format(" {}", value);
            out += '\n';
        }
        return out;
    }

    void PhysicsSimulationModule::LoadState(const std::string& state)
    {
        for (auto& [id, record] : m_Joints)
        {
            if (record.Joint) record.Joint->release();
        }
        m_Joints.clear();
        m_PendingJoints.clear();
        std::istringstream in(state);
        std::string line;
        if (!std::getline(in, line)) return;
        std::istringstream header(line);
        header >> m_NextJoint;
        while (std::getline(in, line))
        {
            std::istringstream fields(line);
            std::uint32_t id = 0;
            int kind = 0;
            JointSpec spec;
            std::array<float, 20> v{};
            fields >> id >> kind >> spec.GuidA >> spec.GuidB;
            for (auto& value : v) fields >> value;
            if (!fields) continue;
            spec.Kind = static_cast<PhysicsJointKind>(kind);
            spec.FrameA = physx::PxTransform(physx::PxVec3(v[0], v[1], v[2]), physx::PxQuat(v[3], v[4], v[5], v[6]));
            spec.FrameB = physx::PxTransform(physx::PxVec3(v[7], v[8], v[9]), physx::PxQuat(v[10], v[11], v[12], v[13]));
            spec.Distance = v[14];
            spec.MinDistance = v[15];
            spec.Stiffness = v[16];
            spec.Damping = v[17];
            spec.BreakForce = v[18];
            spec.BreakTorque = v[19];
            m_PendingJoints.emplace_back(id, spec);
        }
    }
}

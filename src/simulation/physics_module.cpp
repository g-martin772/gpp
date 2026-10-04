module GPP.Simulation;

import :PhysicsModule;
import std;

namespace GPP
{
    namespace
    {
        physx::PxFilterFlags DefaultFilterShader(
            physx::PxFilterObjectAttributes attributes0, physx::PxFilterData,
            physx::PxFilterObjectAttributes attributes1, physx::PxFilterData,
            physx::PxPairFlags& pairFlags, const void*, physx::PxU32)
        {
            if (physx::PxFilterObjectIsTrigger(attributes0) || physx::PxFilterObjectIsTrigger(attributes1))
            {
                pairFlags = physx::PxPairFlag::eTRIGGER_DEFAULT;
                return physx::PxFilterFlag::eDEFAULT;
            }
            pairFlags = physx::PxPairFlag::eCONTACT_DEFAULT | physx::PxPairFlag::eNOTIFY_TOUCH_FOUND |
                physx::PxPairFlag::eNOTIFY_TOUCH_LOST;
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
        if (m_Physics) { m_Physics->release(); m_Physics = nullptr; }
        if (m_Foundation) { m_Foundation->release(); m_Foundation = nullptr; }
    }

    void PhysicsSimulationModule::OnInit(Scene& scene)
    {
        m_Foundation = PxCreateFoundation(physx::PxPhysicsVersion, m_Allocator, m_ErrorCallback);
        if (!m_Foundation)
        {
            throw std::runtime_error("PhysicsSimulationModule: PxCreateFoundation failed.");
        }
        m_Physics = PxCreatePhysics(physx::PxPhysicsVersion, *m_Foundation,
                                           physx::PxTolerancesScale());
        if (!m_Physics)
        {
            throw std::runtime_error("PhysicsSimulationModule: PxCreatePhysics failed.");
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

        SyncActors(scene);
        if (m_Logger)
        {
            m_Logger->Info("PhysicsSimulationModule initialized ({} actor(s)).", m_ActorsByEntity.size());
        }
    }

    void PhysicsSimulationModule::OnTick(Scene& scene, const float deltaTime)
    {
        m_CurrentScene = &scene;
        SyncActors(scene);
        StepPhysics(deltaTime);
        WriteBackTransforms(scene);
        m_CurrentScene = nullptr;
    }

    void PhysicsSimulationModule::OnShutdown(Scene&)
    {
        for (const auto& [entity, actor] : m_ActorsByEntity)
        {
            DestroyActor(actor);
        }
        m_ActorsByEntity.clear();
        m_EntitiesByActor.clear();

        if (m_Scene) { m_Scene->release(); m_Scene = nullptr; }
        if (m_CpuDispatcher) { m_CpuDispatcher->release(); m_CpuDispatcher = nullptr; }
        if (m_Physics) { m_Physics->release(); m_Physics = nullptr; }
        if (m_Foundation) { m_Foundation->release(); m_Foundation = nullptr; }
    }

    void PhysicsSimulationModule::SyncActors(Scene& scene)
    {
        std::unordered_set<entt::entity> current;
        auto view = scene.Registry().view<RigidBodyComponent, ColliderComponent, TransformComponent>();
        for (const auto entity : view)
        {
            current.insert(entity);
            if (m_ActorsByEntity.contains(entity))
            {
                continue;
            }

            const auto& body = view.get<RigidBodyComponent>(entity);
            const auto& collider = view.get<ColliderComponent>(entity);
            const auto& transform = view.get<TransformComponent>(entity);
            if (auto* actor = CreateActor(entity, body, collider, transform))
            {
                SeedVelocity(entity, scene, actor);
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
            it = m_ActorsByEntity.erase(it);
        }
    }

    physx::PxConvexMesh* PhysicsSimulationModule::CookConvexMesh(const std::vector<glm::vec3>& points)
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
            pxPoints.emplace_back(point.x, point.y, point.z);
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
        const TransformComponent& transform)
    {
        physx::PxConvexMesh* convexMesh = nullptr;
        if (collider.Shape == ColliderShape::ConvexMesh)
        {
            convexMesh = CookConvexMesh(collider.ConvexHullPoints);
            if (!convexMesh) return nullptr;
        }

        const physx::PxTransform pxTransform(
            physx::PxVec3(transform.Position.x, transform.Position.y, transform.Position.z),
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
                *actor, physx::PxSphereGeometry(collider.Radius), *material);
            break;
        case ColliderShape::Capsule:
            shape = physx::PxRigidActorExt::createExclusiveShape(
                *actor, physx::PxCapsuleGeometry(collider.Radius, collider.HalfHeight), *material);
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
                physx::PxBoxGeometry(collider.HalfExtents.x, collider.HalfExtents.y, collider.HalfExtents.z),
                *material);
            break;
        }
        material->release();

        if (!shape)
        {
            if (m_Logger) m_Logger->Error("PhysicsSimulationModule: failed to create a collider shape.");
            actor->release();
            return nullptr;
        }

        if (collider.IsTrigger)
        {
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
                physx::PxRigidBodyExt::setMassAndUpdateInertia(*dynamic, std::max(body.Mass, 0.0001f));
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
            dynamic->setLinearVelocity(physx::PxVec3(velocity->Linear.x, velocity->Linear.y, velocity->Linear.z));
            dynamic->setAngularVelocity(
                physx::PxVec3(velocity->Angular.x, velocity->Angular.y, velocity->Angular.z));
        }
    }

    void PhysicsSimulationModule::DestroyActor(physx::PxRigidActor* actor)
    {
        if (!actor)
        {
            return;
        }
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
            if (!dynamic || (dynamic->getRigidBodyFlags() & physx::PxRigidBodyFlag::eKINEMATIC))
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
            const auto pose = actor->getGlobalPose();
            transform->Position = {pose.p.x, pose.p.y, pose.p.z};
            transform->Rotation = {pose.q.w, pose.q.x, pose.q.y, pose.q.z};

            if (auto* velocity = scene.Registry().try_get<VelocityComponent>(entity))
            {
                const auto linear = dynamic->getLinearVelocity();
                const auto angular = dynamic->getAngularVelocity();
                velocity->Linear = {linear.x, linear.y, linear.z};
                velocity->Angular = {angular.x, angular.y, angular.z};
            }
        }
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
        if (!m_CurrentScene || !m_Dispatcher)
        {
            return;
        }
        bool touchFound = false;
        for (physx::PxU32 i = 0; i < count; ++i)
        {
            if (pairs[i].events & physx::PxPairFlag::eNOTIFY_TOUCH_FOUND)
            {
                touchFound = true;
                break;
            }
        }
        if (!touchFound)
        {
            return;
        }

        const auto guidA = GuidForActor(static_cast<physx::PxRigidActor*>(pairHeader.actors[0]));
        const auto guidB = GuidForActor(static_cast<physx::PxRigidActor*>(pairHeader.actors[1]));
        if (guidA == 0 || guidB == 0)
        {
            return;
        }
        m_Dispatcher->Publish(PhysicsContactEvent{guidA, guidB});
    }

    void PhysicsSimulationModule::onTrigger(physx::PxTriggerPair* pairs, const physx::PxU32 count)
    {
        if (!m_CurrentScene || !m_Dispatcher)
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
            m_Dispatcher->Publish(PhysicsTriggerEvent{
                triggerGuid, otherGuid, pairs[i].status == physx::PxPairFlag::eNOTIFY_TOUCH_FOUND
            });
        }
    }
}

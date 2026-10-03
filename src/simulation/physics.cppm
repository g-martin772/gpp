module;
#include <PxPhysicsAPI.h>
export module GPP.Simulation:Physics;

export namespace physx
{
    using physx::PxTolerancesScale;
    using physx::PxVec3;
    using physx::PxVec4;
    using physx::PxQuat;
    using physx::PxTransform;
    using physx::PxMat44;
    using physx::PxBounds3;

    using physx::PxFoundation;
    using physx::PxPhysics;
    using physx::PxDefaultAllocator;
    using physx::PxDefaultErrorCallback;
    using physx::PxAllocatorCallback;
    using physx::PxErrorCallback;
    using physx::PxCpuDispatcher;
    using physx::PxDefaultCpuDispatcherCreate;
    using physx::PxDefaultSimulationFilterShader;

    using physx::PxScene;
    using physx::PxSceneDesc;
    using physx::PxSceneFlag;

    using physx::PxActor;
    using physx::PxRigidActor;
    using physx::PxRigidBody;
    using physx::PxRigidStatic;
    using physx::PxRigidDynamic;
    using physx::PxShape;
    using physx::PxShapeFlag;
    using physx::PxMaterial;
    using physx::PxGeometry;
    using physx::PxBoxGeometry;
    using physx::PxSphereGeometry;
    using physx::PxPlaneGeometry;
    using physx::PxCapsuleGeometry;

    using physx::PxCreatePlane;
    using physx::PxCreateStatic;
    using physx::PxCreateDynamic;

    using physx::PxCookingParams;

    inline constexpr physx::PxU32 PxPhysicsVersion = PX_PHYSICS_VERSION;
}

export
{
    using ::PxCreateFoundation;
    using ::PxCreatePhysics;
    using ::PxCookConvexMesh;
    using ::PxCreateConvexMesh;
    using ::PxCookTriangleMesh;
    using ::PxCreateTriangleMesh;
}

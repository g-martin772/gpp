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
    using physx::PxDefaultCpuDispatcher;
    using physx::PxDefaultCpuDispatcherCreate;
    using physx::PxDefaultSimulationFilterShader;

    using physx::PxScene;
    using physx::PxSceneDesc;
    using physx::PxSceneFlag;

    using physx::PxActor;
    using physx::PxActorFlag;
    using physx::PxRigidActor;
    using physx::PxRigidActorExt;
    using physx::PxRigidBody;
    using physx::PxRigidBodyExt;
    using physx::PxRigidBodyFlag;
    using physx::PxRigidStatic;
    using physx::PxRigidDynamic;
    using physx::PxForceMode;
    using physx::PxShape;
    using physx::PxShapeFlag;
    using physx::PxMaterial;
    using physx::PxGeometry;
    using physx::PxBoxGeometry;
    using physx::PxSphereGeometry;
    using physx::PxPlaneGeometry;
    using physx::PxCapsuleGeometry;
    using physx::PxConvexMeshGeometry;

    using physx::PxCreatePlane;
    using physx::PxCreateStatic;
    using physx::PxCreateDynamic;

    using physx::PxCookingParams;
    using physx::PxConvexMesh;
    using physx::PxConvexMeshDesc;
    using physx::PxConvexFlag;
    using physx::PxConvexMeshCookingResult;
    using physx::PxDefaultMemoryOutputStream;
    using physx::PxDefaultMemoryInputData;

    using physx::PxFilterData;
    using physx::PxFilterFlags;
    using physx::PxFilterFlag;
    using physx::PxFilterObjectAttributes;
    using physx::PxFilterObjectIsTrigger;
    using physx::PxPairFlag;
    using physx::PxPairFlags;

    using physx::PxSimulationEventCallback;
    using physx::PxConstraintInfo;
    using physx::PxContactPairHeader;
    using physx::PxContactPair;
    using physx::PxTriggerPair;
    using physx::PxTriggerPairFlag;
    using physx::PxU32;

    using physx::PxPvd;
    using physx::PxPvdTransport;
    using physx::PxPvdInstrumentationFlag;
    using physx::PxCreatePvd;
    using physx::PxDefaultPvdSocketTransportCreate;

    using physx::PxVisualizationParameter;
    using physx::PxRenderBuffer;
    using physx::PxDebugLine;
    using physx::PxDebugPoint;
    using physx::PxDebugTriangle;

    using physx::operator|;
    using physx::operator&;
    using physx::operator~;

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

#pragma once
#include <physx/PxPhysicsAPI.h>
#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include "ECS/Registry.h"
#include "ECS/Entity.h"
#include "ECS/Components/CollisionCallbackComponent.h"

// ─────────────────────────────────────────────────────────────────────────────
// PhysicsSystem
// ─────────────────────────────────────────────────────────────────────────────
class PhysicsSystem
{
public:
    static PhysicsSystem& Get();

    void Init();
    void Shutdown();

    // Called every game frame:
    //   SyncActors     — creates / recreates / destroys PxActors to match ECS
    //   Step           — advances simulation by dt seconds
    //   WriteBack      — copies dynamic actor poses → TransformComponent
    //   DispatchEvents — fires CollisionCallbackComponent callbacks
    void SyncActors     (Registry& registry);
    void Step           (float dt);
    void WriteBack      (Registry& registry);
    void DispatchEvents (Registry& registry);

    // Call when an entity is destroyed so its actor gets cleaned up
    void RemoveEntity(Entity e);
    // Call when an entity's ColliderComponent or RigidbodyComponent is removed
    void MarkDirty   (Entity e);

private:
    PhysicsSystem()  = default;
    ~PhysicsSystem() = default;

    // ── Internal raw event (pre-dispatch) ────────────────────────────────────
    struct RawEvent
    {
        CollisionEventType type    = CollisionEventType::Enter;
        Entity             entityA = NULL_ENTITY;
        Entity             entityB = NULL_ENTITY;
        glm::vec3          point   = {};
        glm::vec3          normal  = {}; // points from B toward A
        bool               trigger = false;
    };

    // ── Per-actor state hash used to detect component changes ────────────────
    struct ActorState
    {
        physx::PxRigidActor* actor   = nullptr;
        bool                 dynamic = false;

        uint32_t shapeHash   = 0;
        float    mass        = 0.0f;
        bool     kinematic   = false;
        bool     useGravity  = true;
    };

    // ── Simulation event callback ────────────────────────────────────────────
    struct ContactCallback : public physx::PxSimulationEventCallback
    {
        std::vector<RawEvent>* events = nullptr;

        void onContact(const physx::PxContactPairHeader& header,
                       const physx::PxContactPair*       pairs,
                       physx::PxU32                      nbPairs) override;

        void onTrigger(physx::PxTriggerPair* pairs,
                       physx::PxU32          nbPairs) override;

        void onConstraintBreak(physx::PxConstraintInfo*, physx::PxU32) override {}
        void onWake  (physx::PxActor**, physx::PxU32) override {}
        void onSleep (physx::PxActor**, physx::PxU32) override {}
        void onAdvance(const physx::PxRigidBody* const*, const physx::PxTransform*, physx::PxU32) override {}
    };

    // ── Helpers ──────────────────────────────────────────────────────────────
    physx::PxShape*    CreateShape  (Registry& registry, Entity e);
    physx::PxTransform EntityPose   (Registry& registry, Entity e);
    uint32_t           ShapeHash    (Registry& registry, Entity e) const;
    void               DestroyActor (Entity e);

    // ── PhysX objects ────────────────────────────────────────────────────────
    physx::PxDefaultAllocator      m_allocator;
    physx::PxDefaultErrorCallback  m_errorCallback;
    physx::PxFoundation*           m_foundation  = nullptr;
    physx::PxPhysics*              m_physics     = nullptr;
    physx::PxDefaultCpuDispatcher* m_dispatcher  = nullptr;
    physx::PxScene*                m_scene       = nullptr;
    physx::PxMaterial*             m_defaultMat  = nullptr;

    ContactCallback              m_contactCallback;
    std::vector<RawEvent>        m_events;

    std::unordered_map<Entity, ActorState> m_actors;

    bool m_initialized = false;
};

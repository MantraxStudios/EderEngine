#pragma once
#include <physx/PxPhysicsAPI.h>
#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include "ECS/Registry.h"
#include "ECS/Entity.h"

// ─────────────────────────────────────────────────────────────────────────────
// Collision event
// ─────────────────────────────────────────────────────────────────────────────
enum class CollisionEventType { Enter, Exit, Stay };

struct CollisionEvent
{
    CollisionEventType type     = CollisionEventType::Enter;
    Entity             entityA  = NULL_ENTITY;
    Entity             entityB  = NULL_ENTITY;
    glm::vec3          point    = {};   // approx. first contact point in world space
    glm::vec3          normal   = {};   // contact normal pointing from B to A
};

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
    //   SyncActors  — creates / recreates / destroys PxActors to match current ECS state
    //   Step        — advances simulation by dt seconds
    //   WriteBack   — copies dynamic actor poses → TransformComponent
    void SyncActors (Registry& registry);
    void Step       (float dt);
    void WriteBack  (Registry& registry);

    // Drain after WriteBack; cleared at the start of the next SyncActors call
    const std::vector<CollisionEvent>& GetCollisionEvents() const { return m_events; }
    void ClearCollisionEvents() { m_events.clear(); }

    // Call when an entity is destroyed so its actor gets cleaned up
    void RemoveEntity(Entity e);
    // Call when an entity's ColliderComponent or RigidbodyComponent is removed
    void MarkDirty   (Entity e);

private:
    PhysicsSystem()  = default;
    ~PhysicsSystem() = default;

    // ── Per-actor state hash used to detect component changes ────────────────
    struct ActorState
    {
        physx::PxRigidActor* actor   = nullptr;
        bool                 dynamic = false; // PxRigidDynamic?

        // snapshot of component values used to detect dirty
        uint32_t shapeHash   = 0;
        float    mass        = 0.0f;
        bool     kinematic   = false;
        bool     useGravity  = true;
    };

    // ── Simulation event callback ────────────────────────────────────────────
    struct ContactCallback : public physx::PxSimulationEventCallback
    {
        std::vector<CollisionEvent>* events = nullptr;

        void onContact(const physx::PxContactPairHeader& header,
                       const physx::PxContactPair*       pairs,
                       physx::PxU32                      nbPairs) override;

        // Unused callbacks (pure virtual — must override)
        void onConstraintBreak(physx::PxConstraintInfo*, physx::PxU32) override {}
        void onWake  (physx::PxActor**, physx::PxU32) override {}
        void onSleep (physx::PxActor**, physx::PxU32) override {}
        void onTrigger(physx::PxTriggerPair*, physx::PxU32) override {}
        void onAdvance(const physx::PxRigidBody* const*, const physx::PxTransform*, physx::PxU32) override {}
    };

    // ── Helpers ──────────────────────────────────────────────────────────────
    physx::PxShape*      CreateShape(Registry& registry, Entity e);
    physx::PxTransform   EntityPose (Registry& registry, Entity e);
    uint32_t             ShapeHash  (Registry& registry, Entity e) const;
    void                 DestroyActor(Entity e);

    // ── PhysX objects ────────────────────────────────────────────────────────
    physx::PxDefaultAllocator      m_allocator;
    physx::PxDefaultErrorCallback  m_errorCallback;
    physx::PxFoundation*           m_foundation  = nullptr;
    physx::PxPhysics*              m_physics     = nullptr;
    physx::PxDefaultCpuDispatcher* m_dispatcher  = nullptr;
    physx::PxScene*                m_scene       = nullptr;
    physx::PxMaterial*             m_defaultMat  = nullptr;  // fallback

    ContactCallback                m_contactCallback;
    std::vector<CollisionEvent>    m_events;

    std::unordered_map<Entity, ActorState> m_actors;

    bool m_initialized = false;
};

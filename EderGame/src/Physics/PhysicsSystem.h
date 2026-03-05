#pragma once
#include <physx/PxPhysicsAPI.h>
#include <physx/characterkinematic/PxCapsuleController.h>
#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include "ECS/Registry.h"
#include "ECS/Entity.h"
#include "ECS/Components/CollisionCallbackComponent.h"
#include "ECS/Components/CharacterControllerComponent.h"

// ─────────────────────────────────────────────────────────────────────────────
// RaycastHit — result returned by PhysicsSystem::Raycast
// ─────────────────────────────────────────────────────────────────────────────
struct RaycastHit
{
    bool      hit      = false;           // true if any actor was struck
    Entity    entity   = NULL_ENTITY;     // entity that was struck
    float     distance = 0.0f;           // distance along the ray
    glm::vec3 position = {};             // world-space hit position
    glm::vec3 normal   = {};             // world-space surface normal at hit
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
    //   SyncActors        — creates / recreates / destroys PxActors to match ECS
    //   SyncControllers   — creates / destroys PxControllers to match ECS
    //   Step              — advances simulation by dt seconds
    //   WriteBack         — copies dynamic actor poses → TransformComponent
    //   WriteBackControllers — copies controller positions → TransformComponent
    //   DispatchEvents    — fires CollisionCallbackComponent callbacks
    void SyncActors          (Registry& registry);
    void SyncControllers     (Registry& registry);
    void Step                (float dt);
    void WriteBack           (Registry& registry);
    void WriteBackControllers(Registry& registry);
    void DispatchEvents      (Registry& registry);

    // Move a character controller by a world-space displacement vector.
    // Updates isGrounded and velocity fields on the component.
    void MoveController(Entity e, const glm::vec3& displacement, float dt);

    // Call when an entity is destroyed so its actor / controller gets cleaned up
    void RemoveEntity(Entity e);
    // Call when an entity's ColliderComponent or RigidbodyComponent is removed
    void MarkDirty   (Entity e);

    // ── Raycasting ───────────────────────────────────────────────────────────
    // Cast a ray from `origin` in `direction` (need not be normalised) up to
    // `maxDistance` metres. Only hits actors whose LayerComponent.layer bit is
    // set in `layerMask` (default 0xFFFFFFFF = all layers).
    // Returns the *closest* hit found, or an empty RaycastHit if nothing was struck.
    RaycastHit Raycast(const glm::vec3& origin,
                       const glm::vec3& direction,
                       float            maxDistance,
                       uint32_t         layerMask = 0xFFFFFFFFu) const;

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
    physx::PxShape*    CreateShape       (Registry& registry, Entity e);
    physx::PxTransform EntityPose        (Registry& registry, Entity e);
    uint32_t           ShapeHash         (Registry& registry, Entity e) const;
    void               DestroyActor      (Entity e);
    void               DestroyController (Entity e);

    // ── PhysX objects ────────────────────────────────────────────────────────
    physx::PxDefaultAllocator      m_allocator;
    physx::PxDefaultErrorCallback  m_errorCallback;
    physx::PxFoundation*           m_foundation         = nullptr;
    physx::PxPhysics*              m_physics            = nullptr;
    physx::PxDefaultCpuDispatcher* m_dispatcher         = nullptr;
    physx::PxScene*                m_scene              = nullptr;
    physx::PxMaterial*             m_defaultMat         = nullptr;
    physx::PxControllerManager*    m_controllerManager  = nullptr;

    ContactCallback              m_contactCallback;
    std::vector<RawEvent>        m_events;

    std::unordered_map<Entity, ActorState>           m_actors;
    std::unordered_map<Entity, physx::PxController*> m_controllers;

    // Per-frame write-back caches for MoveController → WriteBackControllers
    std::unordered_map<Entity, bool>      m_controllerGrounded;
    std::unordered_map<Entity, glm::vec3> m_controllerVelocity;

    bool m_initialized = false;
};

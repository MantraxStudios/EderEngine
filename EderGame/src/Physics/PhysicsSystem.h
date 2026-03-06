#pragma once

// ── Jolt Physics ──────────────────────────────────────────────────────────────
#include <Jolt/Jolt.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>

#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>
#include <memory>
#include <cstdint>
#include <string>

#include "ECS/Registry.h"
#include "ECS/Entity.h"
#include "ECS/Components/CollisionCallbackComponent.h"
#include "ECS/Components/CharacterControllerComponent.h"

// ── Object layers ─────────────────────────────────────────────────────────────
namespace PhysicsLayers
{
    static constexpr JPH::ObjectLayer STATIC    = 0;
    static constexpr JPH::ObjectLayer DYNAMIC   = 1;
    static constexpr JPH::ObjectLayer NUM_LAYERS = 2;
}

// ── Broad-phase layers ────────────────────────────────────────────────────────
namespace BroadPhaseLayers
{
    static constexpr JPH::BroadPhaseLayer NON_MOVING { 0 };
    static constexpr JPH::BroadPhaseLayer MOVING     { 1 };
    static constexpr uint32_t             NUM_LAYERS  = 2;
}

// ─────────────────────────────────────────────────────────────────────────────
// RaycastHit — result returned by PhysicsSystem::Raycast
// ─────────────────────────────────────────────────────────────────────────────
struct RaycastHit
{
    bool      hit      = false;
    Entity    entity   = NULL_ENTITY;
    float     distance = 0.0f;
    glm::vec3 position = {};
    glm::vec3 normal   = {};
};

// ─────────────────────────────────────────────────────────────────────────────
// PhysicsSystem — Jolt Physics backend
// ─────────────────────────────────────────────────────────────────────────────
class PhysicsSystem
{
public:
    static PhysicsSystem& Get();

    void Init();
    void Shutdown();

    // Called every game frame:
    //   SyncActors        — creates / recreates / destroys bodies to match ECS
    //   SyncControllers   — creates / destroys character virtuals to match ECS
    //   Step              — advances simulation by dt seconds
    //   WriteBack         — copies dynamic body poses → TransformComponent
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

    // Call when an entity is destroyed so its body / controller gets cleaned up
    void RemoveEntity(Entity e);
    // Call when an entity's ColliderComponent or RigidbodyComponent is removed
    void MarkDirty   (Entity e);

    // ── Raycasting ───────────────────────────────────────────────────────────
    RaycastHit Raycast(const glm::vec3& origin,
                       const glm::vec3& direction,
                       float            maxDistance,
                       uint32_t         layerMask = 0xFFFFFFFFu) const;

    void      SetVelocity(Entity e, const glm::vec3& velocity);
    void      AddForce   (Entity e, const glm::vec3& force);
    void      AddImpulse (Entity e, const glm::vec3& impulse);
    glm::vec3 GetVelocity(Entity e) const;

private:
    PhysicsSystem()  = default;
    ~PhysicsSystem() = default;

    // ── Internal raw event ────────────────────────────────────────────────────
    struct RawEvent
    {
        CollisionEventType type    = CollisionEventType::Enter;
        Entity             entityA = NULL_ENTITY;
        Entity             entityB = NULL_ENTITY;
        glm::vec3          point   = {};
        glm::vec3          normal  = {};
        bool               trigger = false;
    };

    // ── Per-body state ────────────────────────────────────────────────────────
    struct ActorState
    {
        JPH::BodyID bodyId;
        bool        dynamic   = false;
        bool        kinematic = false;
        bool        useGravity = true;
        float       mass      = 1.0f;
        uint32_t    shapeHash = 0;
    };

    // ── Contact listener ──────────────────────────────────────────────────────
    class ContactListenerImpl final : public JPH::ContactListener
    {
    public:
        std::vector<RawEvent>*                 events        = nullptr;
        std::unordered_map<uint32_t, Entity>*  bodyEntityMap = nullptr;
        std::unordered_map<uint32_t, bool>*    bodyIsSensor  = nullptr;

        JPH::ValidateResult OnContactValidate(
            const JPH::Body&, const JPH::Body&,
            JPH::RVec3Arg, const JPH::CollideShapeResult&) override
        {
            return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
        }

        void OnContactAdded(const JPH::Body& body1, const JPH::Body& body2,
            const JPH::ContactManifold& manifold, JPH::ContactSettings&) override;

        void OnContactPersisted(const JPH::Body& body1, const JPH::Body& body2,
            const JPH::ContactManifold& manifold, JPH::ContactSettings&) override;

        void OnContactRemoved(const JPH::SubShapeIDPair& subShapePair) override;

    private:
        void PushEvent(CollisionEventType type,
                       const JPH::Body& body1, const JPH::Body& body2,
                       const JPH::ContactManifold& manifold);
    };

    // ── Broad-phase layer interface ───────────────────────────────────────────
    struct BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface
    {
        uint32_t             GetNumBroadPhaseLayers()                          const override;
        JPH::BroadPhaseLayer GetBroadPhaseLayer    (JPH::ObjectLayer layer)    const override;
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
        const char*          GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override;
#endif
    };

    struct ObjVsBPLayerFilterImpl final : public JPH::ObjectVsBroadPhaseLayerFilter
    {
        bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer bpLayer) const override;
    };

    struct ObjLayerPairFilterImpl final : public JPH::ObjectLayerPairFilter
    {
        bool ShouldCollide(JPH::ObjectLayer layer1, JPH::ObjectLayer layer2) const override;
    };

    // ── Helpers ───────────────────────────────────────────────────────────────
    JPH::Ref<JPH::Shape> CreateShape         (Registry& registry, Entity e);
    JPH::RVec3           EntityPosition      (Registry& registry, Entity e);
    JPH::Quat            EntityRotation      (Registry& registry, Entity e);
    uint32_t             ShapeHash           (Registry& registry, Entity e) const;
    void                 DestroyActor        (Entity e);
    void                 DestroyController   (Entity e);

    JPH::BodyInterface& BodyIface() const
    {
        return m_physicsSystem->GetBodyInterface();
    }

    // ── Jolt objects ──────────────────────────────────────────────────────────
    std::unique_ptr<JPH::TempAllocatorImpl>   m_tempAllocator;
    std::unique_ptr<JPH::JobSystemThreadPool> m_jobSystem;
    std::unique_ptr<JPH::PhysicsSystem>       m_physicsSystem;

    BPLayerInterfaceImpl   m_bpLayerInterface;
    ObjVsBPLayerFilterImpl m_objVsBpFilter;
    ObjLayerPairFilterImpl m_objPairFilter;
    ContactListenerImpl    m_contactListener;

    std::vector<RawEvent>                                      m_events;
    std::unordered_map<Entity, ActorState>                     m_actors;
    std::unordered_map<Entity, JPH::Ref<JPH::CharacterVirtual>> m_controllers;

    // body index → entity / isSensor (for contact removal lookup)
    std::unordered_map<uint32_t, Entity> m_bodyEntityMap;
    std::unordered_map<uint32_t, bool>   m_bodyIsSensor;

    // pending controller movement (set by MoveController, consumed in Step)
    std::unordered_map<Entity, glm::vec3> m_pendingDisplacement;

    // per-frame write-back caches for controllers → WriteBackControllers
    std::unordered_map<Entity, bool>      m_controllerGrounded;
    std::unordered_map<Entity, glm::vec3> m_controllerVelocity;

    bool m_initialized = false;
};

#include "PhysicsSystem.h"

#include "ECS/Components/TransformComponent.h"
#include "ECS/Components/ColliderComponent.h"
#include "ECS/Components/RigidbodyComponent.h"
#include "ECS/Components/CollisionCallbackComponent.h"
#include "ECS/Components/LayerComponent.h"
#include "ECS/Systems/TransformSystem.h"

#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cstring>
#include <cmath>

using namespace physx;

// ─────────────────────────────────────────────────────────────────────────────
// Singleton
// ─────────────────────────────────────────────────────────────────────────────
PhysicsSystem& PhysicsSystem::Get()
{
    static PhysicsSystem s_instance;
    return s_instance;
}

// ─────────────────────────────────────────────────────────────────────────────
// Filter shader — layer-aware collision filtering
//
//  Each shape stores in its PxFilterData:
//    word0 = layer bit  (1u << layer)
//    word1 = layerMask  (which layers this entity interacts with)
//
//  Two actors interact only when both accept each other’s layer.
// ─────────────────────────────────────────────────────────────────────────────
static PxFilterFlags ContactFilterShader(
    PxFilterObjectAttributes /*a0*/, PxFilterData d0,
    PxFilterObjectAttributes /*a1*/, PxFilterData d1,
    PxPairFlags& pairFlags,
    const void* /*constantBlock*/, PxU32 /*constantBlockSize*/)
{
    // Layer-mask check: A must accept B’s layer AND B must accept A’s layer.
    if (!(d0.word1 & d1.word0) || !(d1.word1 & d0.word0))
        return PxFilterFlag::eSUPPRESS;

    pairFlags = PxPairFlag::eCONTACT_DEFAULT
              | PxPairFlag::eNOTIFY_TOUCH_FOUND
              | PxPairFlag::eNOTIFY_TOUCH_LOST
              | PxPairFlag::eNOTIFY_TOUCH_PERSISTS;

    return PxFilterFlag::eDEFAULT;
}

// ─────────────────────────────────────────────────────────────────────────────
// ContactCallback
// ─────────────────────────────────────────────────────────────────────────────
void PhysicsSystem::ContactCallback::onContact(
    const PxContactPairHeader& header,
    const PxContactPair*       pairs,
    PxU32                      nbPairs)
{
    if (!events) return;

    Entity eA = static_cast<Entity>(reinterpret_cast<uintptr_t>(header.actors[0]->userData));
    Entity eB = static_cast<Entity>(reinterpret_cast<uintptr_t>(header.actors[1]->userData));

    for (PxU32 i = 0; i < nbPairs; ++i)
    {
        const PxContactPair& cp = pairs[i];

        CollisionEventType type = CollisionEventType::Stay;
        if      (cp.events & PxPairFlag::eNOTIFY_TOUCH_FOUND) type = CollisionEventType::Enter;
        else if (cp.events & PxPairFlag::eNOTIFY_TOUCH_LOST)  type = CollisionEventType::Exit;

        glm::vec3 point  = {};
        glm::vec3 normal = {};
        PxContactPairPoint pts[8];
        PxU32 nPts = cp.extractContacts(pts, 8);
        if (nPts > 0)
        {
            point  = { pts[0].position.x, pts[0].position.y, pts[0].position.z };
            normal = { pts[0].normal.x,   pts[0].normal.y,   pts[0].normal.z   };
        }

        events->push_back({ type, eA, eB, point, normal, false });
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// ContactCallback::onTrigger
// ─────────────────────────────────────────────────────────────────────────────
void PhysicsSystem::ContactCallback::onTrigger(
    PxTriggerPair* pairs, PxU32 nbPairs)
{
    if (!events) return;
    for (PxU32 i = 0; i < nbPairs; ++i)
    {
        const PxTriggerPair& tp = pairs[i];
        // Skip stale pairs caused by removed shapes/actors
        if (tp.flags & (PxTriggerPairFlag::eREMOVED_SHAPE_TRIGGER |
                        PxTriggerPairFlag::eREMOVED_SHAPE_OTHER))
            continue;

        Entity eTrigger = static_cast<Entity>(
            reinterpret_cast<uintptr_t>(tp.triggerActor->userData));
        Entity eOther = static_cast<Entity>(
            reinterpret_cast<uintptr_t>(tp.otherActor->userData));

        CollisionEventType type =
            (tp.status & PxPairFlag::eNOTIFY_TOUCH_FOUND)
            ? CollisionEventType::Enter
            : CollisionEventType::Exit;

        events->push_back({ type, eTrigger, eOther, {}, {}, true });
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Init / Shutdown
// ─────────────────────────────────────────────────────────────────────────────
void PhysicsSystem::Init()
{
    if (m_initialized) return;

    m_foundation = PxCreateFoundation(
        PX_PHYSICS_VERSION, m_allocator, m_errorCallback);

    m_physics = PxCreatePhysics(
        PX_PHYSICS_VERSION, *m_foundation,
        PxTolerancesScale(), true /*trackOutstandingAllocations*/);

    m_dispatcher = PxDefaultCpuDispatcherCreate(2 /*threads*/);

    PxSceneDesc sceneDesc(m_physics->getTolerancesScale());
    sceneDesc.gravity        = PxVec3(0.0f, -9.81f, 0.0f);
    sceneDesc.cpuDispatcher  = m_dispatcher;
    sceneDesc.filterShader   = ContactFilterShader;

    m_contactCallback.events = &m_events;
    sceneDesc.simulationEventCallback = &m_contactCallback;

    m_scene = m_physics->createScene(sceneDesc);

    m_defaultMat = m_physics->createMaterial(0.5f, 0.5f, 0.3f);

    m_initialized = true;
}

void PhysicsSystem::Shutdown()
{
    if (!m_initialized) return;

    // Release all actors
    for (auto& [e, state] : m_actors)
    {
        if (state.actor)
        {
            m_scene->removeActor(*state.actor);
            state.actor->release();
        }
    }
    m_actors.clear();

    if (m_defaultMat)   { m_defaultMat->release();   m_defaultMat   = nullptr; }
    if (m_scene)        { m_scene->release();         m_scene        = nullptr; }
    if (m_dispatcher)   { m_dispatcher->release();    m_dispatcher   = nullptr; }
    if (m_physics)      { m_physics->release();       m_physics      = nullptr; }
    if (m_foundation)   { m_foundation->release();    m_foundation   = nullptr; }

    m_initialized = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────
static inline PxVec3 ToPhysX(const glm::vec3& v) { return { v.x, v.y, v.z }; }
static inline PxQuat ToPhysX(const glm::quat& q)  { return { q.x, q.y, q.z, q.w }; }
static inline glm::vec3 ToGlm(const PxVec3& v)   { return { v.x, v.y, v.z }; }
static inline glm::quat ToGlm(const PxQuat& q)   { return { q.w, q.x, q.y, q.z }; }

PxTransform PhysicsSystem::EntityPose(Registry& registry, Entity e)
{
    glm::mat4 world = TransformSystem::GetWorldMatrix(e, registry);

    // Extract translation
    glm::vec3 pos = glm::vec3(world[3]);

    // Extract rotation (strip scale)
    glm::vec3 scl;
    glm::quat rot;
    glm::vec3 skew;
    glm::vec4 perspective;
    glm::decompose(world, scl, rot, pos, skew, perspective);

    // Apply center offset only when a ColliderComponent exists
    if (registry.Has<ColliderComponent>(e))
    {
        const auto& col = registry.Get<ColliderComponent>(e);
        pos += rot * col.center;
    }

    return PxTransform(ToPhysX(pos), ToPhysX(rot));
}

uint32_t PhysicsSystem::ShapeHash(Registry& registry, Entity e) const
{
    const auto& col = registry.Get<ColliderComponent>(e);
    glm::mat4   w   = TransformSystem::GetWorldMatrix(e, registry);
    glm::vec3   scl = { glm::length(glm::vec3(w[0])),
                        glm::length(glm::vec3(w[1])),
                        glm::length(glm::vec3(w[2])) };

    // Simple hash combining shape enum + dims + scale + isTrigger
    uint32_t h = static_cast<uint32_t>(col.shape) * 2654435761u;

    auto hashF = [](float f) -> uint32_t {
        uint32_t u; memcpy(&u, &f, sizeof(u)); return u * 2246822519u;
    };

    h ^= hashF(col.boxHalfExtents.x); h ^= hashF(col.boxHalfExtents.y); h ^= hashF(col.boxHalfExtents.z);
    h ^= hashF(col.radius);
    h ^= hashF(col.capsuleHalfHeight);
    h ^= hashF(col.staticFriction);
    h ^= hashF(col.dynamicFriction);
    h ^= hashF(col.restitution);
    h ^= hashF(scl.x); h ^= hashF(scl.y); h ^= hashF(scl.z);
    h ^= static_cast<uint32_t>(col.isTrigger);

    // Include rigidbody presence and properties so adding/removing it
    // forces a static -> dynamic (or dynamic -> static) actor rebuild.
    if (registry.Has<RigidbodyComponent>(e))
    {
        const auto& rb = registry.Get<RigidbodyComponent>(e);
        h ^= 0xDEADBEEFu;
        h ^= hashF(rb.mass);
        h ^= hashF(rb.linearDrag);
        h ^= hashF(rb.angularDrag);
        h ^= (rb.isKinematic ? 0xAAAAAAAAu : 0u);
        h ^= (rb.useGravity  ? 0x55555555u : 0u);
    }

    // Layer changes require a shape rebuild (filter data must be refreshed).
    if (registry.Has<LayerComponent>(e))
    {
        const auto& lc = registry.Get<LayerComponent>(e);
        h ^= static_cast<uint32_t>(lc.layer) * 0x12345678u;
        h ^= lc.layerMask * 0x87654321u;
    }

    return h;
}

PxShape* PhysicsSystem::CreateShape(Registry& registry, Entity e)
{
    const auto& col = registry.Get<ColliderComponent>(e);

    // Extract world scale
    glm::mat4 world = TransformSystem::GetWorldMatrix(e, registry);
    glm::vec3 scl   = { glm::length(glm::vec3(world[0])),
                        glm::length(glm::vec3(world[1])),
                        glm::length(glm::vec3(world[2])) };

    PxMaterial* mat = m_physics->createMaterial(
        col.staticFriction, col.dynamicFriction, col.restitution);

    PxShape* shape = nullptr;

    switch (col.shape)
    {
    case ColliderShape::Box:
    {
        PxVec3 half(
            std::abs(col.boxHalfExtents.x * scl.x),
            std::abs(col.boxHalfExtents.y * scl.y),
            std::abs(col.boxHalfExtents.z * scl.z));
        // clamp to a minimum to avoid degenerate geometry
        half.x = std::max(half.x, 0.001f);
        half.y = std::max(half.y, 0.001f);
        half.z = std::max(half.z, 0.001f);
        shape = m_physics->createShape(PxBoxGeometry(half), *mat, true);
        break;
    }
    case ColliderShape::Sphere:
    {
        float maxScl = std::max({ scl.x, scl.y, scl.z });
        float r      = std::max(col.radius * maxScl, 0.001f);
        shape = m_physics->createShape(PxSphereGeometry(r), *mat, true);
        break;
    }
    case ColliderShape::Capsule:
    {
        // PxCapsuleGeometry is aligned along X; rotate local pose 90° around Z
        float maxXZ = std::max(scl.x, scl.z);
        float r     = std::max(col.radius           * maxXZ, 0.001f);
        float hh    = std::max(col.capsuleHalfHeight * scl.y, 0.001f);
        shape = m_physics->createShape(PxCapsuleGeometry(r, hh), *mat, true);
        // Rotate 90° around Z to align capsule along Y
        PxQuat rot90(physx::PxHalfPi, PxVec3(0.0f, 0.0f, 1.0f));
        shape->setLocalPose(PxTransform(PxVec3(0.0f), rot90));
        break;
    }
    }

    mat->release(); // shape holds its own ref

    if (shape)
    {
        if (col.isTrigger)
        {
            shape->setFlag(PxShapeFlag::eSIMULATION_SHAPE, false);
            shape->setFlag(PxShapeFlag::eTRIGGER_SHAPE,    true);
        }

        // ── Layer filter data ────────────────────────────────────────────────────────
        // word0 = bit representing this entity’s layer
        // word1 = layer mask (which layers this entity interacts with)
        PxFilterData fd;
        if (registry.Has<LayerComponent>(e))
        {
            const auto& lc = registry.Get<LayerComponent>(e);
            uint8_t layer = lc.layer < 32u ? lc.layer : 0u;
            fd.word0 = 1u << layer;
            fd.word1 = lc.layerMask;
        }
        else
        {
            fd.word0 = 1u;           // default: layer 0
            fd.word1 = 0xFFFFFFFFu;  // interacts with everything
        }
        // Store entity id in simulationFilterData so the filter shader and
        // raycast queries can retrieve it.
        fd.word2 = static_cast<PxU32>(e);
        shape->setSimulationFilterData(fd);
        shape->setQueryFilterData(fd);
    }

    return shape;
}

void PhysicsSystem::DestroyActor(Entity e)
{
    auto it = m_actors.find(e);
    if (it == m_actors.end()) return;

    if (it->second.actor)
    {
        m_scene->removeActor(*it->second.actor);
        it->second.actor->release();
    }
    m_actors.erase(it);
}

// ─────────────────────────────────────────────────────────────────────────────
// SyncActors — create / recreate / remove PxActors to match ECS
// ─────────────────────────────────────────────────────────────────────────────
void PhysicsSystem::SyncActors(Registry& registry)
{
    if (!m_initialized) return;

    // ── Remove actors for entities that lost all physics components ─────────
    std::vector<Entity> toRemove;
    for (auto& [e, state] : m_actors)
    {
        if (!registry.Has<ColliderComponent>(e) && !registry.Has<RigidbodyComponent>(e))
            toRemove.push_back(e);
    }
    for (Entity e : toRemove)
        DestroyActor(e);

    // ── Entities with ColliderComponent (with or without RigidbodyComponent) ─
    registry.Each<ColliderComponent>([&](Entity e, ColliderComponent& col)
    {
        uint32_t newHash = ShapeHash(registry, e);

        auto it = m_actors.find(e);
        if (it != m_actors.end())
        {
            // Check if collider shape geometry changed
            if (it->second.shapeHash == newHash)
            {
                // Just update kinematic target or static pose if needed
                ActorState& state = it->second;
                if (state.dynamic)
                {
                    auto* dyn = static_cast<PxRigidDynamic*>(state.actor);
                    if (state.kinematic && registry.Has<TransformComponent>(e))
                    {
                        PxTransform pose = EntityPose(registry, e);
                        dyn->setKinematicTarget(pose);
                    }
                }
                else
                {
                    // Static: update pose if transform drifted (e.g., editor moved it)
                    if (registry.Has<TransformComponent>(e))
                    {
                        PxTransform pose = EntityPose(registry, e);
                        state.actor->setGlobalPose(pose);
                    }
                }
                return; // no shape rebuild needed
            }
            // Shape changed — fall through to destroy & recreate
            DestroyActor(e);
        }

        // ── Create new actor ─────────────────────────────────────────────────
        if (!registry.Has<TransformComponent>(e)) return;

        PxShape* shape = CreateShape(registry, e);
        if (!shape) return;

        PxTransform pose = EntityPose(registry, e);

        ActorState state;
        state.shapeHash = newHash;

        if (registry.Has<RigidbodyComponent>(e))
        {
            const auto& rb = registry.Get<RigidbodyComponent>(e);
            PxRigidDynamic* dyn = m_physics->createRigidDynamic(pose);
            dyn->attachShape(*shape);
            PxRigidBodyExt::updateMassAndInertia(*dyn, rb.mass > 0.0f ? rb.mass : 1.0f);
            dyn->setLinearDamping(rb.linearDrag);
            dyn->setAngularDamping(rb.angularDrag);
            dyn->setActorFlag(PxActorFlag::eDISABLE_GRAVITY, !rb.useGravity);
            if (rb.isKinematic)
                dyn->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, true);
            dyn->userData = reinterpret_cast<void*>(static_cast<uintptr_t>(e));
            m_scene->addActor(*dyn);

            state.actor     = dyn;
            state.dynamic   = true;
            state.kinematic = rb.isKinematic;
            state.useGravity= rb.useGravity;
            state.mass      = rb.mass;
        }
        else
        {
            PxRigidStatic* sta = m_physics->createRigidStatic(pose);
            sta->attachShape(*shape);
            sta->userData = reinterpret_cast<void*>(static_cast<uintptr_t>(e));
            m_scene->addActor(*sta);

            state.actor   = sta;
            state.dynamic = false;
        }

        shape->release();
        m_actors[e] = state;
    });

    // ── Entities with RigidbodyComponent but NO ColliderComponent ─────────────
    // These still need a PxRigidDynamic so gravity / forces / velocity work.
    // They have no shape, so they will not collide with anything.
    registry.Each<RigidbodyComponent>([&](Entity e, RigidbodyComponent& rb)
    {
        if (registry.Has<ColliderComponent>(e)) return; // already handled above
        if (!registry.Has<TransformComponent>(e)) return;

        // Hash rb fields to detect changes
        auto hashF = [](float f) -> uint32_t {
            uint32_t u; memcpy(&u, &f, sizeof(u)); return u * 2246822519u;
        };
        uint32_t newHash = hashF(rb.mass) ^ hashF(rb.linearDrag) ^ hashF(rb.angularDrag)
                         ^ (rb.isKinematic ? 0xAAAAAAAAu : 0u)
                         ^ (rb.useGravity  ? 0x55555555u : 0u);

        auto it = m_actors.find(e);
        if (it != m_actors.end())
        {
            if (it->second.shapeHash == newHash)
            {
                // Already up-to-date — just push kinematic target if needed
                if (it->second.kinematic)
                {
                    auto* dyn = static_cast<PxRigidDynamic*>(it->second.actor);
                    dyn->setKinematicTarget(EntityPose(registry, e));
                }
                return;
            }
            // rb params changed — rebuild
            DestroyActor(e);
        }

        PxTransform pose = EntityPose(registry, e);
        PxRigidDynamic* dyn = m_physics->createRigidDynamic(pose);

        // Shapeless actor: visible to simulation (gravity, forces) but no collisions
        float mass = rb.mass > 0.0f ? rb.mass : 1.0f;
        dyn->setMass(mass);
        dyn->setMassSpaceInertiaTensor(PxVec3(mass * 0.1f)); // minimal inertia tensor
        dyn->setLinearDamping(rb.linearDrag);
        dyn->setAngularDamping(rb.angularDrag);
        dyn->setActorFlag(PxActorFlag::eDISABLE_GRAVITY, !rb.useGravity);
        if (rb.isKinematic)
            dyn->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, true);
        dyn->userData = reinterpret_cast<void*>(static_cast<uintptr_t>(e));
        m_scene->addActor(*dyn);

        ActorState state;
        state.actor      = dyn;
        state.dynamic    = true;
        state.kinematic  = rb.isKinematic;
        state.useGravity = rb.useGravity;
        state.mass       = rb.mass;
        state.shapeHash  = newHash;
        m_actors[e] = state;
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// Step — advance simulation
// ─────────────────────────────────────────────────────────────────────────────
void PhysicsSystem::Step(float dt)
{
    if (!m_initialized || !m_scene) return;
    // Clamp delta to avoid instability on first frame / breakpoints
    dt = std::min(dt, 0.05f);
    if (dt <= 0.0f) return;

    m_events.clear();
    m_scene->simulate(dt);
    m_scene->fetchResults(true);
}

// ─────────────────────────────────────────────────────────────────────────────
// WriteBack — copy dynamic actor poses back into TransformComponent
// ─────────────────────────────────────────────────────────────────────────────
void PhysicsSystem::WriteBack(Registry& registry)
{
    if (!m_initialized) return;

    for (auto& [e, state] : m_actors)
    {
        if (!state.dynamic || state.kinematic) continue;
        if (!registry.Has<TransformComponent>(e)) continue;

        auto* dyn = static_cast<PxRigidDynamic*>(state.actor);
        PxTransform t = dyn->getGlobalPose();

        glm::vec3 pos(t.p.x, t.p.y, t.p.z);
        glm::quat rot(t.q.w, t.q.x, t.q.y, t.q.z);

        // Subtract center offset
        if (registry.Has<ColliderComponent>(e))
        {
            const auto& col = registry.Get<ColliderComponent>(e);
            pos -= rot * col.center;
        }

        auto& tr = registry.Get<TransformComponent>(e);

        // Write world position (assumes root-level transforms for physics objects)
        tr.position = pos;

        // Convert quaternion → euler degrees (XYZ / Pitch-Yaw-Roll)
        glm::vec3 euler = glm::degrees(glm::eulerAngles(rot));
        tr.rotation = euler;

        // Write back velocity to RigidbodyComponent
        if (registry.Has<RigidbodyComponent>(e))
        {
            auto& rb = registry.Get<RigidbodyComponent>(e);
            PxVec3 lv = dyn->getLinearVelocity();
            PxVec3 av = dyn->getAngularVelocity();
            rb.linearVelocity  = { lv.x, lv.y, lv.z };
            rb.angularVelocity = { av.x, av.y, av.z };
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// RemoveEntity / MarkDirty
// ─────────────────────────────────────────────────────────────────────────────
void PhysicsSystem::RemoveEntity(Entity e)
{
    DestroyActor(e);
}

void PhysicsSystem::MarkDirty(Entity e)
{
    auto it = m_actors.find(e);
    if (it != m_actors.end())
        it->second.shapeHash = 0; // force recreation on next SyncActors
}

// ─────────────────────────────────────────────────────────────────────────────
// DispatchEvents — fire CollisionCallbackComponent callbacks, then clear queue
// Call this after WriteBack every frame.
// ─────────────────────────────────────────────────────────────────────────────
void PhysicsSystem::DispatchEvents(Registry& registry)
{
    for (const RawEvent& raw : m_events)
    {
        // Fire callbacks for both participants.
        // `self` receives the event with normal pointing AWAY from `other`.
        auto fire = [&](Entity self, Entity other, const glm::vec3& normal)
        {
            if (self == NULL_ENTITY) return;
            if (!registry.Has<CollisionCallbackComponent>(self)) return;
            auto& cb = registry.Get<CollisionCallbackComponent>(self);

            CollisionEvent ev;
            ev.type    = raw.type;
            ev.self    = self;
            ev.other   = other;
            ev.point   = raw.point;
            ev.normal  = normal;
            ev.trigger = raw.trigger;

            if (raw.trigger)
            {
                if (raw.type == CollisionEventType::Enter && cb.onTriggerEnter) cb.onTriggerEnter(ev);
                if (raw.type == CollisionEventType::Exit  && cb.onTriggerExit)  cb.onTriggerExit(ev);
            }
            else
            {
                if (raw.type == CollisionEventType::Enter && cb.onCollisionEnter) cb.onCollisionEnter(ev);
                if (raw.type == CollisionEventType::Stay  && cb.onCollisionStay)  cb.onCollisionStay(ev);
                if (raw.type == CollisionEventType::Exit  && cb.onCollisionExit)  cb.onCollisionExit(ev);
            }
        };

        fire(raw.entityA, raw.entityB,  raw.normal);
        fire(raw.entityB, raw.entityA, -raw.normal);
    }
    m_events.clear();
}

// ─────────────────────────────────────────────────────────────────────────────
// Raycast — cast a ray through the PhysX scene
// ─────────────────────────────────────────────────────────────────────────────
RaycastHit PhysicsSystem::Raycast(const glm::vec3& origin,
                                  const glm::vec3& direction,
                                  float            maxDistance,
                                  uint32_t         layerMask) const
{
    RaycastHit result;
    if (!m_initialized || !m_scene) return result;

    // Normalise the direction
    float len = glm::length(direction);
    if (len < 1e-6f) return result;
    glm::vec3 dir = direction / len;

    PxVec3 origin3(origin.x, origin.y, origin.z);
    PxVec3 dir3(dir.x, dir.y, dir.z);

    // Build a query filter data that carries the caller\u2019s layer mask in word1.
    // The query filter callback will accept a shape when its layer bit (word0)
    // is set in the caller\u2019s mask (word1).
    PxQueryFilterData filterData;
    filterData.data.word1 = layerMask;
    filterData.flags      = PxQueryFlag::eSTATIC
                          | PxQueryFlag::eDYNAMIC
                          | PxQueryFlag::ePREFILTER;

    // Use an inline pre-filter to apply the layer mask without a separate callback object.
    struct LayerFilter : public PxQueryFilterCallback
    {
        uint32_t callerMask;
        explicit LayerFilter(uint32_t mask) : callerMask(mask) {}

        PxQueryHitType::Enum preFilter(
            const PxFilterData& fd,
            const PxShape*,
            const PxRigidActor*,
            PxHitFlags&) override
        {
            // Accept only if this shape\u2019s layer bit is in the caller\u2019s mask,
            // and also if the shape\u2019s own mask includes at least one source layer.
            if (fd.word0 & callerMask)
                return PxQueryHitType::eBLOCK;
            return PxQueryHitType::eNONE;
        }

        PxQueryHitType::Enum postFilter(
            const PxFilterData&,
            const PxQueryHit&,
            const PxShape*,
            const PxRigidActor*) override
        {
            return PxQueryHitType::eBLOCK;
        }
    } filter(layerMask);

    PxRaycastBuffer hit;
    bool found = m_scene->raycast(origin3, dir3, maxDistance, hit,
                                  PxHitFlag::eDEFAULT, filterData, &filter);

    if (found && hit.hasBlock)
    {
        const PxRaycastHit& block = hit.block;
        result.hit      = true;
        result.distance = block.distance;
        result.position = { block.position.x, block.position.y, block.position.z };
        result.normal   = { block.normal.x,   block.normal.y,   block.normal.z   };

        // Recover entity from actor userData (set in SyncActors)
        if (block.actor && block.actor->userData)
            result.entity = static_cast<Entity>(
                reinterpret_cast<uintptr_t>(block.actor->userData));
    }

    return result;
}

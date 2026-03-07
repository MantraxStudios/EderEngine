#include "PhysicsSystem.h"

#include "ECS/Components/TransformComponent.h"
#include "ECS/Components/ColliderComponent.h"
#include "ECS/Components/RigidbodyComponent.h"
#include "ECS/Components/CollisionCallbackComponent.h"
#include "ECS/Components/LayerComponent.h"
#include "ECS/Components/CharacterControllerComponent.h"
#include "ECS/Systems/TransformSystem.h"

// ── Jolt ──────────────────────────────────────────────────────────────────────
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseQuery.h>

#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cstring>
#include <cmath>
#include <iostream>
#include <thread>

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────
static inline JPH::Vec3  ToJolt (const glm::vec3& v) { return { v.x, v.y, v.z }; }
static inline JPH::RVec3 ToJoltR(const glm::vec3& v) { return JPH::RVec3(v.x, v.y, v.z); }
static inline JPH::Quat  ToJolt (const glm::quat& q) { return { q.x, q.y, q.z, q.w }; }

// Single template handles both Vec3 and RVec3 (same type in single-precision builds)
template<typename T>
static inline glm::vec3  ToGlm(const T& v) { return { (float)v.GetX(), (float)v.GetY(), (float)v.GetZ() }; }
static inline glm::quat  ToGlm(const JPH::Quat& q) { return { q.GetW(), q.GetX(), q.GetY(), q.GetZ() }; }

// ─────────────────────────────────────────────────────────────────────────────
// Simple "accept all" filters used by CharacterVirtual::Update
// ─────────────────────────────────────────────────────────────────────────────
class AllBroadPhaseLayerFilter : public JPH::BroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::BroadPhaseLayer) const override { return true; }
};

class AllObjectLayerFilter : public JPH::ObjectLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer) const override { return true; }
};

// ─────────────────────────────────────────────────────────────────────────────
// BPLayerInterfaceImpl
// ─────────────────────────────────────────────────────────────────────────────
uint32_t PhysicsSystem::BPLayerInterfaceImpl::GetNumBroadPhaseLayers() const
{
    return BroadPhaseLayers::NUM_LAYERS;
}

JPH::BroadPhaseLayer PhysicsSystem::BPLayerInterfaceImpl::GetBroadPhaseLayer(JPH::ObjectLayer layer) const
{
    switch (layer)
    {
    case PhysicsLayers::STATIC:  return BroadPhaseLayers::NON_MOVING;
    case PhysicsLayers::DYNAMIC: return BroadPhaseLayers::MOVING;
    default:                     return BroadPhaseLayers::MOVING;
    }
}

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
const char* PhysicsSystem::BPLayerInterfaceImpl::GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const
{
    switch ((uint8_t)layer)
    {
    case 0: return "NON_MOVING";
    case 1: return "MOVING";
    default: return "INVALID";
    }
}
#endif

// ─────────────────────────────────────────────────────────────────────────────
// ObjVsBPLayerFilterImpl
// ─────────────────────────────────────────────────────────────────────────────
bool PhysicsSystem::ObjVsBPLayerFilterImpl::ShouldCollide(
    JPH::ObjectLayer layer, JPH::BroadPhaseLayer bpLayer) const
{
    switch (layer)
    {
    case PhysicsLayers::STATIC:  return bpLayer == BroadPhaseLayers::MOVING;
    case PhysicsLayers::DYNAMIC: return true;
    default:                     return false;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// ObjLayerPairFilterImpl
// ─────────────────────────────────────────────────────────────────────────────
bool PhysicsSystem::ObjLayerPairFilterImpl::ShouldCollide(
    JPH::ObjectLayer layer1, JPH::ObjectLayer layer2) const
{
    switch (layer1)
    {
    case PhysicsLayers::STATIC:  return layer2 == PhysicsLayers::DYNAMIC;
    case PhysicsLayers::DYNAMIC: return true;
    default:                     return false;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// ContactListenerImpl
// ─────────────────────────────────────────────────────────────────────────────
void PhysicsSystem::ContactListenerImpl::PushEvent(
    CollisionEventType type,
    const JPH::Body& body1, const JPH::Body& body2,
    const JPH::ContactManifold& manifold)
{
    if (!events || !bodyEntityMap) return;

    uint32_t idx1 = body1.GetID().GetIndex();
    uint32_t idx2 = body2.GetID().GetIndex();

    auto it1 = bodyEntityMap->find(idx1);
    auto it2 = bodyEntityMap->find(idx2);
    if (it1 == bodyEntityMap->end() || it2 == bodyEntityMap->end()) return;

    Entity eA = it1->second;
    Entity eB = it2->second;

    bool isTrigger = body1.IsSensor() || body2.IsSensor();

    glm::vec3 point  = {};
    glm::vec3 normal = {};

    if (!manifold.mRelativeContactPointsOn1.empty())
    {
        JPH::RVec3 wp = manifold.mBaseOffset + manifold.mRelativeContactPointsOn1[0];
        point = ToGlm(wp);
    }
    JPH::Vec3 n = manifold.mWorldSpaceNormal;
    normal = ToGlm(n);

    events->push_back({ type, eA, eB, point, normal, isTrigger });
}

void PhysicsSystem::ContactListenerImpl::OnContactAdded(
    const JPH::Body& body1, const JPH::Body& body2,
    const JPH::ContactManifold& manifold, JPH::ContactSettings&)
{
    PushEvent(CollisionEventType::Enter, body1, body2, manifold);
}

void PhysicsSystem::ContactListenerImpl::OnContactPersisted(
    const JPH::Body& body1, const JPH::Body& body2,
    const JPH::ContactManifold& manifold, JPH::ContactSettings&)
{
    PushEvent(CollisionEventType::Stay, body1, body2, manifold);
}

void PhysicsSystem::ContactListenerImpl::OnContactRemoved(
    const JPH::SubShapeIDPair& subShapePair)
{
    if (!events || !bodyEntityMap) return;

    uint32_t idx1 = subShapePair.GetBody1ID().GetIndex();
    uint32_t idx2 = subShapePair.GetBody2ID().GetIndex();

    auto it1 = bodyEntityMap->find(idx1);
    auto it2 = bodyEntityMap->find(idx2);
    if (it1 == bodyEntityMap->end() || it2 == bodyEntityMap->end()) return;

    Entity eA = it1->second;
    Entity eB = it2->second;

    bool isTrigger = false;
    if (bodyIsSensor)
    {
        auto s1 = bodyIsSensor->find(idx1);
        auto s2 = bodyIsSensor->find(idx2);
        if (s1 != bodyIsSensor->end()) isTrigger |= s1->second;
        if (s2 != bodyIsSensor->end()) isTrigger |= s2->second;
    }

    events->push_back({ CollisionEventType::Exit, eA, eB, {}, {}, isTrigger });
}

// ─────────────────────────────────────────────────────────────────────────────
// Singleton
// ─────────────────────────────────────────────────────────────────────────────
PhysicsSystem& PhysicsSystem::Get()
{
    static PhysicsSystem s_instance;
    return s_instance;
}

// ─────────────────────────────────────────────────────────────────────────────
// Init / Shutdown
// ─────────────────────────────────────────────────────────────────────────────
void PhysicsSystem::Init()
{
    if (m_initialized) return;

    JPH::RegisterDefaultAllocator();

    JPH::Factory::sInstance = new JPH::Factory();

    if (!JPH::VerifyJoltVersionID())
    {
        std::cerr << "[Jolt] FATAL: ABI version mismatch between headers and Jolt.lib.\n"
                  << "       Recompile Jolt or match its compile defines in CMakeLists.\n";
        return;
    }

    JPH::RegisterTypes();

    constexpr uint32_t cTempAllocMB  = 16;
    constexpr uint32_t cMaxBodies    = 65536;
    constexpr uint32_t cBodyMutexes  = 0;      // auto
    constexpr uint32_t cMaxBodyPairs = 65536;
    constexpr uint32_t cMaxContacts  = 10240;

    m_tempAllocator = std::make_unique<JPH::TempAllocatorImpl>(cTempAllocMB * 1024 * 1024);

    int numThreads = std::max(1, (int)std::thread::hardware_concurrency() - 1);
    m_jobSystem = std::make_unique<JPH::JobSystemThreadPool>(
        JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, numThreads);

    m_physicsSystem = std::make_unique<JPH::PhysicsSystem>();
    m_physicsSystem->Init(cMaxBodies, cBodyMutexes, cMaxBodyPairs, cMaxContacts,
                          m_bpLayerInterface, m_objVsBpFilter, m_objPairFilter);

    m_physicsSystem->SetGravity(JPH::Vec3(0.0f, -9.81f, 0.0f));

    // Reduce penetration slop and speculative contact distance to prevent sticky collisions.
    JPH::PhysicsSettings ps;
    ps.mPenetrationSlop               = 0.005f;  // default 0.02 — less sinking/sticking
    ps.mSpeculativeContactDistance    = 0.005f;  // default 0.02 — less ghost contacts
    ps.mBaumgarte                     = 0.4f;    // default 0.2  — faster depenetration
    m_physicsSystem->SetPhysicsSettings(ps);

    m_contactListener.events        = &m_events;
    m_contactListener.bodyEntityMap = &m_bodyEntityMap;
    m_contactListener.bodyIsSensor  = &m_bodyIsSensor;
    m_physicsSystem->SetContactListener(&m_contactListener);

    m_initialized = true;
}

void PhysicsSystem::Shutdown()
{
    if (!m_initialized) return;

    // Remove all bodies
    for (auto& [e, state] : m_actors)
    {
        if (!state.bodyId.IsInvalid())
        {
            BodyIface().RemoveBody(state.bodyId);
            BodyIface().DestroyBody(state.bodyId);
        }
    }
    m_actors.clear();
    m_bodyEntityMap.clear();
    m_bodyIsSensor.clear();

    m_controllers.clear();
    m_events.clear();
    m_pendingDisplacement.clear();
    m_controllerGrounded.clear();
    m_controllerVelocity.clear();
    m_controllerPrevContacts.clear();

    m_physicsSystem.reset();
    m_jobSystem.reset();
    m_tempAllocator.reset();

    JPH::UnregisterTypes();
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;

    m_initialized = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Entity pose helpers
// ─────────────────────────────────────────────────────────────────────────────
JPH::RVec3 PhysicsSystem::EntityPosition(Registry& registry, Entity e)
{
    glm::mat4 world = TransformSystem::GetWorldMatrix(e, registry);

    glm::vec3 pos, skew;
    glm::vec4 perspective;
    glm::quat rot;
    glm::vec3 scl;
    glm::decompose(world, scl, rot, pos, skew, perspective);

    if (registry.Has<ColliderComponent>(e))
        pos += rot * registry.Get<ColliderComponent>(e).center;

    return ToJoltR(pos);
}

JPH::Quat PhysicsSystem::EntityRotation(Registry& registry, Entity e)
{
    glm::mat4 world = TransformSystem::GetWorldMatrix(e, registry);

    glm::vec3 pos, skew, scl;
    glm::vec4 perspective;
    glm::quat rot;
    glm::decompose(world, scl, rot, pos, skew, perspective);

    return ToJolt(rot);
}

// ─────────────────────────────────────────────────────────────────────────────
// ShapeHash — detect component changes without recreating bodies
// ─────────────────────────────────────────────────────────────────────────────
uint32_t PhysicsSystem::ShapeHash(Registry& registry, Entity e) const
{
    const auto& col = registry.Get<ColliderComponent>(e);
    glm::mat4   w   = TransformSystem::GetWorldMatrix(e, registry);
    glm::vec3   scl = { glm::length(glm::vec3(w[0])),
                        glm::length(glm::vec3(w[1])),
                        glm::length(glm::vec3(w[2])) };

    auto hashF = [](float f) -> uint32_t {
        uint32_t u; memcpy(&u, &f, sizeof(u)); return u * 2246822519u;
    };

    uint32_t h = static_cast<uint32_t>(col.shape) * 2654435761u;
    h ^= hashF(col.boxHalfExtents.x); h ^= hashF(col.boxHalfExtents.y); h ^= hashF(col.boxHalfExtents.z);
    h ^= hashF(col.radius);
    h ^= hashF(col.capsuleHalfHeight);
    h ^= hashF(col.staticFriction);
    h ^= hashF(col.dynamicFriction);
    h ^= hashF(col.restitution);
    h ^= hashF(scl.x); h ^= hashF(scl.y); h ^= hashF(scl.z);
    h ^= static_cast<uint32_t>(col.isTrigger);

    if (registry.Has<RigidbodyComponent>(e))
    {
        const auto& rb = registry.Get<RigidbodyComponent>(e);
        h ^= 0xDEADBEEFu;
        h ^= hashF(rb.mass);
        h ^= hashF(rb.linearDrag);
        h ^= hashF(rb.angularDrag);
        h ^= (rb.isKinematic     ? 0xAAAAAAAAu : 0u);
        h ^= (rb.useGravity      ? 0x55555555u : 0u);
        h ^= (rb.freezeRotationX ? 0x11111111u : 0u);
        h ^= (rb.freezeRotationY ? 0x22222222u : 0u);
        h ^= (rb.freezeRotationZ ? 0x44444444u : 0u);
    }

    if (registry.Has<LayerComponent>(e))
    {
        const auto& lc = registry.Get<LayerComponent>(e);
        h ^= static_cast<uint32_t>(lc.layer) * 0x12345678u;
        h ^= lc.layerMask * 0x87654321u;
    }

    return h;
}

// ─────────────────────────────────────────────────────────────────────────────
// CreateShape — build a Jolt shape from ColliderComponent
// ─────────────────────────────────────────────────────────────────────────────
JPH::Ref<JPH::Shape> PhysicsSystem::CreateShape(Registry& registry, Entity e)
{
    const auto& col = registry.Get<ColliderComponent>(e);

    glm::mat4 world = TransformSystem::GetWorldMatrix(e, registry);
    glm::vec3 scl   = { glm::length(glm::vec3(world[0])),
                        glm::length(glm::vec3(world[1])),
                        glm::length(glm::vec3(world[2])) };

    JPH::ShapeSettings::ShapeResult result;

    // Use a tiny convex radius to avoid the default 5cm padding that causes sticky collisions.
    // Must be < the smallest half-extent, so we clamp it.
    auto boxConvexRadius = [](JPH::Vec3 half) -> float {
        float smallest = std::min({ half.GetX(), half.GetY(), half.GetZ() });
        return std::min(0.002f, smallest * 0.1f);
    };

    switch (col.shape)
    {
    case ColliderShape::Box:
    {
        JPH::Vec3 half(
            std::max(std::abs(col.boxHalfExtents.x * scl.x), 0.001f),
            std::max(std::abs(col.boxHalfExtents.y * scl.y), 0.001f),
            std::max(std::abs(col.boxHalfExtents.z * scl.z), 0.001f));
        JPH::BoxShapeSettings settings(half, boxConvexRadius(half));
        result = settings.Create();
        break;
    }
    case ColliderShape::Sphere:
    {
        float maxScl = std::max({ scl.x, scl.y, scl.z });
        float r      = std::max(col.radius * maxScl, 0.001f);
        result = JPH::SphereShapeSettings(r).Create();
        break;
    }
    case ColliderShape::Capsule:
    {
        float maxXZ = std::max(scl.x, scl.z);
        float r     = std::max(col.radius           * maxXZ, 0.001f);
        float hh    = std::max(col.capsuleHalfHeight * scl.y, 0.001f);
        // Jolt CapsuleShape is already Y-aligned — no local rotation needed
        result = JPH::CapsuleShapeSettings(hh, r).Create();
        break;
    }
    }

    if (result.HasError())
    {
        std::cerr << "[Jolt] Shape creation error: " << result.GetError() << "\n";
        return nullptr;
    }

    return result.Get();
}


// ─────────────────────────────────────────────────────────────────────────────
// DestroyActor / DestroyController
// ─────────────────────────────────────────────────────────────────────────────
void PhysicsSystem::DestroyActor(Entity e)
{
    auto it = m_actors.find(e);
    if (it == m_actors.end()) return;

    if (!it->second.bodyId.IsInvalid())
    {
        uint32_t idx = it->second.bodyId.GetIndex();
        BodyIface().RemoveBody(it->second.bodyId);
        BodyIface().DestroyBody(it->second.bodyId);
        m_bodyEntityMap.erase(idx);
        m_bodyIsSensor.erase(idx);
    }
    m_actors.erase(it);
}

void PhysicsSystem::DestroyController(Entity e)
{
    auto it = m_controllers.find(e);
    if (it == m_controllers.end()) return;
    m_controllers.erase(it);
    m_pendingDisplacement.erase(e);
    m_controllerGrounded.erase(e);
    m_controllerVelocity.erase(e);
    m_controllerPrevContacts.erase(e);
}

// ─────────────────────────────────────────────────────────────────────────────
// SyncActors
// ─────────────────────────────────────────────────────────────────────────────
void PhysicsSystem::SyncActors(Registry& registry)
{
    if (!m_initialized) return;

    // Remove bodies whose ECS components were deleted
    std::vector<Entity> toRemove;
    for (auto& [e, state] : m_actors)
    {
        if (!registry.Has<ColliderComponent>(e) && !registry.Has<RigidbodyComponent>(e))
            toRemove.push_back(e);
    }
    for (Entity e : toRemove) DestroyActor(e);

    // Sync ColliderComponent entities
    registry.Each<ColliderComponent>([&](Entity e, ColliderComponent& col)
    {
        uint32_t newHash = ShapeHash(registry, e);
        auto it = m_actors.find(e);

        if (it != m_actors.end())
        {
            if (it->second.shapeHash == newHash)
            {
                // Update kinematic target or static pose
                ActorState& state = it->second;
                if (state.kinematic)
                {
                    BodyIface().SetPositionAndRotation(
                        state.bodyId,
                        EntityPosition(registry, e),
                        EntityRotation(registry, e),
                        JPH::EActivation::DontActivate);
                }
                else if (!state.dynamic)
                {
                    // Static body — teleport if editor moved it
                    JPH::RVec3 desiredPos = EntityPosition(registry, e);
                    JPH::Quat  desiredRot = EntityRotation(registry, e);
                    JPH::RVec3 currentPos = BodyIface().GetPosition(state.bodyId);
                    JPH::Quat  currentRot = BodyIface().GetRotation(state.bodyId);

                    float dist2 = (ToGlm(desiredPos) - ToGlm(currentPos)).length();
                    float dot   = std::abs(desiredRot.Dot(currentRot));
                    if (dist2 > 1e-3f || dot < 0.9999f)
                    {
                        BodyIface().SetPositionAndRotation(
                            state.bodyId, desiredPos, desiredRot,
                            JPH::EActivation::DontActivate);
                    }
                }
                return;
            }
            DestroyActor(e);
        }

        if (!registry.Has<TransformComponent>(e)) return;

        JPH::Ref<JPH::Shape> shape = CreateShape(registry, e);
        if (!shape) return;

        JPH::RVec3 pos = EntityPosition(registry, e);
        JPH::Quat  rot = EntityRotation(registry, e);

        ActorState state;
        state.shapeHash = newHash;

        bool isTrigger = col.isTrigger;

        if (registry.Has<RigidbodyComponent>(e))
        {
            const auto& rb = registry.Get<RigidbodyComponent>(e);
            bool forceStatic = !rb.isKinematic && !rb.useGravity && rb.mass <= 0.0f;

            JPH::EMotionType motionType = forceStatic
                ? JPH::EMotionType::Static
                : (rb.isKinematic ? JPH::EMotionType::Kinematic : JPH::EMotionType::Dynamic);

            JPH::ObjectLayer layer = (motionType == JPH::EMotionType::Static)
                ? PhysicsLayers::STATIC : PhysicsLayers::DYNAMIC;

            JPH::BodyCreationSettings bcs(shape, pos, rot, motionType, layer);
            bcs.mIsSensor = isTrigger;

            if (motionType == JPH::EMotionType::Dynamic)
            {
                float mass = rb.mass > 0.0f ? rb.mass : 1.0f;
                bcs.mOverrideMassProperties      = JPH::EOverrideMassProperties::CalculateInertia;
                bcs.mMassPropertiesOverride.mMass = mass;
                bcs.mLinearDamping  = rb.linearDrag;
                bcs.mAngularDamping = rb.angularDrag;
                bcs.mGravityFactor  = rb.useGravity ? 1.0f : 0.0f;

                JPH::EAllowedDOFs dofs = JPH::EAllowedDOFs::All;
                if (rb.freezeRotationX) dofs &= ~JPH::EAllowedDOFs::RotationX;
                if (rb.freezeRotationY) dofs &= ~JPH::EAllowedDOFs::RotationY;
                if (rb.freezeRotationZ) dofs &= ~JPH::EAllowedDOFs::RotationZ;
                bcs.mAllowedDOFs = dofs;
            }

            JPH::BodyID bodyId = BodyIface().CreateAndAddBody(
                bcs,
                (motionType == JPH::EMotionType::Static)
                    ? JPH::EActivation::DontActivate
                    : JPH::EActivation::Activate);

            if (bodyId.IsInvalid()) { std::cerr << "[Jolt] Failed to create body for entity " << e << "\n"; return; }

            BodyIface().SetUserData(bodyId, static_cast<uint64_t>(e));
            m_bodyEntityMap[bodyId.GetIndex()] = e;
            m_bodyIsSensor[bodyId.GetIndex()]  = isTrigger;

            state.bodyId    = bodyId;
            state.dynamic   = (motionType == JPH::EMotionType::Dynamic);
            state.kinematic = (motionType == JPH::EMotionType::Kinematic);
            state.useGravity= rb.useGravity;
            state.mass      = rb.mass;
        }
        else
        {
            // No rigidbody → static collider
            JPH::BodyCreationSettings bcs(shape, pos, rot,
                JPH::EMotionType::Static, PhysicsLayers::STATIC);
            bcs.mIsSensor = isTrigger;

            JPH::BodyID bodyId = BodyIface().CreateAndAddBody(bcs, JPH::EActivation::DontActivate);
            if (bodyId.IsInvalid()) { std::cerr << "[Jolt] Failed to create static body for entity " << e << "\n"; return; }

            BodyIface().SetUserData(bodyId, static_cast<uint64_t>(e));
            m_bodyEntityMap[bodyId.GetIndex()] = e;
            m_bodyIsSensor[bodyId.GetIndex()]  = isTrigger;

            state.bodyId  = bodyId;
            state.dynamic = false;
        }

        m_actors[e] = state;
    });

    // Sync RigidbodyComponent-only entities (no collider)
    registry.Each<RigidbodyComponent>([&](Entity e, RigidbodyComponent& rb)
    {
        if (registry.Has<ColliderComponent>(e)) return;
        if (!registry.Has<TransformComponent>(e)) return;

        auto hashF = [](float f) -> uint32_t {
            uint32_t u; memcpy(&u, &f, sizeof(u)); return u * 2246822519u;
        };
        uint32_t newHash = hashF(rb.mass) ^ hashF(rb.linearDrag) ^ hashF(rb.angularDrag)
                         ^ (rb.isKinematic     ? 0xAAAAAAAAu : 0u)
                         ^ (rb.useGravity      ? 0x55555555u : 0u)
                         ^ (rb.freezeRotationX ? 0x11111111u : 0u)
                         ^ (rb.freezeRotationY ? 0x22222222u : 0u)
                         ^ (rb.freezeRotationZ ? 0x44444444u : 0u);

        auto it = m_actors.find(e);
        if (it != m_actors.end())
        {
            if (it->second.shapeHash == newHash)
            {
                if (it->second.kinematic)
                {
                    BodyIface().SetPositionAndRotation(
                        it->second.bodyId,
                        EntityPosition(registry, e),
                        EntityRotation(registry, e),
                        JPH::EActivation::DontActivate);
                }
                return;
            }
            DestroyActor(e);
        }

        JPH::RVec3 pos = EntityPosition(registry, e);
        JPH::Quat  rot = EntityRotation(registry, e);

        float mass = rb.mass > 0.0f ? rb.mass : 1.0f;

        JPH::EMotionType motionType = rb.isKinematic
            ? JPH::EMotionType::Kinematic : JPH::EMotionType::Dynamic;

        // Tiny sphere as placeholder shape (no visual collider)
        auto shapeResult = JPH::SphereShapeSettings(0.01f).Create();
        if (shapeResult.HasError()) return;

        JPH::BodyCreationSettings bcs(shapeResult.Get(), pos, rot,
            motionType, PhysicsLayers::DYNAMIC);
        bcs.mOverrideMassProperties       = JPH::EOverrideMassProperties::CalculateInertia;
        bcs.mMassPropertiesOverride.mMass = mass;
        bcs.mLinearDamping  = rb.linearDrag;
        bcs.mAngularDamping = rb.angularDrag;
        bcs.mGravityFactor  = rb.useGravity ? 1.0f : 0.0f;
        bcs.mIsSensor       = true; // no visible shape, disable collision response

        JPH::BodyID bodyId = BodyIface().CreateAndAddBody(bcs, JPH::EActivation::Activate);
        if (bodyId.IsInvalid()) return;

        BodyIface().SetUserData(bodyId, static_cast<uint64_t>(e));
        m_bodyEntityMap[bodyId.GetIndex()] = e;
        m_bodyIsSensor[bodyId.GetIndex()]  = false;

        ActorState state;
        state.bodyId    = bodyId;
        state.dynamic   = (motionType == JPH::EMotionType::Dynamic);
        state.kinematic = (motionType == JPH::EMotionType::Kinematic);
        state.useGravity= rb.useGravity;
        state.mass      = rb.mass;
        state.shapeHash = newHash;
        m_actors[e] = state;
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// SyncControllers
// ─────────────────────────────────────────────────────────────────────────────
void PhysicsSystem::SyncControllers(Registry& registry)
{
    if (!m_initialized) return;

    std::vector<Entity> toRemove;
    for (auto& [e, ch] : m_controllers)
        if (!registry.Has<CharacterControllerComponent>(e))
            toRemove.push_back(e);
    for (Entity e : toRemove) DestroyController(e);

    registry.Each<CharacterControllerComponent>([&](Entity e, CharacterControllerComponent& cc)
    {
        if (!registry.Has<TransformComponent>(e)) return;

        if (m_controllers.count(e))
        {
            // Update position if teleported far away
            glm::mat4 world    = TransformSystem::GetWorldMatrix(e, registry);
            glm::vec3 desired  = glm::vec3(world[3]) + cc.center;
            glm::vec3 current  = ToGlm(m_controllers[e]->GetPosition());
            if (glm::length(desired - current) > 1.0f)
                m_controllers[e]->SetPosition(ToJoltR(desired));
            return;
        }

        glm::mat4 world    = TransformSystem::GetWorldMatrix(e, registry);
        glm::vec3 worldPos = glm::vec3(world[3]) + cc.center;

        float halfHeight = std::max((cc.height * 0.5f) - cc.radius, 0.01f);
        float radius     = std::max(cc.radius, 0.01f);

        auto capsuleResult = JPH::CapsuleShapeSettings(halfHeight, radius).Create();
        if (capsuleResult.HasError())
        {
            std::cerr << "[Jolt] CharacterVirtual capsule error: " << capsuleResult.GetError() << "\n";
            return;
        }

        JPH::CharacterVirtualSettings settings;
        settings.mMaxSlopeAngle              = JPH::DegreesToRadians(cc.slopeLimit);
        settings.mShape                      = capsuleResult.Get();
        settings.mCharacterPadding           = std::max(cc.skinWidth, 0.001f);
        settings.mUp                         = JPH::Vec3::sAxisY();
        settings.mSupportingVolume           = JPH::Plane(JPH::Vec3::sAxisY(), -radius);
        settings.mPenetrationRecoverySpeed   = 1.0f;
        settings.mPredictiveContactDistance  = 0.1f;

        JPH::Ref<JPH::CharacterVirtual> character = new JPH::CharacterVirtual(
            &settings, ToJoltR(worldPos), JPH::Quat::sIdentity(), 0, m_physicsSystem.get());

        m_controllers[e] = character;
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// Step
// ─────────────────────────────────────────────────────────────────────────────
void PhysicsSystem::Step(float dt)
{
    if (!m_initialized || dt <= 0.0f) return;

    m_events.clear();

    // Advance Jolt physics
    m_physicsSystem->Update(dt, 1, m_tempAllocator.get(), m_jobSystem.get());

    // Update character virtuals
    AllBroadPhaseLayerFilter bpFilter;
    AllObjectLayerFilter     objFilter;
    JPH::BodyFilter          bodyFilter;
    JPH::ShapeFilter         shapeFilter;

    for (auto& [e, character] : m_controllers)
    {
        if (!character) continue;

        glm::vec3 disp(0.0f);
        auto dispIt = m_pendingDisplacement.find(e);
        if (dispIt != m_pendingDisplacement.end())
        {
            disp = dispIt->second;
            m_pendingDisplacement.erase(dispIt);
        }

        // Convert displacement → velocity and set it (gravity handled by script)
        glm::vec3 vel = (dt > 1e-6f) ? (disp / dt) : glm::vec3(0.0f);
        character->SetLinearVelocity(ToJolt(vel));

        // Update with zero internal gravity (Lua scripts integrate gravity themselves)
        character->Update(dt, JPH::Vec3::sZero(), bpFilter, objFilter,
                          bodyFilter, shapeFilter, *m_tempAllocator);

        bool grounded = (character->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround);
        m_controllerGrounded[e] = grounded;
        m_controllerVelocity[e] = disp;

        // ── Collision events for CharacterVirtual ────────────────────────
        // CharacterVirtual is not a Jolt body so it never triggers ContactListener.
        // Use GetActiveContacts() + frame-to-frame diff to generate Enter/Stay/Exit.
        std::unordered_set<uint32_t> currentContacts;

        for (const auto& contact : character->GetActiveContacts())
        {
            // mHadCollision=false means predictive (speculative) contact — skip
            if (!contact.mHadCollision) continue;

            uint32_t idx = contact.mBodyB.GetIndex();
            auto entityIt = m_bodyEntityMap.find(idx);
            if (entityIt == m_bodyEntityMap.end()) continue;

            Entity other = entityIt->second;
            bool isSensor = m_bodyIsSensor.count(idx) && m_bodyIsSensor.at(idx);

            glm::vec3 pos    = ToGlm(contact.mPosition);
            glm::vec3 normal = ToGlm(contact.mContactNormal);

            auto& prev = m_controllerPrevContacts[e];
            CollisionEventType type = prev.count(idx)
                ? CollisionEventType::Stay
                : CollisionEventType::Enter;

            m_events.push_back({ type, e, other, pos, normal, isSensor });
            currentContacts.insert(idx);
        }

        // Exit: bodies that were in contact last frame but not this frame
        auto& prev = m_controllerPrevContacts[e];
        for (uint32_t idx : prev)
        {
            if (currentContacts.count(idx)) continue;
            auto entityIt = m_bodyEntityMap.find(idx);
            if (entityIt == m_bodyEntityMap.end()) continue;
            Entity other   = entityIt->second;
            bool isSensor  = m_bodyIsSensor.count(idx) && m_bodyIsSensor.at(idx);
            m_events.push_back({ CollisionEventType::Exit, e, other, {}, {}, isSensor });
        }

        prev = std::move(currentContacts);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// WriteBack
// ─────────────────────────────────────────────────────────────────────────────
void PhysicsSystem::WriteBack(Registry& registry)
{
    if (!m_initialized) return;

    for (auto& [e, state] : m_actors)
    {
        if (!state.dynamic || state.kinematic) continue;
        if (!registry.Has<TransformComponent>(e)) continue;

        JPH::RVec3 jPos = BodyIface().GetPosition(state.bodyId);
        JPH::Quat  jRot = BodyIface().GetRotation(state.bodyId);

        glm::vec3 pos = ToGlm(jPos);
        glm::quat rot = ToGlm(jRot);

        if (registry.Has<ColliderComponent>(e))
            pos -= rot * registry.Get<ColliderComponent>(e).center;

        auto& tr = registry.Get<TransformComponent>(e);
        tr.position = pos;

        bool allFrozen = false;
        if (registry.Has<RigidbodyComponent>(e))
        {
            const auto& rb2 = registry.Get<RigidbodyComponent>(e);
            allFrozen = rb2.freezeRotationX && rb2.freezeRotationY && rb2.freezeRotationZ;
        }
        if (!allFrozen)
        {
            // Store quaternion directly — avoids gimbal lock at ±90° pitch that
            // occurs when converting to YXZ Euler angles. GetMatrix() will use
            // physicsQuat instead of Euler angles when usePhysicsQuat is true.
            tr.usePhysicsQuat = true;
            tr.physicsQuat    = rot;

            // Also update Euler angles so the inspector shows a reasonable value.
            glm::mat4 rotMat = glm::mat4_cast(rot);
            float yRad, xRad, zRad;
            glm::extractEulerAngleYXZ(rotMat, yRad, xRad, zRad);
            tr.rotation = glm::degrees(glm::vec3(xRad, yRad, zRad));
        }

        if (registry.Has<RigidbodyComponent>(e))
        {
            auto& rb = registry.Get<RigidbodyComponent>(e);
            JPH::Vec3 lv = BodyIface().GetLinearVelocity(state.bodyId);
            JPH::Vec3 av = BodyIface().GetAngularVelocity(state.bodyId);
            rb.linearVelocity  = ToGlm(lv);
            rb.angularVelocity = ToGlm(av);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// WriteBackControllers
// ─────────────────────────────────────────────────────────────────────────────
void PhysicsSystem::WriteBackControllers(Registry& registry)
{
    if (!m_initialized) return;

    for (auto& [e, character] : m_controllers)
    {
        if (!character) continue;
        if (!registry.Has<TransformComponent>(e)) continue;
        if (!registry.Has<CharacterControllerComponent>(e)) continue;

        glm::vec3 p = ToGlm(character->GetPosition());

        auto& tr = registry.Get<TransformComponent>(e);
        auto& cc = registry.Get<CharacterControllerComponent>(e);

        tr.position = p - cc.center;

        if (m_controllerGrounded.count(e))
        {
            cc.isGrounded = m_controllerGrounded[e];
            m_controllerGrounded.erase(e);
        }
        if (m_controllerVelocity.count(e))
        {
            cc.velocity = m_controllerVelocity[e];
            m_controllerVelocity.erase(e);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// DispatchEvents
// ─────────────────────────────────────────────────────────────────────────────
void PhysicsSystem::DispatchEvents(Registry& registry)
{
    for (const RawEvent& raw : m_events)
    {
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
// MoveController
// ─────────────────────────────────────────────────────────────────────────────
void PhysicsSystem::MoveController(Entity e, const glm::vec3& displacement, float /*dt*/)
{
    if (!m_initialized) return;
    if (!m_controllers.count(e)) return;
    m_pendingDisplacement[e] = displacement;
}

// ─────────────────────────────────────────────────────────────────────────────
// RemoveEntity / MarkDirty
// ─────────────────────────────────────────────────────────────────────────────
void PhysicsSystem::RemoveEntity(Entity e)
{
    DestroyActor(e);
    DestroyController(e);
}

void PhysicsSystem::MarkDirty(Entity e)
{
    auto it = m_actors.find(e);
    if (it != m_actors.end())
        it->second.shapeHash = 0;

    DestroyController(e);
}

// ─────────────────────────────────────────────────────────────────────────────
// Raycast
// ─────────────────────────────────────────────────────────────────────────────
RaycastHit PhysicsSystem::Raycast(const glm::vec3& origin,
                                  const glm::vec3& direction,
                                  float            maxDistance,
                                  uint32_t         layerMask) const
{
    RaycastHit result;
    if (!m_initialized) return result;

    float len = glm::length(direction);
    if (len < 1e-6f) return result;
    glm::vec3 dir = direction / len;

    JPH::RRayCast ray;
    ray.mOrigin    = ToJoltR(origin);
    ray.mDirection = ToJolt(dir) * maxDistance;

    // Accept all layers — per-entity layerMask is checked after the hit
    AllBroadPhaseLayerFilter bpFilter;
    AllObjectLayerFilter     objFilter;

    JPH::RayCastResult hitResult;
    bool found = m_physicsSystem->GetNarrowPhaseQuery().CastRay(
        ray, hitResult, bpFilter, objFilter);

    if (!found) return result;

    result.hit      = true;
    result.distance = hitResult.mFraction * maxDistance;
    JPH::RVec3 hitPos = ray.mOrigin + hitResult.mFraction * ray.mDirection;
    result.position = ToGlm(hitPos);

    // Look up entity and surface normal
    {
        JPH::BodyLockRead lock(m_physicsSystem->GetBodyLockInterface(), hitResult.mBodyID);
        if (lock.Succeeded())
        {
            const JPH::Body& body = lock.GetBody();
            result.entity = static_cast<Entity>(body.GetUserData());

            // Layer mask filter
            uint32_t bodyLayer = 1u << body.GetObjectLayer();
            if (!(bodyLayer & layerMask))
            {
                result.hit = false;
                return result;
            }

            JPH::Vec3 normal = body.GetWorldSpaceSurfaceNormal(
                hitResult.mSubShapeID2, JPH::Vec3(hitPos));
            result.normal = ToGlm(normal);
        }
    }

    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// SetVelocity / AddForce / AddImpulse / GetVelocity
// ─────────────────────────────────────────────────────────────────────────────
void PhysicsSystem::SetVelocity(Entity e, const glm::vec3& velocity)
{
    auto it = m_actors.find(e);
    if (it == m_actors.end() || !it->second.dynamic || it->second.kinematic) return;
    BodyIface().SetLinearVelocity(it->second.bodyId, ToJolt(velocity));
}

void PhysicsSystem::AddForce(Entity e, const glm::vec3& force)
{
    auto it = m_actors.find(e);
    if (it == m_actors.end() || !it->second.dynamic || it->second.kinematic) return;
    BodyIface().AddForce(it->second.bodyId, ToJolt(force));
}

void PhysicsSystem::AddImpulse(Entity e, const glm::vec3& impulse)
{
    auto it = m_actors.find(e);
    if (it == m_actors.end() || !it->second.dynamic || it->second.kinematic) return;
    BodyIface().AddImpulse(it->second.bodyId, ToJolt(impulse));
}

glm::vec3 PhysicsSystem::GetVelocity(Entity e) const
{
    auto it = m_actors.find(e);
    if (it == m_actors.end() || !it->second.dynamic) return {};
    return ToGlm(BodyIface().GetLinearVelocity(it->second.bodyId));
}

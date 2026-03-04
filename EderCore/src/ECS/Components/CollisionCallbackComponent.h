#pragma once
#include <functional>
#include <glm/glm.hpp>
#include "ECS/Entity.h"

// ─────────────────────────────────────────────────────────────────────────────
//  CollisionEvent — produced by PhysicsSystem after each simulation step.
//  Shared between PhysicsSystem and CollisionCallbackComponent so the
//  component header has no dependency on PhysicsSystem.
// ─────────────────────────────────────────────────────────────────────────────
enum class CollisionEventType { Enter, Stay, Exit };

struct CollisionEvent
{
    CollisionEventType type    = CollisionEventType::Enter;
    Entity             self    = NULL_ENTITY; // the entity whose callback is being fired
    Entity             other   = NULL_ENTITY; // the entity on the other side
    glm::vec3          point   = {};          // approx. first contact point (world space)
    glm::vec3          normal  = {};          // contact normal pointing away from `other`
    bool               trigger = false;       // true when one of the shapes is a trigger
};

// ─────────────────────────────────────────────────────────────────────────────
//  CollisionCallbackComponent
//  Attach to any entity that has a ColliderComponent to receive physics events.
//  All callbacks are optional — leave unset to ignore that event type.
//
//  Example:
//      auto& cb = registry.Add<CollisionCallbackComponent>(e);
//      cb.onCollisionEnter = [](const CollisionEvent& ev) {
//          printf("Hit entity %u\n", ev.other);
//      };
// ─────────────────────────────────────────────────────────────────────────────
struct CollisionCallbackComponent
{
    // ── Solid collisions ─────────────────────────────────────────────────────
    std::function<void(const CollisionEvent&)> onCollisionEnter;
    std::function<void(const CollisionEvent&)> onCollisionStay;
    std::function<void(const CollisionEvent&)> onCollisionExit;

    // ── Trigger volumes ──────────────────────────────────────────────────────
    std::function<void(const CollisionEvent&)> onTriggerEnter;
    std::function<void(const CollisionEvent&)> onTriggerExit;
};

#pragma once
#include <glm/glm.hpp>

// ─────────────────────────────────────────────────────────────────────────────
// CharacterControllerComponent
//
//  Capsule-based kinematic character controller built on top of Jolt Physics
//  CharacterVirtual.  Uses sweep-based collision so it can walk, step
//  over small obstacles and slide along slopes without needing a rigidbody.
//
//  NOTE: A CharacterControllerComponent is mutually exclusive with
//        ColliderComponent + RigidbodyComponent on the same entity.
// ─────────────────────────────────────────────────────────────────────────────
struct CharacterControllerComponent
{
    // ── Shape ─────────────────────────────────────────────────────────────────
    float     radius     = 0.30f;    // capsule radius (metres)
    float     height     = 1.80f;   // total capsule height (metres)

    // ── Behaviour ─────────────────────────────────────────────────────────────
    float     stepOffset  = 0.30f;  // max step height the controller can climb
    float     slopeLimit  = 45.0f;  // max walkable slope angle (degrees)
    float     skinWidth   = 0.05f;  // contact offset / skin width — 5cm matches Unity default, prevents edge-catching

    // ── Local offset ──────────────────────────────────────────────────────────
    glm::vec3 center      = {0.0f, 0.0f, 0.0f}; // local pivot offset

    // ── Runtime state (NOT serialised, reset each physics step) ──────────────
    bool      isGrounded  = false;
    glm::vec3 velocity    = {0.0f, 0.0f, 0.0f}; // displacement last frame
};

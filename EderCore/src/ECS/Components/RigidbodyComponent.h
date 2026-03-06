#pragma once
#include <glm/glm.hpp>

struct RigidbodyComponent
{
    float     mass        = 1.0f;
    bool      isKinematic = false;  // true = driven by transform, not by physics
    bool      useGravity  = true;
    float     linearDrag  = 0.01f;
    float     angularDrag = 0.05f;

    // Freeze individual rotation axes (maps to Jolt EAllowedDOFs).
    // Useful to stop rigidbodies spinning on uneven / triangle-mesh surfaces.
    bool      freezeRotationX = false;
    bool      freezeRotationY = false;
    bool      freezeRotationZ = false;

    // Read-only — written back by PhysicsSystem after each simulation step
    glm::vec3 linearVelocity  = {};
    glm::vec3 angularVelocity = {};
};

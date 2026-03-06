#pragma once
#include <glm/glm.hpp>

enum class ColliderShape { Box, Sphere, Capsule, Mesh };

struct ColliderComponent
{
    ColliderShape shape        = ColliderShape::Box;

    // Box
    glm::vec3     boxHalfExtents = { 0.5f, 0.5f, 0.5f };

    // Sphere
    float         radius         = 0.5f;

    // Capsule (total height = 2*halfHeight + 2*radius)
    float         capsuleHalfHeight = 0.5f;

    // Local offset relative to entity origin
    glm::vec3     center         = { 0.0f, 0.0f, 0.0f };

    // Material
    float         staticFriction  = 0.3f;
    float         dynamicFriction = 0.3f;
    float         restitution     = 0.3f;

    bool          isTrigger       = false;
};

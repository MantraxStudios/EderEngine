#pragma once
#include "ECS/Entity.h"
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// HierarchyComponent
// Stores parent / children relationships for the transform hierarchy.
// Transform values in TransformComponent are always LOCAL (relative to parent).
// Use TransformSystem to attach / detach while preserving world position.
// ─────────────────────────────────────────────────────────────────────────────
struct HierarchyComponent
{
    Entity              parent   = NULL_ENTITY;  // NULL_ENTITY = root
    std::vector<Entity> children;
};

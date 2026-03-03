#pragma once
#include "ECS/Registry.h"
#include "ECS/Entity.h"
#include "ECS/Components/TransformComponent.h"
#include "ECS/Components/HierarchyComponent.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/euler_angles.hpp>
#include <algorithm>

// ─────────────────────────────────────────────────────────────────────────────
// TransformSystem
//
// All TransformComponent values are LOCAL (relative to parent).
// Use these utilities whenever world-space data is needed, or
// when attaching / detaching entities from a parent.
//
// Attach / Detach preserve the entity's visual world position — the mesh
// never "jumps" when the hierarchy changes.
// ─────────────────────────────────────────────────────────────────────────────
class TransformSystem
{
public:
    // ── World matrix ─────────────────────────────────────────────────────────
    // Returns the entity's world-space matrix, walking up the parent chain.
    static glm::mat4 GetWorldMatrix(Entity e, const Registry& registry)
    {
        if (!registry.Has<TransformComponent>(e)) return glm::mat4(1.0f);

        const auto& tr    = registry.Get<TransformComponent>(e);
        glm::mat4   local = tr.GetMatrix();

        if (registry.Has<HierarchyComponent>(e))
        {
            const auto& hier = registry.Get<HierarchyComponent>(e);
            if (hier.parent != NULL_ENTITY
                && registry.Has<TransformComponent>(hier.parent))
            {
                return GetWorldMatrix(hier.parent, registry) * local;
            }
        }
        return local;
    }

    // ── Attach ───────────────────────────────────────────────────────────────
    // Parents 'child' under 'newParent'.
    // The entity's world position / rotation / scale does NOT change.
    static void Attach(Entity child, Entity newParent, Registry& registry)
    {
        if (child == NULL_ENTITY || newParent == NULL_ENTITY) return;
        if (child == newParent)                               return;
        if (IsDescendant(newParent, child, registry))         return; // circular guard

        // Snapshot child world matrix BEFORE any change
        glm::mat4 worldMat = GetWorldMatrix(child, registry);

        // Detach from old parent (no world-transform change yet)
        DetachLink(child, registry);

        // Compute new local = inverse(newParentWorld) * childWorld
        glm::mat4 parentWorld = GetWorldMatrix(newParent, registry);
        glm::mat4 newLocal    = glm::inverse(parentWorld) * worldMat;

        // Write decomposed local back to child's TransformComponent
        DecomposeInto(newLocal, registry.Get<TransformComponent>(child));

        // Wire up hierarchy
        EnsureHierarchy(child, registry).parent = newParent;
        EnsureHierarchy(newParent, registry).children.push_back(child);
    }

    // ── Detach ───────────────────────────────────────────────────────────────
    // Removes 'child' from its parent and promotes its world transform to
    // local (root) values.  The entity's visual position does NOT change.
    static void Detach(Entity child, Registry& registry)
    {
        if (!registry.Has<HierarchyComponent>(child)) return;
        if (registry.Get<HierarchyComponent>(child).parent == NULL_ENTITY) return;

        glm::mat4 worldMat = GetWorldMatrix(child, registry);
        DetachLink(child, registry);
        DecomposeInto(worldMat, registry.Get<TransformComponent>(child));
    }

    // ── PrepareDestroy ───────────────────────────────────────────────────────
    // Must be called BEFORE registry.Destroy(e).
    // Detaches all children (makes them roots) and detaches the entity itself.
    static void PrepareDestroy(Entity e, Registry& registry)
    {
        // Promote children to root first (copy list, Detach mutates it)
        if (registry.Has<HierarchyComponent>(e))
        {
            auto children = registry.Get<HierarchyComponent>(e).children;
            for (Entity c : children)
                Detach(c, registry);
        }
        Detach(e, registry);
    }

    // ── IsDescendant ─────────────────────────────────────────────────────────
    // Returns true if 'candidate' is the same as 'ancestor' or is somewhere
    // below it in the hierarchy.  Used to prevent circular chains.
    static bool IsDescendant(Entity candidate, Entity ancestor, const Registry& registry)
    {
        Entity cur = candidate;
        while (cur != NULL_ENTITY)
        {
            if (cur == ancestor) return true;
            if (!registry.Has<HierarchyComponent>(cur)) break;
            cur = registry.Get<HierarchyComponent>(cur).parent;
        }
        return false;
    }

    // ── DecomposeInto (public) ────────────────────────────────────────────────
    // Decomposes a mat4 into a TransformComponent (position, rotation in
    // degrees YXZ, scale).  Matches the YXZ convention used in GetMatrix().
    static void DecomposeInto(const glm::mat4& m, TransformComponent& tr)
    {
        // Translation
        tr.position = glm::vec3(m[3]);

        // Scale — length of each column
        tr.scale.x = glm::length(glm::vec3(m[0]));
        tr.scale.y = glm::length(glm::vec3(m[1]));
        tr.scale.z = glm::length(glm::vec3(m[2]));

        // Guard against near-zero scale
        const float eps = 1e-6f;
        if (tr.scale.x < eps) tr.scale.x = eps;
        if (tr.scale.y < eps) tr.scale.y = eps;
        if (tr.scale.z < eps) tr.scale.z = eps;

        // Rotation — strip scale, then extract YXZ Euler (degrees)
        glm::mat4 rot = m;
        rot[0] = glm::vec4(glm::vec3(m[0]) / tr.scale.x, 0.0f);
        rot[1] = glm::vec4(glm::vec3(m[1]) / tr.scale.y, 0.0f);
        rot[2] = glm::vec4(glm::vec3(m[2]) / tr.scale.z, 0.0f);
        rot[3] = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);

        float yRad, xRad, zRad;
        glm::extractEulerAngleYXZ(rot, yRad, xRad, zRad);
        tr.rotation = glm::degrees(glm::vec3(xRad, yRad, zRad));
    }

private:
    // Unlinks child from its parent in the hierarchy without changing transforms.
    static void DetachLink(Entity child, Registry& registry)
    {
        if (!registry.Has<HierarchyComponent>(child)) return;
        auto& hier = registry.Get<HierarchyComponent>(child);

        if (hier.parent != NULL_ENTITY && registry.Has<HierarchyComponent>(hier.parent))
        {
            auto& ch = registry.Get<HierarchyComponent>(hier.parent).children;
            ch.erase(std::remove(ch.begin(), ch.end(), child), ch.end());
        }
        hier.parent = NULL_ENTITY;
    }

    // Returns the entity's HierarchyComponent, creating it if missing.
    static HierarchyComponent& EnsureHierarchy(Entity e, Registry& registry)
    {
        if (!registry.Has<HierarchyComponent>(e))
            registry.Add<HierarchyComponent>(e);
        return registry.Get<HierarchyComponent>(e);
    }
};

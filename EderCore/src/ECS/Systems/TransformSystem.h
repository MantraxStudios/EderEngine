#pragma once
#include "ECS/Registry.h"
#include "ECS/Entity.h"
#include "ECS/Components/TransformComponent.h"
#include "ECS/Components/HierarchyComponent.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <algorithm>

// ─────────────────────────────────────────────────────────────────────────────
// TransformSystem (v3 - COMPLETO)
// ─────────────────────────────────────────────────────────────────────────────
class TransformSystem
{
public:
    static glm::mat4 GetLocalMatrix(Entity e, const Registry& registry)
    {
        if (!registry.Has<TransformComponent>(e)) return glm::mat4(1.0f);
        return registry.Get<TransformComponent>(e).GetMatrix();
    }

    static glm::mat4 GetWorldMatrix(Entity e, const Registry& registry)
    {
        if (!registry.Has<TransformComponent>(e)) return glm::mat4(1.0f);
        glm::mat4 local = GetLocalMatrix(e, registry);

        if (registry.Has<HierarchyComponent>(e))
        {
            const auto& hier = registry.Get<HierarchyComponent>(e);
            if (hier.parent != NULL_ENTITY && registry.Has<TransformComponent>(hier.parent))
            {
                return GetWorldMatrix(hier.parent, registry) * local;
            }
        }
        return local;
    }

    // ── Getters (Posición y Rotación) ────────────────────────────────────────

    static glm::vec3 GetLocalPosition(Entity e, const Registry& registry)
    {
        return registry.Has<TransformComponent>(e) ? registry.Get<TransformComponent>(e).position : glm::vec3(0.0f);
    }

    static glm::vec3 GetWorldPosition(Entity e, const Registry& registry)
    {
        glm::mat4 world = GetWorldMatrix(e, registry);
        return glm::vec3(world[3]);
    }

    static glm::vec3 GetLocalRotation(Entity e, const Registry& registry)
    {
        return registry.Has<TransformComponent>(e) ? registry.Get<TransformComponent>(e).rotation : glm::vec3(0.0f);
    }

    static glm::vec3 GetWorldRotation(Entity e, const Registry& registry)
    {
        glm::mat4 world = GetWorldMatrix(e, registry);
        
        // Extraer escala para normalizar la matriz de rotación
        glm::vec3 scale;
        scale.x = glm::length(glm::vec3(world[0]));
        scale.y = glm::length(glm::vec3(world[1]));
        scale.z = glm::length(glm::vec3(world[2]));

        glm::mat4 rot = glm::mat4(1.0f);
        rot[0] = world[0] / (scale.x > 1e-6f ? scale.x : 1.0f);
        rot[1] = world[1] / (scale.y > 1e-6f ? scale.y : 1.0f);
        rot[2] = world[2] / (scale.z > 1e-6f ? scale.z : 1.0f);

        float yRad, xRad, zRad;
        glm::extractEulerAngleYXZ(rot, yRad, xRad, zRad);
        return glm::degrees(glm::vec3(xRad, yRad, zRad));
    }

    // ── Setters (Posición y Rotación) ────────────────────────────────────────

    static void SetLocalPosition(Entity e, const glm::vec3& pos, Registry& registry)
    {
        if (registry.Has<TransformComponent>(e))
            registry.Get<TransformComponent>(e).position = pos;
    }

    static void SetWorldPosition(Entity e, const glm::vec3& worldPos, Registry& registry)
    {
        if (!registry.Has<TransformComponent>(e)) return;

        Entity parent = NULL_ENTITY;
        if (registry.Has<HierarchyComponent>(e))
            parent = registry.Get<HierarchyComponent>(e).parent;

        if (parent == NULL_ENTITY || !registry.Has<TransformComponent>(parent))
        {
            registry.Get<TransformComponent>(e).position = worldPos;
        }
        else
        {
            // Convertir posición de mundo a local relativa al padre
            glm::mat4 invParentWorld = glm::inverse(GetWorldMatrix(parent, registry));
            glm::vec4 localPos = invParentWorld * glm::vec4(worldPos, 1.0f);
            registry.Get<TransformComponent>(e).position = glm::vec3(localPos);
        }
    }

    static void SetLocalRotation(Entity e, const glm::vec3& rotDegrees, Registry& registry)
    {
        if (registry.Has<TransformComponent>(e))
            registry.Get<TransformComponent>(e).rotation = rotDegrees;
    }

    static void SetWorldRotation(Entity e, const glm::vec3& worldRotDegrees, Registry& registry)
    {
        if (!registry.Has<TransformComponent>(e)) return;

        Entity parent = NULL_ENTITY;
        if (registry.Has<HierarchyComponent>(e))
            parent = registry.Get<HierarchyComponent>(e).parent;

        if (parent == NULL_ENTITY || !registry.Has<TransformComponent>(parent))
        {
            registry.Get<TransformComponent>(e).rotation = worldRotDegrees;
        }
        else
        {
            // Convertir rotación de mundo a local relativa al padre
            // worldRot = parentRot * localRot  =>  localRot = inv(parentRot) * worldRot
            glm::mat4 worldRotMat = glm::eulerAngleYXZ(
                glm::radians(worldRotDegrees.y), 
                glm::radians(worldRotDegrees.x), 
                glm::radians(worldRotDegrees.z)
            );
            
            glm::mat4 parentWorld = GetWorldMatrix(parent, registry);
            // Extraer solo la rotación del padre (normalizando columnas)
            glm::mat4 parentRotMat = glm::mat4(1.0f);
            parentRotMat[0] = glm::normalize(parentWorld[0]);
            parentRotMat[1] = glm::normalize(parentWorld[1]);
            parentRotMat[2] = glm::normalize(parentWorld[2]);

            glm::mat4 localRotMat = glm::inverse(parentRotMat) * worldRotMat;

            float yRad, xRad, zRad;
            glm::extractEulerAngleYXZ(localRotMat, yRad, xRad, zRad);
            registry.Get<TransformComponent>(e).rotation = glm::degrees(glm::vec3(xRad, yRad, zRad));
        }
    }

    // ── Jerarquía ────────────────────────────────────────────────────────────

    static void Attach(Entity child, Entity newParent, Registry& registry)
    {
        if (child == NULL_ENTITY || newParent == NULL_ENTITY || child == newParent) return;
        if (IsDescendant(newParent, child, registry)) return;

        glm::mat4 childWorld = GetWorldMatrix(child, registry);
        DetachLink(child, registry);

        glm::mat4 parentWorld = GetWorldMatrix(newParent, registry);
        glm::mat4 newLocal = glm::inverse(parentWorld) * childWorld;

        DecomposeInto(newLocal, registry.Get<TransformComponent>(child));

        auto& childHier = EnsureHierarchy(child, registry);
        childHier.parent = newParent;
        
        auto& parentHier = EnsureHierarchy(newParent, registry);
        if (std::find(parentHier.children.begin(), parentHier.children.end(), child) == parentHier.children.end()) {
            parentHier.children.push_back(child);
        }
    }

    static void Detach(Entity child, Registry& registry)
    {
        if (!registry.Has<HierarchyComponent>(child)) return;
        if (registry.Get<HierarchyComponent>(child).parent == NULL_ENTITY) return;

        glm::mat4 worldMat = GetWorldMatrix(child, registry);
        DetachLink(child, registry);
        DecomposeInto(worldMat, registry.Get<TransformComponent>(child));
    }

    static void PrepareDestroy(Entity e, Registry& registry)
    {
        if (registry.Has<HierarchyComponent>(e))
        {
            auto children = registry.Get<HierarchyComponent>(e).children;
            for (Entity c : children)
                Detach(c, registry);
        }
        Detach(e, registry);
    }

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

    static void DecomposeInto(const glm::mat4& m, TransformComponent& tr)
    {
        tr.position = glm::vec3(m[3]);
        tr.scale.x = glm::length(glm::vec3(m[0]));
        tr.scale.y = glm::length(glm::vec3(m[1]));
        tr.scale.z = glm::length(glm::vec3(m[2]));

        const float eps = 1e-6f;
        glm::vec3 safeScale = glm::max(tr.scale, glm::vec3(eps));

        glm::mat4 rot = glm::mat4(1.0f);
        rot[0] = glm::vec4(glm::vec3(m[0]) / safeScale.x, 0.0f);
        rot[1] = glm::vec4(glm::vec3(m[1]) / safeScale.y, 0.0f);
        rot[2] = glm::vec4(glm::vec3(m[2]) / safeScale.z, 0.0f);

        float yRad, xRad, zRad;
        glm::extractEulerAngleYXZ(rot, yRad, xRad, zRad);
        tr.rotation = glm::degrees(glm::vec3(xRad, yRad, zRad));
    }

private:
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

    static HierarchyComponent& EnsureHierarchy(Entity e, Registry& registry)
    {
        if (!registry.Has<HierarchyComponent>(e))
            registry.Add<HierarchyComponent>(e);
        return registry.Get<HierarchyComponent>(e);
    }
};

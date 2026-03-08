#pragma once
#include "DLLHeader.h"
#include <glm/glm.hpp>
#include <vector>

class EDERGRAPHICS_API VulkanMesh;
class EDERGRAPHICS_API Material;

struct SceneObject
{
    VulkanMesh* mesh        = nullptr;
    Material*   material    = nullptr;
    glm::mat4   worldMatrix = glm::mat4(1.0f);
    uint32_t    entityId    = 0;    // links to ECS entity (0 = not linked)
    bool        isSkinned   = false; // true = drawn separately with per-entity bone matrices

    // Per-submesh material overrides. If non-empty, size must match mesh->GetSubmeshCount().
    // Object is drawn non-instanced, one DrawSubMesh per slot.
    std::vector<Material*> subMeshMaterials;
};

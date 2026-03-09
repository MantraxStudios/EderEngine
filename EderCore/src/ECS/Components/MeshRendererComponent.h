#pragma once
#include <string>
#include <cstdint>
#include <vector>
#include <glm/glm.hpp>

// Local-space transform offset for a single submesh (set at runtime via scripting).
struct SubMeshTransform
{
    glm::vec3 position    = {0.f, 0.f, 0.f};
    glm::vec3 rotEulerDeg = {0.f, 0.f, 0.f};  // XYZ euler degrees
    glm::vec3 scale       = {1.f, 1.f, 1.f};
};

struct MeshRendererComponent
{
    uint64_t    meshGuid     = 0;                  // GUID from AssetManager — rename/move safe
    std::string meshPath     = "assets/Capoeira.fbx"; // resolved from GUID at load time (display/fallback)
    uint64_t    materialGuid = 0;                  // GUID of .mat asset (0 = use materialName fallback)
    std::string materialName = "default";
    bool        castShadow   = true;
    bool        visible      = true;

    // Per-submesh material overrides (optional).
    // If populated, size should match the mesh's GetSubmeshCount().
    std::vector<uint64_t>    subMeshMaterialGuids;   // GUID per slot (0 = inherit base material)
    std::vector<std::string> subMeshMaterialNames;   // name per slot (fallback when GUID is 0)

    // Per-submesh local transform offsets (runtime only, set via scripting).
    // Empty = no local offsets (all submeshes follow the parent worldMatrix).
    std::vector<SubMeshTransform> subMeshTransforms;

    // Per-submesh ECS entity (0 = none, uses subMeshTransforms instead).
    // When set, the sub-mesh world transform comes from that entity's TransformComponent
    // so it can carry full ECS components: physics, scripts, colliders, etc.
    std::vector<uint32_t> subMeshEntityIds;
};

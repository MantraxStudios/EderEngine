#pragma once
#include <string>
#include <cstdint>
#include <vector>

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
};

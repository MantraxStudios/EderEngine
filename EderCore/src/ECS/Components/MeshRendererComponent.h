#pragma once
#include <string>
#include <cstdint>

struct MeshRendererComponent
{
    uint64_t    meshGuid     = 0;                  // GUID from AssetManager — rename/move safe
    std::string meshPath     = "assets/Capoeira.fbx"; // resolved from GUID at load time (display/fallback)
    uint64_t    materialGuid = 0;                  // GUID of .mat asset (0 = use materialName fallback)
    std::string materialName = "default";
    bool        castShadow   = true;
    bool        visible      = true;
};

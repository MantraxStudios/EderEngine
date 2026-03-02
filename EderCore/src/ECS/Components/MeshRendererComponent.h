#pragma once
#include <string>

struct MeshRendererComponent
{
    std::string meshPath     = "assets/box.fbx"; // path to the mesh asset
    std::string materialName = "default";        // key into MaterialManager
    bool        castShadow  = true;
    bool        visible     = true;
};

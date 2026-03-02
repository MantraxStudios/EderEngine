#pragma once
#include <string>

struct MeshRendererComponent
{
    std::string meshPath;     // path to the mesh asset
    std::string materialPath; // path to the material / texture asset
    bool        castShadow  = true;
    bool        visible     = true;
};

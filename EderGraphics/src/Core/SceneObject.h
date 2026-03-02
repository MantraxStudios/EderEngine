#pragma once
#include "Transform.h"
#include "DLLHeader.h"

class EDERGRAPHICS_API VulkanMesh;
class EDERGRAPHICS_API Material;

struct SceneObject
{
    VulkanMesh* mesh     = nullptr;
    Material*   material = nullptr;
    Transform   transform;
    uint32_t    entityId = 0;   // links to ECS entity (0 = not linked)
};

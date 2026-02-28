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
};

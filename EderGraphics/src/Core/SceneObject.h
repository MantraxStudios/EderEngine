#pragma once
#include "Transform.h"

class VulkanMesh;
class Material;

struct SceneObject
{
    VulkanMesh* mesh     = nullptr;
    Material*   material = nullptr;
    Transform   transform;
};

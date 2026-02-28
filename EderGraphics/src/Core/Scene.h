#pragma once
#include "SceneObject.h"
#include "Renderer/Vulkan/ImportCore.h"
#include <vector>

class Camera;
class VulkanPipeline;

class Scene
{
public:
    SceneObject& Add(VulkanMesh& mesh, Material& material);
    void         Draw(vk::CommandBuffer cmd, VulkanPipeline& pipeline, const Camera& camera, float aspect);
    void         Clear();

    std::vector<SceneObject>& GetObjects() { return objects; }

private:
    std::vector<SceneObject> objects;
};

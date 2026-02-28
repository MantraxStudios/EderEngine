#pragma once
#include "SceneObject.h"
#include "Renderer/Vulkan/ImportCore.h"
#include "Renderer/Vulkan/VulkanInstanceBuffer.h"
#include <vector>
#include <map>

class Camera;
class VulkanPipeline;
class LightBuffer;

class Scene
{
public:
    SceneObject& Add(VulkanMesh& mesh, Material& material);
    void         Draw   (vk::CommandBuffer cmd, VulkanPipeline& pipeline, const Camera& camera, float aspect, LightBuffer& lights);
    void         Clear  ();
    void         Destroy();

    std::vector<SceneObject>& GetObjects() { return objects; }

private:
    std::vector<SceneObject> objects;
    VulkanInstanceBuffer     instanceBuffer;
};

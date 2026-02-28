#pragma once
#include "SceneObject.h"
#include "Renderer/Vulkan/ImportCore.h"
#include "Renderer/Vulkan/VulkanInstanceBuffer.h"
#include <vector>
#include <map>
#include "DLLHeader.h"

class EDERGRAPHICS_API Camera;
class EDERGRAPHICS_API VulkanPipeline;
class EDERGRAPHICS_API LightBuffer;
class EDERGRAPHICS_API VulkanShadowPipeline;

class EDERGRAPHICS_API Scene
{
public:
    SceneObject& Add(VulkanMesh& mesh, Material& material);
    void         Draw      (vk::CommandBuffer cmd, VulkanPipeline& pipeline, const Camera& camera, float aspect, LightBuffer& lights);
    void         DrawShadow(vk::CommandBuffer cmd, VulkanShadowPipeline& shadowPipeline, const glm::mat4& lightViewProj);
    void         Clear  ();
    void         Destroy();

    std::vector<SceneObject>& GetObjects() { return objects; }

private:
    std::vector<SceneObject> objects;
    VulkanInstanceBuffer     instanceBuffer;
};

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
class EDERGRAPHICS_API VulkanPointShadowPipeline;

class EDERGRAPHICS_API Scene
{
public:
    SceneObject& Add(VulkanMesh& mesh, Material& material);
    void         Remove(uint32_t entityId);   // removes first object linked to entityId
    void         Draw            (vk::CommandBuffer cmd, VulkanPipeline& pipeline, const Camera& camera, float aspect, LightBuffer& lights);
    void         DrawTransparent (vk::CommandBuffer cmd, VulkanPipeline& pipeline, const Camera& camera, float aspect, LightBuffer& lights);
    void         DrawShadow      (vk::CommandBuffer cmd, VulkanShadowPipeline& shadowPipeline, const glm::mat4& lightViewProj);
    void         DrawShadowPoint (vk::CommandBuffer cmd, VulkanPointShadowPipeline& pipeline,
                                  const glm::mat4& lightViewProj, const glm::vec3& lightPos, float farPlane);
    void         Clear  ();
    void         Destroy();

    std::vector<SceneObject>& GetObjects() { return objects; }

private:
    std::vector<SceneObject> objects;
    VulkanInstanceBuffer     instanceBuffer;
    VulkanInstanceBuffer     transparentInstanceBuffer;  // buffer separado — evita sobreescribir opacos
};

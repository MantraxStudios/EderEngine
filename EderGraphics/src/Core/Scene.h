#pragma once
#include "SceneObject.h"
#include "Renderer/Vulkan/ImportCore.h"
#include "Renderer/Vulkan/VulkanInstanceBuffer.h"
#include <vector>
#include <map>
#include <functional>
#include "DLLHeader.h"

class EDERGRAPHICS_API Camera;
class EDERGRAPHICS_API VulkanPipeline;
class EDERGRAPHICS_API LightBuffer;
class EDERGRAPHICS_API VulkanShadowPipeline;
class EDERGRAPHICS_API VulkanPointShadowPipeline;
class EDERGRAPHICS_API BoneSSBO;

class EDERGRAPHICS_API Scene
{
public:
    SceneObject& Add(VulkanMesh& mesh, Material& material);
    void         Remove(uint32_t entityId);   // removes first object linked to entityId
    void         Draw            (vk::CommandBuffer cmd, VulkanPipeline& pipeline, const Camera& camera, float aspect, LightBuffer& lights);
    void         DrawSkinned     (vk::CommandBuffer cmd, VulkanPipeline& pipeline, const Camera& camera, float aspect, LightBuffer& lights,
                                  const std::function<void(uint32_t entityId)>& bindBonesFn);
    void         DrawTransparent (vk::CommandBuffer cmd, VulkanPipeline& pipeline, const Camera& camera, float aspect, LightBuffer& lights);
    // cullSphere (optional): world-space sphere (xyz = center, w = radius).
    // Casters whose world bounding sphere does not intersect it are skipped.
    // fallbackBones: identity bone SSBO bound at set 1 of the alpha-test shadow
    // pipeline (its vertex shader requires the binding even for static meshes).
    void         DrawShadow            (vk::CommandBuffer cmd, VulkanShadowPipeline& shadowPipeline, const glm::mat4& lightViewProj,
                                        const glm::vec4* cullSphere = nullptr, BoneSSBO* fallbackBones = nullptr);
    void         DrawSkinnedShadow     (vk::CommandBuffer cmd, VulkanShadowPipeline& shadowPipeline, const glm::mat4& lightViewProj,
                                        const std::function<void(uint32_t entityId)>& bindBonesFn,
                                        const glm::vec4* cullSphere = nullptr);
    void         DrawShadowPoint       (vk::CommandBuffer cmd, VulkanPointShadowPipeline& pipeline,
                                        const glm::mat4& lightViewProj, const glm::vec3& lightPos, float farPlane);
    void         DrawSkinnedShadowPoint(vk::CommandBuffer cmd, VulkanPointShadowPipeline& pipeline,
                                        const glm::mat4& lightViewProj, const glm::vec3& lightPos, float farPlane,
                                        const std::function<void(uint32_t entityId)>& bindBonesFn);
    void         Clear  ();
    void         Destroy();

    std::vector<SceneObject>& GetObjects() { return objects; }

private:
    // Reusable scratch buffers for the shadow passes — kept as members so the
    // per-frame draw paths don't allocate (they retain capacity across frames).
    struct ShadowGroupEntry { VulkanMesh* mesh; glm::mat4 matrix; };
    struct ShadowSubMeshEntry { SceneObject* obj; uint32_t si; bool opaque; };
    std::vector<ShadowGroupEntry>   m_scratchGroups;
    std::vector<glm::mat4>          m_scratchMatrices;
    std::vector<glm::mat4>          m_scratchSmMats;
    std::vector<ShadowSubMeshEntry> m_scratchSmDraw;
    std::vector<glm::mat4>          m_scratchSkinned;

    std::vector<SceneObject> objects;
    VulkanInstanceBuffer     instanceBuffer;
    VulkanInstanceBuffer     shadowInstanceBuffer;
    VulkanInstanceBuffer     transparentInstanceBuffer;
    VulkanInstanceBuffer     skinnedInstanceBuffer;           // per-object skinned draws (main pass)
    VulkanInstanceBuffer     skinnedShadowInstanceBuffer;     // per-object skinned draws (shadow passes)
    VulkanInstanceBuffer     subMeshInstanceBuffer;           // per-submesh multi-material (main pass)
    VulkanInstanceBuffer     shadowSubMeshInstanceBuffer;     // per-submesh multi-material (dir shadow)
    VulkanInstanceBuffer     pointShadowSubMeshInstanceBuffer;// per-submesh multi-material (point/spot shadow)
    VulkanInstanceBuffer     alphaShadowInstanceBuffer;       // alpha-tested casters (dir/spot shadow)
};

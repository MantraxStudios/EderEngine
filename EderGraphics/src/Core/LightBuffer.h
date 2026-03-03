#pragma once
#include "Lights.h"
#include "Renderer/Vulkan/ImportCore.h"
#include "Renderer/Vulkan/VulkanBuffer.h"
#include "DLLHeader.h"

class EDERGRAPHICS_API VulkanPipeline;

class EDERGRAPHICS_API LightBuffer
{
public:
    void Build            (VulkanPipeline& pipeline);
    void Update           (const glm::vec3& cameraPos);
    void Bind             (vk::CommandBuffer cmd, vk::PipelineLayout layout);
    void Destroy();
    void SetCascadeData   (const glm::mat4 matrices[4], const glm::vec4& splits);
    void SetCameraForward (const glm::vec3& forward);
    void BindShadowMap    (vk::ImageView arrayView, vk::Sampler sampler);
    void BindSpotShadowMap (vk::ImageView arrayView,      vk::Sampler sampler);
    void BindPointShadowMap(vk::ImageView cubeArrayView,  vk::Sampler sampler);
    void SetSpotMatrix         (int slot, const glm::mat4& m);
    void SetPointFarPlane      (int slot, float farPlane);
    void UpdatePointPosition   (int idx,  const glm::vec3& pos);
    void UpdateSpotPosDir      (int idx,  const glm::vec3& pos, const glm::vec3& dir);
    void SetSkyAmbient         (const glm::vec3& sky, const glm::vec3& ground);

    void AddDirectional(const DirectionalLight& light);
    void AddPoint      (const PointLight&       light);
    void AddSpot       (const SpotLight&        light);
    void ClearLights   ();

    vk::DescriptorSet GetDescriptorSet() const { return *descriptorSet; }

private:
    LightUBO             ubo{};
    VulkanBuffer         buffer;
    vk::raii::DescriptorPool descriptorPool = nullptr;
    vk::raii::DescriptorSet  descriptorSet  = nullptr;
};

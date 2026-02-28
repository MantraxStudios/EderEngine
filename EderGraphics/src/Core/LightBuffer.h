#pragma once
#include "Lights.h"
#include "Renderer/Vulkan/ImportCore.h"
#include "Renderer/Vulkan/VulkanBuffer.h"

class VulkanPipeline;

class LightBuffer
{
public:
    void Build  (VulkanPipeline& pipeline);
    void Update (const glm::vec3& cameraPos);
    void Bind   (vk::CommandBuffer cmd, vk::PipelineLayout layout);
    void Destroy();

    void AddDirectional(const DirectionalLight& light);
    void AddPoint      (const PointLight&       light);
    void AddSpot       (const SpotLight&        light);
    void ClearLights   ();

private:
    LightUBO             ubo{};
    VulkanBuffer         buffer;
    vk::raii::DescriptorPool descriptorPool = nullptr;
    vk::raii::DescriptorSet  descriptorSet  = nullptr;
};

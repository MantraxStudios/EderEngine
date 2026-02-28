#pragma once
#include "Renderer/Vulkan/ImportCore.h"
#include "Renderer/Vulkan/VulkanBuffer.h"
#include "Renderer/Vulkan/VulkanTexture.h"
#include "MaterialLayout.h"
#include <unordered_map>
#include <string>
#include "DLLHeader.h"

class EDERGRAPHICS_API VulkanPipeline;

class EDERGRAPHICS_API Material
{
public:
    Material() = default;

    void Build  (const MaterialLayout& layout, VulkanPipeline& pipeline);
    void Destroy();

    void SetFloat  (const std::string& name, float value);
    void SetInt    (const std::string& name, int32_t value);
    void SetVec2   (const std::string& name, const glm::vec2& value);
    void SetVec3   (const std::string& name, const glm::vec3& value);
    void SetVec4   (const std::string& name, const glm::vec4& value);
    void SetMat4   (const std::string& name, const glm::mat4& value);
    void BindTexture(uint32_t slot, VulkanTexture& texture);

    void Bind(vk::CommandBuffer cmd, vk::PipelineLayout pipelineLayout);

private:
    void SetProperty(const std::string& name, MaterialPropertyValue value);
    void FlushToGPU();

    MaterialLayout                                          layout;
    std::unordered_map<std::string, MaterialPropertyValue>  properties;
    std::vector<uint8_t>                                    cpuBuffer;

    VulkanBuffer             ubo;
    vk::raii::DescriptorPool descriptorPool = nullptr;
    vk::raii::DescriptorSet  descriptorSet  = nullptr;
    bool                     dirty          = true;
};

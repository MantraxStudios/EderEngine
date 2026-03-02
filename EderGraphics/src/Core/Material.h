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

    // Alpha mode ─────────────────────────────────────────────────────────────
    // Opaque   : normal rendering, texture alpha ignored
    // AlphaTest: texture alpha used; pixels below alphaCutoff are discarded
    // AlphaBlend: object sorted and drawn with the transparent pipeline
    enum class AlphaMode : uint8_t { Opaque, AlphaTest, AlphaBlend };

    AlphaMode alphaMode   = AlphaMode::Opaque;
    float     alphaCutoff = 0.5f;             // threshold for AlphaTest mode
    // Opacidad global: 1.0 = opaco, <1.0 = transparente
    float opacity         = 1.0f;
    bool  IsTransparent() const { return alphaMode == AlphaMode::AlphaBlend || opacity < 0.999f; }
    bool  IsAlphaTested() const { return alphaMode == AlphaMode::AlphaTest; }

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

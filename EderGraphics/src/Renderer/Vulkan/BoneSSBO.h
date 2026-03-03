#pragma once
#include "ImportCore.h"
#include "VulkanBuffer.h"
#include <glm/glm.hpp>
#include <vector>

class EDERGRAPHICS_API VulkanPipeline;

constexpr uint32_t MAX_BONES = 256;

// SSBO that holds per-frame bone matrices for skeletal animation.
// Bound as set 2, binding 0 in the main pipeline.
class EDERGRAPHICS_API BoneSSBO
{
public:
    void Create (VulkanPipeline& pipeline);
    void Upload (const std::vector<glm::mat4>& boneMatrices);
    void Bind   (vk::CommandBuffer cmd, vk::PipelineLayout layout); // binds to set 2
    void Destroy();

    vk::DescriptorSet GetDescriptorSet() const { return *descriptorSet; }

private:
    VulkanBuffer                  buffer;
    vk::raii::DescriptorPool      descriptorPool = nullptr;
    vk::raii::DescriptorSet       descriptorSet  = nullptr;
};

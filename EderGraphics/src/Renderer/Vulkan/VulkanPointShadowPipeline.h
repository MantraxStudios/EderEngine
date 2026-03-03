#pragma once
#include "ImportCore.h"

class EDERGRAPHICS_API VulkanPointShadowPipeline
{
public:
    void Create (vk::Format depthFormat);
    void Bind   (vk::CommandBuffer cmd);
    void Destroy();

    vk::PipelineLayout              GetLayout    () const { return *pipelineLayout; }
    vk::raii::DescriptorSetLayout&  GetBoneDescriptorSetLayout() { return boneDescriptorSetLayout; }

private:
    std::vector<uint32_t> LoadSpv(const std::string& path);

    vk::raii::DescriptorSetLayout boneDescriptorSetLayout = nullptr;
    vk::raii::PipelineLayout      pipelineLayout          = nullptr;
    vk::raii::Pipeline            pipeline                = nullptr;
};

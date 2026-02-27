#pragma once
#include "ImportCore.h"

class VulkanFramebuffer;

class VulkanDebugOverlay
{
public:
    void Create (vk::Format swapchainFormat, vk::Format depthFormat);
    void Destroy();
    void Draw   (vk::CommandBuffer cmd, VulkanFramebuffer& framebuffer);

private:
    std::vector<uint32_t>  LoadSpv(const std::string& path);

    vk::raii::DescriptorSetLayout descriptorSetLayout = nullptr;
    vk::raii::PipelineLayout      pipelineLayout      = nullptr;
    vk::raii::Pipeline            pipeline            = nullptr;
    vk::raii::DescriptorPool      descriptorPool      = nullptr;
    vk::raii::DescriptorSet       descriptorSet       = nullptr;
};

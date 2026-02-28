#pragma once
#include "ImportCore.h"

class VulkanFramebuffer;
class VulkanShadowMap;

class VulkanDebugOverlay
{
public:
    void Create (vk::Format swapchainFormat, vk::Format depthFormat);
    void Destroy();
    void Draw   (vk::CommandBuffer cmd, VulkanFramebuffer& framebuffer, VulkanShadowMap& shadowMap);

private:
    std::vector<uint32_t> LoadSpv(const std::string& path);
    vk::raii::Pipeline    BuildPipeline(vk::Format swapchainFormat, vk::Format depthFormat,
                                        const std::string& vertSpv, const std::string& fragSpv);

    // Shared descriptor set layout and pipeline layout
    vk::raii::DescriptorSetLayout descriptorSetLayout = nullptr;
    vk::raii::PipelineLayout      pipelineLayout      = nullptr;
    vk::raii::DescriptorPool      descriptorPool      = nullptr;

    // Framebuffer debug quad (bottom-right): debug.vert + debug.frag
    vk::raii::Pipeline      fbPipeline      = nullptr;
    vk::raii::DescriptorSet fbDescriptorSet = nullptr;

    // Shadow map debug quad (bottom-left): shadow_debug.vert + shadow_debug.frag
    vk::raii::Pipeline      shadowPipeline      = nullptr;
    vk::raii::DescriptorSet shadowDescriptorSet = nullptr;
};

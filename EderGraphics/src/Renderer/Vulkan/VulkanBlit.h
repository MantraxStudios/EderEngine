#pragma once
#include "ImportCore.h"

// ─────────────────────────────────────────────────────────────────────────────
//  VulkanBlit
//  Blits any VulkanFramebuffer colour image as a fullscreen quad into whatever
//  rendering context is currently active (e.g. the swapchain main pass).
//  Uses fog.vert.spv (fullscreen triangle) + blit.frag.spv (simple 1-sampler).
// ─────────────────────────────────────────────────────────────────────────────
class EDERGRAPHICS_API VulkanBlit
{
public:
    // swapchainFormat: format of the active render target (swapchain colour format)
    // depthFormat    : depth format of the active render target (for pipeline compat)
    void Create (vk::Format swapchainFormat, vk::Format depthFormat);
    void Destroy();

    // Draw the fullscreen blit. Must be called inside an active rendering pass.
    void Draw(vk::CommandBuffer cmd, vk::ImageView srcView, vk::Sampler srcSampler);

private:
    static std::vector<uint32_t> LoadSpv(const std::string& path);
    void UpdateDescriptor(vk::ImageView view, vk::Sampler sampler);

    vk::raii::DescriptorSetLayout m_setLayout      = nullptr;
    vk::raii::DescriptorPool      m_descriptorPool = nullptr;
    vk::raii::DescriptorSet       m_descriptorSet  = nullptr;
    vk::raii::PipelineLayout      m_pipelineLayout = nullptr;
    vk::raii::Pipeline            m_pipeline       = nullptr;

    vk::ImageView m_lastView    = nullptr;
    vk::Sampler   m_lastSampler = nullptr;
};

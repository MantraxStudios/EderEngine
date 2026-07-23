#pragma once
#include "Core/DLLHeader.h"
#include <vulkan/vulkan_raii.hpp>
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include "Renderer/Vulkan/VulkanBuffer.h"

// Deferred lighting composite. Renders a full-screen triangle INTO a caller-owned
// framebuffer (the scene framebuffer), writing lit colour and re-emitting the
// G-buffer depth via gl_FragDepth so later depth-based passes keep working.
//   set 0 = G-buffer + SSAO (owned here)
//   set 1 = shared LightBuffer descriptor set (forward-pipeline layout)
class EDERGRAPHICS_API VulkanDeferredLighting
{
public:
    void Create (vk::Format colorFormat, vk::Format depthFormat,
                 vk::DescriptorSetLayout lightDSL);
    void Destroy();
    void ResetBindings();   // call after G-buffer resize so descriptors refresh

    // Record the composite. The caller must have an active dynamic-rendering pass
    // on the target (color + depth) framebuffer.
    void Record(vk::CommandBuffer cmd,
                vk::ImageView albedoView, vk::ImageView normalView,
                vk::ImageView materialView, vk::ImageView depthView,
                vk::Sampler   gSampler,
                vk::ImageView ssaoView, vk::Sampler ssaoSampler,
                const glm::mat4& invViewProj,
                vk::DescriptorSet lightDS);

private:
    struct alignas(16) DeferredUBO { glm::mat4 invViewProj; };

    void BuildPipeline(vk::Format colorFormat, vk::Format depthFormat,
                       vk::DescriptorSetLayout lightDSL);
    void UpdateDescriptor(vk::ImageView a, vk::ImageView n, vk::ImageView m,
                          vk::ImageView d, vk::Sampler gs,
                          vk::ImageView ssao, vk::Sampler ss);
    static std::vector<uint32_t> LoadSpv(const std::string& path);

    VulkanBuffer                  m_ubo;
    vk::raii::DescriptorSetLayout m_setLayout      = nullptr;
    vk::raii::DescriptorPool      m_descriptorPool = nullptr;
    vk::raii::DescriptorSet       m_descriptorSet  = nullptr;
    vk::raii::PipelineLayout      m_pipelineLayout = nullptr;
    vk::raii::Pipeline            m_pipeline       = nullptr;

    vk::ImageView m_lastA = nullptr, m_lastN = nullptr, m_lastM = nullptr,
                  m_lastD = nullptr, m_lastSSAO = nullptr;
    vk::Sampler   m_lastGS = nullptr, m_lastSS = nullptr;
};

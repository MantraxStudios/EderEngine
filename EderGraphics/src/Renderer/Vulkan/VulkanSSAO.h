#pragma once
#include "Core/DLLHeader.h"
#include <vulkan/vulkan_raii.hpp>
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include "Renderer/Vulkan/VulkanFramebuffer.h"
#include "Renderer/Vulkan/VulkanBuffer.h"

// Screen-space ambient occlusion pass. Reads the G-buffer normal + depth and
// writes a single-channel occlusion factor (1 = lit) into its output framebuffer.
class EDERGRAPHICS_API VulkanSSAO
{
public:
    void Create (uint32_t w, uint32_t h);
    void Destroy();
    void Resize (uint32_t w, uint32_t h);

    void Draw(vk::CommandBuffer cmd,
              vk::ImageView normalView, vk::ImageView depthView, vk::Sampler gSampler,
              const glm::mat4& proj, const glm::mat4& view,
              float radius, float bias, float intensity, float power);

    VulkanFramebuffer& GetOutput() { return m_outputFb; }

private:
    struct alignas(16) SSAOUBO {
        glm::mat4 proj;
        glm::mat4 invProj;
        glm::mat4 view;
        glm::vec4 params;   // radius, bias, intensity, power
    };

    void BuildPipeline();
    void UpdateDescriptor(vk::ImageView normalView, vk::ImageView depthView, vk::Sampler s);
    static std::vector<uint32_t> LoadSpv(const std::string& path);

    VulkanFramebuffer             m_outputFb;
    VulkanBuffer                  m_ubo;
    vk::raii::DescriptorSetLayout m_setLayout      = nullptr;
    vk::raii::DescriptorPool      m_descriptorPool = nullptr;
    vk::raii::DescriptorSet       m_descriptorSet  = nullptr;
    vk::raii::PipelineLayout      m_pipelineLayout = nullptr;
    vk::raii::Pipeline            m_pipeline       = nullptr;

    vk::ImageView m_lastNormal = nullptr;
    vk::ImageView m_lastDepth  = nullptr;
    vk::Sampler   m_lastSmp    = nullptr;
};

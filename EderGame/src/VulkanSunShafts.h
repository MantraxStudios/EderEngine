#pragma once
#include <vulkan/vulkan_raii.hpp>
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include "Renderer/Vulkan/VulkanFramebuffer.h"

class VulkanSunShafts
{
public:
    void Create (vk::Format colorFormat, uint32_t w, uint32_t h);
    void Destroy();

    void Resize(uint32_t w, uint32_t h);

    void Draw(vk::CommandBuffer cmd,
              vk::ImageView    sceneView,
              vk::Sampler      sceneSampler,
              vk::ImageView    occlusionView,  // from VulkanOcclusionPass
              vk::ImageView    depthView,
              glm::vec2        sunUV,
              float density,    float bloomScale,
              float decay,      float weight,
              float exposure,
              const glm::vec3& tint,
              float            sunHeight = 1.0f);

    VulkanFramebuffer& GetOutput() { return outputFb; }

private:
    struct PushData
    {
        glm::vec2 sunUV;             // offset  0  (8)
        float     decay;             // offset  8  (4)
        float     weight;            // offset 12  (4)
        float     exposure;          // offset 16  (4)
        float     density;           // offset 20  (4)
        float     bloomScale = 1.0f; // offset 24  (4)  — glare-only multiplier
        float     sunHeight  = 1.0f; // offset 28  (4)
        glm::vec3 tint;              // offset 32 (12)
    };  // total: 44 bytes

    void BuildPipeline(vk::Format colorFormat);
    void UpdateDescriptor(vk::ImageView sceneView, vk::Sampler sampler,
                          vk::ImageView occlusionView, vk::ImageView depthView);
    static std::vector<uint32_t> LoadSpv(const std::string& path);

    VulkanFramebuffer             outputFb;
    vk::Format                    fmt       = vk::Format::eUndefined;

    vk::raii::DescriptorSetLayout setLayout      = nullptr;
    vk::raii::DescriptorPool      descriptorPool = nullptr;
    vk::raii::DescriptorSet       descriptorSet  = nullptr;
    vk::raii::PipelineLayout      pipelineLayout = nullptr;
    vk::raii::Pipeline            pipeline       = nullptr;

    vk::ImageView lastView        = nullptr;
    vk::Sampler   lastSampler     = nullptr;
    vk::ImageView lastOccView     = nullptr;
    vk::ImageView lastDepthView   = nullptr;
};

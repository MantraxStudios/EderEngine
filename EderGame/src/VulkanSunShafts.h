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
              vk::ImageView    depthView,         
              glm::vec2        sunUV,              
              float intensity, float decay,
              float weight,    float exposure,
              const glm::vec3& tint,
              float            sunHeight = 1.0f); 

    VulkanFramebuffer& GetOutput() { return outputFb; }

private:
    struct PushData
    {
        glm::vec2 sunUV;
        float     decay;
        float     weight;
        float     exposure;
        float     intensity;
        glm::vec3 tint;
        float     sunHeight = 1.0f;
    };

    void BuildPipeline(vk::Format colorFormat);
    void UpdateDescriptor(vk::ImageView sceneView, vk::Sampler sampler, vk::ImageView depthView);
    static std::vector<uint32_t> LoadSpv(const std::string& path);

    VulkanFramebuffer             outputFb;
    vk::Format                    fmt       = vk::Format::eUndefined;

    vk::raii::DescriptorSetLayout setLayout      = nullptr;
    vk::raii::DescriptorPool      descriptorPool = nullptr;
    vk::raii::DescriptorSet       descriptorSet  = nullptr;
    vk::raii::PipelineLayout      pipelineLayout = nullptr;
    vk::raii::Pipeline            pipeline       = nullptr;

    vk::ImageView lastView      = nullptr;
    vk::Sampler   lastSampler   = nullptr;
    vk::ImageView lastDepthView = nullptr;
};

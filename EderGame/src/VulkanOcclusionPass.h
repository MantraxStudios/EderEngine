#pragma once
#include <vulkan/vulkan_raii.hpp>
#include <glm/glm.hpp>
#include <string>
#include <vector>

class VulkanOcclusionPass
{
public:
    void Create (uint32_t w, uint32_t h);
    void Destroy();
    void Resize  (uint32_t w, uint32_t h);

    void Draw(vk::CommandBuffer cmd,
              vk::ImageView     depthView,
              vk::Sampler       depthSampler,
              glm::vec2         sunUV,
              float             sunRadius);

    vk::ImageView GetView()    const { return *occView;    }
    vk::Sampler   GetSampler() const { return *occSampler; }
    vk::Extent2D  GetExtent()  const { return extent;      }

private:
    struct PushData
    {
        glm::vec2 sunUV;      // offset 0
        float     sunRadius;  // offset 8
    };

    void     CreateImages (uint32_t w, uint32_t h);
    void     DestroyImages();
    void     BuildPipeline();
    void     UpdateDescriptor(vk::ImageView depthView, vk::Sampler depthSampler);
    uint32_t FindMemoryType(uint32_t filter, vk::MemoryPropertyFlags props);
    static std::vector<uint32_t> LoadSpv(const std::string& path);

    vk::raii::Image        occImage   = nullptr;
    vk::raii::DeviceMemory occMemory  = nullptr;
    vk::raii::ImageView    occView    = nullptr;
    vk::raii::Sampler      occSampler = nullptr;
    vk::ImageLayout        occLayout  = vk::ImageLayout::eUndefined;
    vk::Extent2D           extent     = {};

    vk::raii::DescriptorSetLayout setLayout      = nullptr;
    vk::raii::DescriptorPool      descriptorPool = nullptr;
    vk::raii::DescriptorSet       descriptorSet  = nullptr;
    vk::raii::PipelineLayout      pipelineLayout = nullptr;
    vk::raii::Pipeline            pipeline       = nullptr;

    vk::ImageView lastDepthView = nullptr;
    vk::Sampler   lastDepthSmp  = nullptr;
};

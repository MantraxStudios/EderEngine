#pragma once
#include "ImportCore.h"
#include <glm/glm.hpp>

class EDERGRAPHICS_API VulkanSpotShadowMap
{
public:
    static constexpr uint32_t MAX_SLOTS = 4;

    void Create (uint32_t size = 1024);
    void Destroy();

    void BeginRendering        (vk::CommandBuffer cmd, uint32_t slot);
    void EndRendering          (vk::CommandBuffer cmd);
    void TransitionToShaderRead(vk::CommandBuffer cmd);

    vk::ImageView GetArrayView() const { return *arrayView; }
    vk::Sampler   GetSampler()   const { return *sampler;   }
    vk::Format    GetFormat()    const { return format;     }

    // Build a perspective viewProj for a spot light.
    // outerCos: cosine of the outer cone half-angle.
    static glm::mat4 ComputeMatrix(const glm::vec3& pos, const glm::vec3& dir,
                                   float outerCos, float nearZ = 0.1f, float farZ = 100.0f);

private:
    vk::Format FindDepthFormat();
    uint32_t   FindMemoryType(uint32_t filter, vk::MemoryPropertyFlags props);

    vk::raii::Image        depthImage  = nullptr;
    vk::raii::DeviceMemory depthMemory = nullptr;
    vk::raii::ImageView    layerViews[MAX_SLOTS] = { nullptr, nullptr, nullptr, nullptr };
    vk::raii::ImageView    arrayView   = nullptr;
    vk::raii::Sampler      sampler     = nullptr;

    vk::Format      format                  = vk::Format::eUndefined;
    vk::ImageLayout layerLayouts[MAX_SLOTS] = {};
    uint32_t        mapSize                 = 1024;
};

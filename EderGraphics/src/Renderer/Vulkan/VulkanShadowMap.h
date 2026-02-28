#pragma once
#include "ImportCore.h"
#include <glm/glm.hpp>

class VulkanShadowMap
{
public:
    void Create (uint32_t size = 2048);
    void Destroy();

    void BeginRendering        (vk::CommandBuffer cmd);
    void EndRendering          (vk::CommandBuffer cmd);
    void TransitionToShaderRead(vk::CommandBuffer cmd);

    vk::ImageView GetDepthView() const { return *depthView; }
    vk::Sampler   GetSampler()   const { return *sampler;   }
    vk::Format    GetFormat()    const { return format;     }
    uint32_t      GetSize()      const { return mapSize;    }

    glm::mat4 ComputeLightSpaceMatrix(const glm::vec3& lightDir, float sceneDist = 150.0f) const;

private:
    uint32_t   FindMemoryType(uint32_t filter, vk::MemoryPropertyFlags props);
    vk::Format FindDepthFormat();

    vk::raii::Image        depthImage  = nullptr;
    vk::raii::DeviceMemory depthMemory = nullptr;
    vk::raii::ImageView    depthView   = nullptr;
    vk::raii::Sampler      sampler     = nullptr;

    vk::Format      format  = vk::Format::eUndefined;
    vk::ImageLayout layout  = vk::ImageLayout::eUndefined;
    uint32_t        mapSize = 2048;
};

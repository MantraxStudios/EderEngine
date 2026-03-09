#pragma once
#include "ImportCore.h"
#include <glm/glm.hpp>

class EDERGRAPHICS_API VulkanShadowMap
{
public:
    static constexpr uint32_t NUM_CASCADES = 4;

    void Create (uint32_t size = 1024);
    void Destroy();

    void BeginRendering        (vk::CommandBuffer cmd, uint32_t cascadeIndex);
    void EndRendering          (vk::CommandBuffer cmd);
    void TransitionToShaderRead(vk::CommandBuffer cmd);

    vk::ImageView GetArrayView() const { return *arrayView; }
    vk::Sampler   GetSampler()   const { return *sampler;   }
    vk::Format    GetFormat()    const { return format;     }
    uint32_t      GetSize()      const { return mapSize;    }

    void ComputeCascades(
        const glm::mat4& camView,
        const glm::vec3& lightDir,
        float nearPlane, float farPlane,
        glm::mat4 outMatrices[NUM_CASCADES],
        glm::vec4& outSplits) const;

private:
    uint32_t   FindMemoryType(uint32_t filter, vk::MemoryPropertyFlags props);
    vk::Format FindDepthFormat();

    vk::raii::Image        depthImage  = nullptr;
    vk::raii::DeviceMemory depthMemory = nullptr;
    vk::raii::ImageView    layerViews[NUM_CASCADES] = { nullptr, nullptr, nullptr, nullptr };
    vk::raii::ImageView    arrayView   = nullptr;
    vk::raii::Sampler      sampler     = nullptr;

    vk::Format      format                    = vk::Format::eUndefined;
    vk::ImageLayout layerLayouts[NUM_CASCADES] = {};
    uint32_t        mapSize                    = 1024;
};

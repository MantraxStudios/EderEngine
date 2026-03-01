#pragma once
#include "ImportCore.h"
#include <glm/glm.hpp>
#include <array>

class EDERGRAPHICS_API VulkanPointShadowMap
{
public:
    static constexpr uint32_t MAX_SLOTS   = 4;
    static constexpr uint32_t FACE_COUNT  = 6;

    void Create (uint32_t size = 512);
    void Destroy();

    void BeginRendering        (vk::CommandBuffer cmd, uint32_t slot, uint32_t face);
    void EndRendering          (vk::CommandBuffer cmd);
    void TransitionToShaderRead(vk::CommandBuffer cmd, uint32_t slot);

    vk::ImageView GetCubeArrayView() const { return *cubeArrayView; }
    vk::Sampler   GetSampler()       const { return *sampler;       }
    vk::Format    GetFormat()        const { return format;         }

    // Returns all 6 face viewProj matrices for a point light at 'pos' with given farPlane.
    static std::array<glm::mat4, 6> ComputeFaceMatrices(const glm::vec3& pos,
                                                         float nearZ, float farZ);

private:
    vk::Format FindDepthFormat();
    uint32_t   FindMemoryType(uint32_t filter, vk::MemoryPropertyFlags props);

    // One depth image: MAX_SLOTS * 6 array layers, VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT
    vk::raii::Image        depthImage    = nullptr;
    vk::raii::DeviceMemory depthMemory   = nullptr;

    // Per-face 2D views (MAX_SLOTS * 6) for rendering
    std::array<vk::raii::ImageView, MAX_SLOTS * FACE_COUNT> faceViews =
        { nullptr,nullptr,nullptr,nullptr, nullptr,nullptr,nullptr,nullptr,
          nullptr,nullptr,nullptr,nullptr, nullptr,nullptr,nullptr,nullptr,
          nullptr,nullptr,nullptr,nullptr, nullptr,nullptr,nullptr,nullptr };

    // Cube-array view for shader sampling
    vk::raii::ImageView    cubeArrayView = nullptr;
    vk::raii::Sampler      sampler       = nullptr;

    vk::Format format  = vk::Format::eUndefined;
    uint32_t   mapSize = 512;

    // Track per-face layout (MAX_SLOTS * 6)
    std::array<vk::ImageLayout, MAX_SLOTS * FACE_COUNT> faceLayouts = {};
};

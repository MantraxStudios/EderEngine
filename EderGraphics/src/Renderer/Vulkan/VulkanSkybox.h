#pragma once
#include "ImportCore.h"
#include "VulkanBuffer.h"
#include <glm/glm.hpp>

class EDERGRAPHICS_API VulkanSkybox
{
public:
    void Create (vk::Format swapchainFormat, vk::Format depthFormat);
    void Destroy();

    // view / proj matrices are used to build inverseView, inverseProj, cameraPos
    // that get uploaded to the camera UBO every frame.
    void Draw(vk::CommandBuffer  cmd,
              const glm::mat4&   view,
              const glm::mat4&   proj,
              glm::vec3          sunDir       = glm::vec3(-0.3f, 0.8f, 0.2f),
              float              sunIntensity = 22.0f);

private:
    std::vector<uint32_t> LoadSpv(const std::string& path);

    // Camera UBO — set=0, binding=0
    // Layout: mat4 inverseView (64) + mat4 inverseProj (64) + vec3 cameraPos padded (16) = 144 bytes
    struct CameraUBO
    {
        glm::mat4 inverseView;
        glm::mat4 inverseProj;
        glm::vec4 cameraPos;   // vec3 padded to vec4 to match GLSL std140
    };

    VulkanBuffer                  cameraBuffer;
    vk::raii::DescriptorSetLayout setLayout    = nullptr;
    vk::raii::DescriptorPool      descPool     = nullptr;
    vk::raii::DescriptorSet       descSet      = nullptr;

    vk::raii::PipelineLayout pipelineLayout = nullptr;
    vk::raii::Pipeline       pipeline       = nullptr;
};

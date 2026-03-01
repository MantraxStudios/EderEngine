#pragma once
#include "ImportCore.h"
#include <glm/glm.hpp>

class EDERGRAPHICS_API VulkanSkybox
{
public:
    void Create (vk::Format swapchainFormat, vk::Format depthFormat);
    void Destroy();
    void Draw   (vk::CommandBuffer cmd,
                 const glm::mat4& invViewProj,
                 glm::vec3        sunDir       = glm::vec3(-0.3f, 0.8f, 0.2f),
                 float            sunIntensity = 22.0f);

private:
    std::vector<uint32_t> LoadSpv(const std::string& path);

    vk::raii::PipelineLayout pipelineLayout = nullptr;
    vk::raii::Pipeline       pipeline       = nullptr;
};

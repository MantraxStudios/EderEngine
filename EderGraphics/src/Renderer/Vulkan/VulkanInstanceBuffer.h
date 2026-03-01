#pragma once
#include "ImportCore.h"
#include "VulkanBuffer.h"
#include <glm/glm.hpp>
#include <vector>
#include <array>

class EDERGRAPHICS_API VulkanInstanceBuffer
{
public:
    void Upload(const std::vector<glm::mat4>& matrices);
    void Bind   (vk::CommandBuffer cmd);
    void Destroy();

private:
    static constexpr uint32_t FRAMES = 2;  // must match MAX_FRAMES_IN_FLIGHT
    std::array<VulkanBuffer, FRAMES> buffers;
    std::array<uint32_t,     FRAMES> capacities = {};
};

#pragma once
#include "ImportCore.h"
#include "VulkanBuffer.h"
#include <glm/glm.hpp>
#include <vector>

class VulkanInstanceBuffer
{
public:
    void Upload(const std::vector<glm::mat4>& matrices);
    void Bind   (vk::CommandBuffer cmd);
    void Destroy();

private:
    VulkanBuffer buffer;
    uint32_t     capacity = 0;
};

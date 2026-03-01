#include "VulkanInstanceBuffer.h"
#include "Renderer/VulkanRenderer.h"

static uint32_t Frame() { return VulkanRenderer::Get().GetCurrentFrame(); }

void VulkanInstanceBuffer::Upload(const std::vector<glm::mat4>& matrices)
{
    uint32_t f      = Frame();
    uint32_t needed = static_cast<uint32_t>(matrices.size());
    if (needed == 0) return;

    if (needed > capacities[f])
    {
        buffers[f].Destroy();
        buffers[f].Create(
            sizeof(glm::mat4) * needed,
            vk::BufferUsageFlagBits::eVertexBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
        capacities[f] = needed;
    }

    buffers[f].Upload(matrices.data(), sizeof(glm::mat4) * needed);
}

void VulkanInstanceBuffer::Bind(vk::CommandBuffer cmd)
{
    uint32_t      f      = Frame();
    vk::Buffer    buf    = buffers[f].GetBuffer();
    vk::DeviceSize offset = 0;
    cmd.bindVertexBuffers(1, buf, offset);
}

void VulkanInstanceBuffer::Destroy()
{
    for (auto& b : buffers)  b.Destroy();
    capacities = {};
}

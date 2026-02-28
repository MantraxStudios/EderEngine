#include "VulkanInstanceBuffer.h"

void VulkanInstanceBuffer::Upload(const std::vector<glm::mat4>& matrices)
{
    uint32_t needed = static_cast<uint32_t>(matrices.size());
    if (needed == 0) return;

    if (needed > capacity)
    {
        buffer.Destroy();
        buffer.Create(
            sizeof(glm::mat4) * needed,
            vk::BufferUsageFlagBits::eVertexBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
        capacity = needed;
    }

    buffer.Upload(matrices.data(), sizeof(glm::mat4) * needed);
}

void VulkanInstanceBuffer::Bind(vk::CommandBuffer cmd)
{
    vk::Buffer     buf    = buffer.GetBuffer();
    vk::DeviceSize offset = 0;
    cmd.bindVertexBuffers(1, buf, offset);
}

void VulkanInstanceBuffer::Destroy()
{
    buffer.Destroy();
    capacity = 0;
}

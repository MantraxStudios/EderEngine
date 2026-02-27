#include "VulkanBuffer.h"
#include "VulkanInstance.h"

void VulkanBuffer::Create(vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties)
{
    auto& device = VulkanInstance::Get().GetDevice();
    bufferSize   = size;

    vk::BufferCreateInfo bufferInfo{};
    bufferInfo.size        = size;
    bufferInfo.usage       = usage;
    bufferInfo.sharingMode = vk::SharingMode::eExclusive;

    buffer = vk::raii::Buffer(device, bufferInfo);

    auto memReqs = buffer.getMemoryRequirements();

    vk::MemoryAllocateInfo allocInfo{};
    allocInfo.allocationSize  = memReqs.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memReqs.memoryTypeBits, properties);

    memory = vk::raii::DeviceMemory(device, allocInfo);
    buffer.bindMemory(*memory, 0);
}

void VulkanBuffer::Upload(const void* data, vk::DeviceSize size)
{
    auto mapped = memory.mapMemory(0, size);
    memcpy(mapped, data, static_cast<size_t>(size));
    memory.unmapMemory();
}

void VulkanBuffer::Destroy()
{
    memory = nullptr;
    buffer = nullptr;
}

uint32_t VulkanBuffer::FindMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties)
{
    auto memProps = VulkanInstance::Get().GetPhysicalDevice().getMemoryProperties();
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++)
        if ((typeFilter & (1 << i)) && (memProps.memoryTypes[i].propertyFlags & properties) == properties)
            return i;
    throw std::runtime_error("Failed to find suitable memory type!");
}
#pragma once
#include "ImportCore.h"

class EDERGRAPHICS_API VulkanBuffer
{
public:
    VulkanBuffer() = default;

    void Create(vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties);
    void Upload(const void* data, vk::DeviceSize size);
    void Destroy();

    vk::Buffer       GetBuffer()    { return *buffer; }
    vk::DeviceMemory GetMemory()    { return *memory; }
    vk::DeviceSize   GetSize()      { return bufferSize; }

private:
    uint32_t FindMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties);

    vk::raii::Buffer       buffer     = nullptr;
    vk::raii::DeviceMemory memory     = nullptr;
    vk::DeviceSize         bufferSize = 0;
};
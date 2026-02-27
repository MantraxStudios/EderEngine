#pragma once
#include "ImportCore.h"

class VulkanDepthBuffer
{
public:
    void Create  (uint32_t width, uint32_t height);
    void Recreate(uint32_t width, uint32_t height);
    void Destroy ();

    vk::ImageView GetImageView() const { return *imageView; }
    vk::Format    GetFormat()    const { return format;     }

private:
    vk::Format FindDepthFormat();
    uint32_t   FindMemoryType(uint32_t filter, vk::MemoryPropertyFlags props);

    vk::raii::Image        image     = nullptr;
    vk::raii::DeviceMemory memory    = nullptr;
    vk::raii::ImageView    imageView = nullptr;
    vk::Format             format    = vk::Format::eUndefined;
};

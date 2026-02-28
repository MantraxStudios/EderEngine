#pragma once
#include "ImportCore.h"

class EDERGRAPHICS_API VulkanTexture
{
public:
    void Load   (const std::string& path);
    void CreateDefault();
    void Destroy();

    vk::ImageView GetImageView() const { return *imageView; }
    vk::Sampler   GetSampler()   const { return *sampler;   }
    bool          IsValid()      const { return created;    }

private:
    void UploadPixels(const uint8_t* pixels, uint32_t width, uint32_t height);
    void CreateImageResources(uint32_t width, uint32_t height);
    void CreateSampler();
    void TransitionLayout(vk::CommandBuffer cmd, vk::ImageLayout from, vk::ImageLayout to);
    void CopyFromBuffer (vk::CommandBuffer cmd, vk::Buffer src, uint32_t width, uint32_t height);
    vk::CommandBuffer   BeginOneTime();
    void                EndOneTime(vk::CommandBuffer cmd);
    uint32_t FindMemoryType(uint32_t filter, vk::MemoryPropertyFlags props);

    vk::raii::Image        image       = nullptr;
    vk::raii::DeviceMemory memory      = nullptr;
    vk::raii::ImageView    imageView   = nullptr;
    vk::raii::Sampler      sampler     = nullptr;
    vk::raii::CommandPool  oneTimePool = nullptr;
    bool                   created     = false;
};
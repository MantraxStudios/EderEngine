#pragma once
#include "ImportCore.h"

class EDERGRAPHICS_API VulkanFramebuffer
{
public:
    void Create   (uint32_t width, uint32_t height, vk::Format colorFormat, vk::Format depthFormat);
    void Recreate (uint32_t width, uint32_t height);
    void Destroy  ();

    void BeginRendering        (vk::CommandBuffer cmd);
    void EndRendering          (vk::CommandBuffer cmd);
    void TransitionToShaderRead(vk::CommandBuffer cmd);

    vk::ImageView GetColorView()   const { return *colorView;   }
    vk::ImageView GetDepthView()   const { return *depthView;   }
    vk::Sampler   GetSampler()     const { return *sampler;     }
    vk::Format    GetColorFormat() const { return colorFormat;  }
    vk::Format    GetDepthFormat() const { return depthFormat;  }
    vk::Extent2D  GetExtent()      const { return extent;       }

private:
    uint32_t FindMemoryType(uint32_t filter, vk::MemoryPropertyFlags props);

    vk::raii::Image        colorImage  = nullptr;
    vk::raii::DeviceMemory colorMemory = nullptr;
    vk::raii::ImageView    colorView   = nullptr;
    vk::raii::Sampler      sampler     = nullptr;

    vk::raii::Image        depthImage  = nullptr;
    vk::raii::DeviceMemory depthMemory = nullptr;
    vk::raii::ImageView    depthView   = nullptr;

    vk::Format      colorFormat  = vk::Format::eUndefined;
    vk::Format      depthFormat  = vk::Format::eUndefined;
    vk::Extent2D    extent       = {};
    vk::ImageLayout colorLayout  = vk::ImageLayout::eUndefined;
};

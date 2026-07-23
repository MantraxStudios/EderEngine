#pragma once
#include "ImportCore.h"
#include <array>

// ─────────────────────────────────────────────────────────────────────────────
//  VulkanGBuffer — multi-render-target target for the deferred geometry pass.
//
//  Layout (written by gbuffer.frag):
//    GB0  RGBA8      : albedo.rgb, a = emissiveIntensity
//    GB1  RGBA16F    : world-space normal.xyz
//    GB2  RGBA8      : r = roughness, g = metallic
//    depth D32       : scene depth (also sampled by SSAO / lighting)
// ─────────────────────────────────────────────────────────────────────────────
class EDERGRAPHICS_API VulkanGBuffer
{
public:
    static constexpr uint32_t COLOR_COUNT = 3;

    void Create   (uint32_t width, uint32_t height, vk::Format depthFormat);
    void Recreate (uint32_t width, uint32_t height);
    void Destroy  ();

    void BeginRendering        (vk::CommandBuffer cmd);
    void EndRendering          (vk::CommandBuffer cmd);
    void TransitionToShaderRead(vk::CommandBuffer cmd);

    vk::ImageView GetAlbedoView()   const { return *colorViews[0]; }
    vk::ImageView GetNormalView()   const { return *colorViews[1]; }
    vk::ImageView GetMaterialView() const { return *colorViews[2]; }
    vk::ImageView GetDepthView()    const { return *depthView;     }
    vk::Sampler   GetSampler()      const { return *sampler;       }
    vk::Extent2D  GetExtent()       const { return extent;         }

    // Colour formats in attachment order — feed to VulkanPipeline::Create for MRT.
    static const std::array<vk::Format, COLOR_COUNT>& ColorFormats();

private:
    uint32_t FindMemoryType(uint32_t filter, vk::MemoryPropertyFlags props);

    std::array<vk::raii::Image,        COLOR_COUNT> colorImages  = { nullptr, nullptr, nullptr };
    std::array<vk::raii::DeviceMemory, COLOR_COUNT> colorMemory  = { nullptr, nullptr, nullptr };
    std::array<vk::raii::ImageView,    COLOR_COUNT> colorViews   = { nullptr, nullptr, nullptr };

    vk::raii::Image        depthImage  = nullptr;
    vk::raii::DeviceMemory depthMemory = nullptr;
    vk::raii::ImageView    depthView   = nullptr;
    vk::raii::Sampler      sampler     = nullptr;

    vk::Format   depthFormat = vk::Format::eUndefined;
    vk::Extent2D extent      = {};
    std::array<vk::ImageLayout, COLOR_COUNT> colorLayouts =
        { vk::ImageLayout::eUndefined, vk::ImageLayout::eUndefined, vk::ImageLayout::eUndefined };
};

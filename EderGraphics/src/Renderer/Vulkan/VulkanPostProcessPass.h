#pragma once
#include "Core/DLLHeader.h"
#include <vulkan/vulkan_raii.hpp>
#include <string>
#include <vector>
#include "Renderer/Vulkan/VulkanFramebuffer.h"
#include "Renderer/Vulkan/VulkanBuffer.h"

// ─────────────────────────────────────────────────────────────────────────────
//  VulkanPostProcessPass
//  Executes one custom full-screen post-process effect.
//
//  Shader contract for user fragment shaders:
//    layout(set=0, binding=0) uniform sampler2D scene;   // input color
//    layout(set=0, binding=1) uniform sampler2D depth;   // scene depth
//    layout(std140, set=0, binding=2) uniform PPParams { vec4 p[4]; };  // 16 floats
//    layout(location=0) in  vec2 fragUV;
//    layout(location=0) out vec4 outColor;
//
//  The vertex shader is always the fullscreen triangle (fog.vert.spv).
// ─────────────────────────────────────────────────────────────────────────────
class EDERGRAPHICS_API VulkanPostProcessPass
{
public:
    // fragShaderPath: content-relative, e.g. "shaders/my_effect.frag.spv"
    void Create (vk::Format colorFormat, uint32_t w, uint32_t h,
                 const std::string& fragShaderPath);
    void Destroy();
    void Resize (uint32_t w, uint32_t h);

    // Hot-reload: rebuild pipeline with a different (or the same) frag shader.
    void Rebuild(const std::string& fragShaderPath);

    // Draw effect: reads srcView, writes to outputFb.
    void Draw(vk::CommandBuffer cmd,
              vk::ImageView     srcView,
              vk::Sampler       srcSampler,
              vk::ImageView     depthView,
              vk::Sampler       depthSampler,
              const float       params[16]);

    VulkanFramebuffer& GetOutput() { return m_outputFb; }

private:
    struct alignas(16) ParamsUBO { float data[16]; };

    void BuildPipeline(const std::string& fragShaderPath);
    void UpdateDescriptor(vk::ImageView srcView,   vk::Sampler srcSampler,
                          vk::ImageView depthView, vk::Sampler depthSampler);
    static std::vector<uint32_t> LoadSpv(const std::string& path);

    VulkanFramebuffer             m_outputFb;
    vk::Format                    m_fmt = vk::Format::eUndefined;
    std::string                   m_fragShaderPath;

    VulkanBuffer                  m_uboBuffer;

    vk::raii::DescriptorSetLayout m_setLayout      = nullptr;
    vk::raii::DescriptorPool      m_descriptorPool = nullptr;
    vk::raii::DescriptorSet       m_descriptorSet  = nullptr;
    vk::raii::PipelineLayout      m_pipelineLayout = nullptr;
    vk::raii::Pipeline            m_pipeline       = nullptr;

    vk::ImageView m_lastSrcView   = nullptr;
    vk::Sampler   m_lastSrcSmp    = nullptr;
    vk::ImageView m_lastDepthView = nullptr;
    vk::Sampler   m_lastDepthSmp  = nullptr;
};

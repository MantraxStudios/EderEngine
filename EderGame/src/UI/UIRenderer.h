#pragma once
#include <Renderer/Vulkan/ImportCore.h>
#include <Renderer/Vulkan/VulkanTexture.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include "UITypes.h"
#include <imgui/imstb_truetype.h>

struct UIGlyphInfo
{
    float u0, v0, u1, v1;
    float xOff, yOff;
    float xAdvance;
    float w, h;
};

struct UIFontAtlas
{
    static constexpr int W          = 512;
    static constexpr int H          = 512;
    static constexpr int FIRST_CHAR = 32;
    static constexpr int NUM_CHARS  = 96;

    std::vector<UIGlyphInfo> glyphs;
    float                    bakedSize = 24.f;
    float                    ascent    = 0.f;
    float                    descent   = 0.f;
    float                    lineGap   = 0.f;
    bool                     loaded    = false;

    vk::raii::Image        image     = nullptr;
    vk::raii::DeviceMemory memory    = nullptr;
    vk::raii::ImageView    imageView = nullptr;
    vk::raii::Sampler      sampler   = nullptr;
};

struct UIPushConstants
{
    float rectX, rectY, rectW, rectH;
    float uvX0, uvY0, uvX1, uvY1;
    float colorR, colorG, colorB, colorA;
    float scale;
    float offsetX;
    float offsetY;
    float screenW;
    float screenH;
    float mode;
    float pad0;
    float pad1;
};
static_assert(sizeof(UIPushConstants) == 80, "UIPushConstants size mismatch");

class UIRenderer
{
public:
    void Create (vk::Format swapFormat, vk::Format depthFormat);
    void Destroy();
    void Draw   (vk::CommandBuffer cmd);

    void LoadFont(const std::string& path, float size);

private:
    std::vector<uint32_t> LoadSpv(const std::string& path);

    vk::raii::Pipeline CreatePipeline(vk::Format swapFormat, vk::Format depthFormat);

    void DrawQuad(vk::CommandBuffer cmd,
                  float rx, float ry, float rw, float rh,
                  float u0, float v0, float u1, float v1,
                  const glm::vec4& color,
                  float mode,
                  vk::DescriptorSet ds) const;

    void DrawElement    (vk::CommandBuffer cmd, const UIElement& e);
    void DrawImage      (vk::CommandBuffer cmd, const UIElement& e);
    void DrawText       (vk::CommandBuffer cmd, const UIElement& e,
                         float rx, float ry, float rw, float rh,
                         const glm::vec4& color, float fontSize, bool centered);
    void DrawButton     (vk::CommandBuffer cmd, const UIElement& e);
    void DrawSlider     (vk::CommandBuffer cmd, const UIElement& e);
    void DrawInputField (vk::CommandBuffer cmd, const UIElement& e);

    vk::DescriptorSet GetOrCreateDescriptorSet(vk::ImageView view, vk::Sampler sampler);

    void UploadFontBitmap(const uint8_t* pixels, int w, int h);

    vk::raii::CommandPool   CreateOneTimePool();
    vk::raii::CommandBuffer BeginOneTime(vk::raii::CommandPool& pool);
    void                    EndOneTime(vk::raii::CommandBuffer& cmd);

    vk::raii::DescriptorSetLayout m_dsl      = nullptr;
    vk::raii::PipelineLayout      m_layout   = nullptr;
    vk::raii::Pipeline            m_pipeline = nullptr;
    vk::raii::DescriptorPool      m_pool     = nullptr;

    std::vector<vk::raii::DescriptorSet>             m_allocatedSets;
    std::unordered_map<VkImageView, VkDescriptorSet> m_setCache;

    VulkanTexture     m_whiteTex;
    vk::DescriptorSet m_whiteDS = VK_NULL_HANDLE;

    UIFontAtlas       m_font;
    vk::DescriptorSet m_fontDS = VK_NULL_HANDLE;

    std::unordered_map<std::string, VulkanTexture*> m_texCache;
    std::vector<std::unique_ptr<VulkanTexture>>     m_texStorage;

    float m_scale   = 1.f;
    float m_offsetX = 0.f;
    float m_offsetY = 0.f;
    float m_screenW = 1920.f;
    float m_screenH = 1080.f;
};

void UIRenderer_LoadFont(const std::string& path, float size);
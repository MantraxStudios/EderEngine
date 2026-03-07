#include "UIRenderer.h"
#include "UISystem.h"
#include <Renderer/Vulkan/VulkanInstance.h>
#include <Renderer/Vulkan/VulkanSwapchain.h>
#include <IO/AssetManager.h>
#include <imgui/imstb_truetype.h>
#include <fstream>
#include <cstring>
#include <iostream>
#include <algorithm>
#include <cmath>

static UIRenderer* s_renderer = nullptr;

void UIRenderer_LoadFont(const std::string& path, float size)
{
    if (s_renderer) s_renderer->LoadFont(path, size);
}

std::vector<uint32_t> UIRenderer::LoadSpv(const std::string& path)
{
    auto& am = Krayon::AssetManager::Get();
    if (!am.GetWorkDir().empty() || am.IsCompiled())
    {
        auto bytes = am.GetBytes(path);
        if (!bytes.empty())
        {
            std::vector<uint32_t> buf(bytes.size() / sizeof(uint32_t));
            std::memcpy(buf.data(), bytes.data(), bytes.size());
            return buf;
        }
        if (am.IsCompiled())
            throw std::runtime_error("[UIRenderer] shader not found in PAK: " + path);
    }
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open())
        throw std::runtime_error("[UIRenderer] cannot open shader: " + path);
    size_t size = static_cast<size_t>(file.tellg());
    std::vector<uint32_t> buf(size / sizeof(uint32_t));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(buf.data()), size);
    return buf;
}

vk::raii::CommandPool UIRenderer::CreateOneTimePool()
{
    auto& device = VulkanInstance::Get().GetDevice();
    vk::CommandPoolCreateInfo ci{};
    ci.flags            = vk::CommandPoolCreateFlagBits::eTransient;
    ci.queueFamilyIndex = VulkanInstance::Get().GetGraphicsIndex();
    return vk::raii::CommandPool(device, ci);
}

vk::CommandBuffer UIRenderer::BeginOneTime(vk::raii::CommandPool& pool)
{
    auto& device = VulkanInstance::Get().GetDevice();
    vk::CommandBufferAllocateInfo ai{};
    ai.commandPool        = *pool;
    ai.level              = vk::CommandBufferLevel::ePrimary;
    ai.commandBufferCount = 1;
    auto bufs = device.allocateCommandBuffers(ai);

    vk::CommandBufferBeginInfo bi{};
    bi.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
    bufs[0].begin(bi);
    return *bufs[0];
}

void UIRenderer::EndOneTime(vk::raii::CommandPool& pool, vk::CommandBuffer cmd)
{
    cmd.end();
    auto& instance = VulkanInstance::Get();
    vk::SubmitInfo si{};
    si.commandBufferCount = 1;
    si.pCommandBuffers    = &cmd;
    instance.GetGraphicsQueue().submit(si, nullptr);
    instance.GetGraphicsQueue().waitIdle();
}

void UIRenderer::UploadFontBitmap(const uint8_t* pixels, int w, int h)
{
    auto& device   = VulkanInstance::Get().GetDevice();
    auto& instance = VulkanInstance::Get();

    auto findMemType = [&](uint32_t filter, vk::MemoryPropertyFlags props) -> uint32_t {
        auto memProps = instance.GetPhysicalDevice().getMemoryProperties();
        for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
            if ((filter & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & props) == props)
                return i;
        throw std::runtime_error("[UIRenderer] No suitable memory type");
    };

    // Expand R8 atlas to RGBA8 (R=G=B=A=font_value) for portable hardware sampling
    std::vector<uint8_t> rgba(w * h * 4);
    for (int i = 0; i < w * h; ++i)
    {
        rgba[i*4+0] = pixels[i];
        rgba[i*4+1] = pixels[i];
        rgba[i*4+2] = pixels[i];
        rgba[i*4+3] = pixels[i];
    }

    vk::DeviceSize size = (vk::DeviceSize)w * h * 4;

    vk::BufferCreateInfo bci{};
    bci.size        = size;
    bci.usage       = vk::BufferUsageFlagBits::eTransferSrc;
    bci.sharingMode = vk::SharingMode::eExclusive;
    vk::raii::Buffer staging(device, bci);

    auto stagingReqs = staging.getMemoryRequirements();
    vk::MemoryAllocateInfo sai{};
    sai.allocationSize  = stagingReqs.size;
    sai.memoryTypeIndex = findMemType(stagingReqs.memoryTypeBits,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
    vk::raii::DeviceMemory stagingMem(device, sai);
    staging.bindMemory(*stagingMem, 0);

    void* mapped = stagingMem.mapMemory(0, size);
    std::memcpy(mapped, rgba.data(), (size_t)size);
    stagingMem.unmapMemory();

    vk::ImageCreateInfo ici{};
    ici.imageType   = vk::ImageType::e2D;
    ici.format      = vk::Format::eR8G8B8A8Unorm;
    ici.extent      = vk::Extent3D{ (uint32_t)w, (uint32_t)h, 1u };
    ici.mipLevels   = 1;
    ici.arrayLayers = 1;
    ici.samples     = vk::SampleCountFlagBits::e1;
    ici.tiling      = vk::ImageTiling::eOptimal;
    ici.usage       = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
    ici.initialLayout = vk::ImageLayout::eUndefined;
    m_font.image = vk::raii::Image(device, ici);

    auto imgReqs = m_font.image.getMemoryRequirements();
    vk::MemoryAllocateInfo iai{};
    iai.allocationSize  = imgReqs.size;
    iai.memoryTypeIndex = findMemType(imgReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
    m_font.memory = vk::raii::DeviceMemory(device, iai);
    m_font.image.bindMemory(*m_font.memory, 0);

    auto pool = CreateOneTimePool();
    auto cmd  = BeginOneTime(pool);

    vk::ImageMemoryBarrier barrier{};
    barrier.oldLayout           = vk::ImageLayout::eUndefined;
    barrier.newLayout           = vk::ImageLayout::eTransferDstOptimal;
    barrier.image               = *m_font.image;
    barrier.subresourceRange    = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 };
    barrier.srcAccessMask       = {};
    barrier.dstAccessMask       = vk::AccessFlagBits::eTransferWrite;
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer,
                        {}, {}, {}, barrier);

    vk::BufferImageCopy copy{};
    copy.imageSubresource = { vk::ImageAspectFlagBits::eColor, 0, 0, 1 };
    copy.imageExtent      = vk::Extent3D{ (uint32_t)w, (uint32_t)h, 1u };
    cmd.copyBufferToImage(*staging, *m_font.image, vk::ImageLayout::eTransferDstOptimal, copy);

    barrier.oldLayout   = vk::ImageLayout::eTransferDstOptimal;
    barrier.newLayout   = vk::ImageLayout::eShaderReadOnlyOptimal;
    barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
    barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader,
                        {}, {}, {}, barrier);

    EndOneTime(pool, cmd);

    vk::ImageViewCreateInfo vci{};
    vci.image            = *m_font.image;
    vci.viewType         = vk::ImageViewType::e2D;
    vci.format           = vk::Format::eR8G8B8A8Unorm;
    vci.subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 };
    m_font.imageView = vk::raii::ImageView(device, vci);

    vk::SamplerCreateInfo sci{};
    sci.magFilter    = vk::Filter::eLinear;
    sci.minFilter    = vk::Filter::eLinear;
    sci.addressModeU = vk::SamplerAddressMode::eClampToEdge;
    sci.addressModeV = vk::SamplerAddressMode::eClampToEdge;
    m_font.sampler = vk::raii::Sampler(device, sci);

    m_fontDS = GetOrCreateDescriptorSet(*m_font.imageView, *m_font.sampler);
}

void UIRenderer::LoadFont(const std::string& path, float size)
{
    std::vector<uint8_t> ttfData;

    auto& am = Krayon::AssetManager::Get();
    if (!am.GetWorkDir().empty() || am.IsCompiled())
        ttfData = am.GetBytes(path);

    if (ttfData.empty())
    {
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (!f.is_open()) { std::cerr << "[UIRenderer] font not found: " << path << "\n"; return; }
        size_t fsz = (size_t)f.tellg();
        ttfData.resize(fsz);
        f.seekg(0);
        f.read(reinterpret_cast<char*>(ttfData.data()), fsz);
    }

    if (ttfData.empty()) return;

    if (m_font.loaded) { m_font.image = nullptr; m_font.memory = nullptr;
                         m_font.imageView = nullptr; m_font.sampler = nullptr; }

    m_font.glyphs.resize(UIFontAtlas::NUM_CHARS);
    m_font.bakedSize = size;

    std::vector<uint8_t> bitmap(UIFontAtlas::W * UIFontAtlas::H);
    int result = stbtt_BakeFontBitmap(
        ttfData.data(), 0, size, bitmap.data(),
        UIFontAtlas::W, UIFontAtlas::H,
        UIFontAtlas::FIRST_CHAR, UIFontAtlas::NUM_CHARS,
        m_font.glyphs.data());

    if (result <= 0)
    {
        std::cerr << "[UIRenderer] stbtt_BakeFontBitmap failed (result=" << result << ")\n";
        return;
    }

    auto maxPx = *std::max_element(bitmap.begin(), bitmap.end());
    std::cout << "[UIRenderer] Atlas baked OK, result=" << result
              << " maxPixel=" << (int)maxPx << "\n";
    if (maxPx == 0)
    {
        std::cerr << "[UIRenderer] Atlas is all zeros - font data invalid\n";
        return;
    }

    UploadFontBitmap(bitmap.data(), UIFontAtlas::W, UIFontAtlas::H);
    m_font.loaded = true;
    std::cout << "[UIRenderer] Font loaded: " << path << " @ " << size << "px\n";
}

vk::DescriptorSet UIRenderer::GetOrCreateDescriptorSet(vk::ImageView view, vk::Sampler sampler)
{
    auto it = m_setCache.find((VkImageView)view);
    if (it != m_setCache.end()) return it->second;

    auto& device = VulkanInstance::Get().GetDevice();
    vk::DescriptorSetLayout dsl = *m_dsl;
    vk::DescriptorSetAllocateInfo ai{};
    ai.descriptorPool     = *m_pool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts        = &dsl;
    auto sets = device.allocateDescriptorSets(ai);
    m_allocatedSets.push_back(std::move(sets[0]));
    vk::DescriptorSet ds = *m_allocatedSets.back();

    vk::DescriptorImageInfo ii{};
    ii.sampler     = sampler;
    ii.imageView   = view;
    ii.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    vk::WriteDescriptorSet wr{};
    wr.dstSet          = ds;
    wr.dstBinding      = 0;
    wr.descriptorCount = 1;
    wr.descriptorType  = vk::DescriptorType::eCombinedImageSampler;
    wr.pImageInfo      = &ii;
    device.updateDescriptorSets(wr, nullptr);

    m_setCache[(VkImageView)view] = ds;
    return ds;
}

vk::raii::Pipeline UIRenderer::CreatePipeline(vk::Format swapFormat, vk::Format depthFormat)
{
    auto& device = VulkanInstance::Get().GetDevice();

    auto mkMod = [&](const std::vector<uint32_t>& code) {
        vk::ShaderModuleCreateInfo ci{};
        ci.codeSize = code.size() * sizeof(uint32_t);
        ci.pCode    = code.data();
        return vk::raii::ShaderModule(device, ci);
    };

    auto vertMod = mkMod(LoadSpv("shaders/ui.vert.spv"));
    auto fragMod = mkMod(LoadSpv("shaders/ui.frag.spv"));

    vk::PipelineShaderStageCreateInfo stages[2]{};
    stages[0].stage  = vk::ShaderStageFlagBits::eVertex;
    stages[0].module = *vertMod;
    stages[0].pName  = "main";
    stages[1].stage  = vk::ShaderStageFlagBits::eFragment;
    stages[1].module = *fragMod;
    stages[1].pName  = "main";

    vk::PipelineVertexInputStateCreateInfo   vi{};
    vk::PipelineInputAssemblyStateCreateInfo ia{};
    ia.topology = vk::PrimitiveTopology::eTriangleList;

    vk::PipelineViewportStateCreateInfo vps{};
    vps.viewportCount = 1;
    vps.scissorCount  = 1;

    vk::PipelineRasterizationStateCreateInfo rast{};
    rast.polygonMode = vk::PolygonMode::eFill;
    rast.cullMode    = vk::CullModeFlagBits::eNone;
    rast.lineWidth   = 1.f;

    vk::PipelineMultisampleStateCreateInfo ms{};
    ms.rasterizationSamples = vk::SampleCountFlagBits::e1;

    vk::PipelineDepthStencilStateCreateInfo ds{};
    ds.depthTestEnable  = vk::False;
    ds.depthWriteEnable = vk::False;

    vk::PipelineColorBlendAttachmentState blendAtt{};
    blendAtt.blendEnable         = vk::True;
    blendAtt.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
    blendAtt.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
    blendAtt.colorBlendOp        = vk::BlendOp::eAdd;
    blendAtt.srcAlphaBlendFactor = vk::BlendFactor::eOne;
    blendAtt.dstAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
    blendAtt.alphaBlendOp        = vk::BlendOp::eAdd;
    blendAtt.colorWriteMask      =
        vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
        vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;

    vk::PipelineColorBlendStateCreateInfo blend{};
    blend.attachmentCount = 1;
    blend.pAttachments    = &blendAtt;

    std::array<vk::DynamicState, 2> dynStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
    vk::PipelineDynamicStateCreateInfo dyn{};
    dyn.dynamicStateCount = (uint32_t)dynStates.size();
    dyn.pDynamicStates    = dynStates.data();

    vk::PipelineRenderingCreateInfo ri{};
    ri.colorAttachmentCount    = 1;
    ri.pColorAttachmentFormats = &swapFormat;
    ri.depthAttachmentFormat   = depthFormat;

    vk::GraphicsPipelineCreateInfo pi{};
    pi.pNext               = &ri;
    pi.stageCount          = 2;
    pi.pStages             = stages;
    pi.pVertexInputState   = &vi;
    pi.pInputAssemblyState = &ia;
    pi.pViewportState      = &vps;
    pi.pRasterizationState = &rast;
    pi.pMultisampleState   = &ms;
    pi.pDepthStencilState  = &ds;
    pi.pColorBlendState    = &blend;
    pi.pDynamicState       = &dyn;
    pi.layout              = *m_layout;

    return vk::raii::Pipeline(device, nullptr, pi);
}

void UIRenderer::Create(vk::Format swapFormat, vk::Format depthFormat)
{
    s_renderer = this;
    auto& device = VulkanInstance::Get().GetDevice();

    vk::DescriptorSetLayoutBinding samplerBinding{};
    samplerBinding.binding         = 0;
    samplerBinding.descriptorType  = vk::DescriptorType::eCombinedImageSampler;
    samplerBinding.descriptorCount = 1;
    samplerBinding.stageFlags      = vk::ShaderStageFlagBits::eFragment;

    vk::DescriptorSetLayoutCreateInfo dslCI{};
    dslCI.bindingCount = 1;
    dslCI.pBindings    = &samplerBinding;
    m_dsl = vk::raii::DescriptorSetLayout(device, dslCI);

    vk::PushConstantRange pcRange{};
    pcRange.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
    pcRange.offset     = 0;
    pcRange.size       = sizeof(UIPushConstants);

    vk::DescriptorSetLayout dsl = *m_dsl;
    vk::PipelineLayoutCreateInfo plCI{};
    plCI.setLayoutCount         = 1;
    plCI.pSetLayouts            = &dsl;
    plCI.pushConstantRangeCount = 1;
    plCI.pPushConstantRanges    = &pcRange;
    m_layout = vk::raii::PipelineLayout(device, plCI);

    vk::DescriptorPoolSize poolSize{ vk::DescriptorType::eCombinedImageSampler, 64 };
    vk::DescriptorPoolCreateInfo poolCI{};
    poolCI.flags         = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    poolCI.maxSets       = 64;
    poolCI.poolSizeCount = 1;
    poolCI.pPoolSizes    = &poolSize;
    m_pool = vk::raii::DescriptorPool(device, poolCI);

    m_pipeline = CreatePipeline(swapFormat, depthFormat);

    m_whiteTex.CreateDefault();
    m_whiteDS = GetOrCreateDescriptorSet(m_whiteTex.GetImageView(), m_whiteTex.GetSampler());
}

void UIRenderer::Destroy()
{
    m_setCache.clear();
    m_allocatedSets.clear();
    m_texStorage.clear();
    m_texCache.clear();
    m_whiteTex.Destroy();
    m_font.image     = nullptr;
    m_font.memory    = nullptr;
    m_font.imageView = nullptr;
    m_font.sampler   = nullptr;
    m_font.loaded    = false;
    m_pipeline   = nullptr;
    m_layout     = nullptr;
    m_dsl        = nullptr;
    m_pool       = nullptr;
    s_renderer   = nullptr;
}

void UIRenderer::DrawQuad(vk::CommandBuffer cmd,
                           float rx, float ry, float rw, float rh,
                           float u0, float v0, float u1, float v1,
                           const glm::vec4& color,
                           float mode,
                           vk::DescriptorSet ds) const
{
    vk::DescriptorSet dsBind = ds;
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *m_layout, 0, dsBind, nullptr);

    UIPushConstants pc{};
    pc.rectX   = rx; pc.rectY = ry; pc.rectW = rw; pc.rectH = rh;
    pc.uvX0    = u0; pc.uvY0  = v0; pc.uvX1  = u1; pc.uvY1  = v1;
    pc.colorR  = color.r; pc.colorG = color.g; pc.colorB = color.b; pc.colorA = color.a;
    pc.scale   = m_scale;
    pc.offsetX = m_offsetX;
    pc.offsetY = m_offsetY;
    pc.screenW = m_screenW;
    pc.screenH = m_screenH;
    pc.mode    = mode;

    cmd.pushConstants<UIPushConstants>(*m_layout,
        vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, pc);
    cmd.draw(6, 1, 0, 0);
}

void UIRenderer::DrawText(vk::CommandBuffer cmd, const UIElement& e,
                           float rx, float ry, float rw, float rh,
                           const glm::vec4& color, float fontSize, bool centered)
{
    if (!m_font.loaded || e.text.empty()) return;

    const std::string& str = e.text;

    // One-time diagnostic to help debug text rendering
    static bool s_debugOnce = false;
    if (!s_debugOnce)
    {
        s_debugOnce = true;
        int ci0 = (int)(uint8_t)str[0] - UIFontAtlas::FIRST_CHAR;
        if (ci0 >= 0 && ci0 < UIFontAtlas::NUM_CHARS)
        {
            float tx = 0.f, ty = 0.f;
            stbtt_aligned_quad dbgQ;
            stbtt_GetBakedQuad(m_font.glyphs.data(), UIFontAtlas::W, UIFontAtlas::H, ci0, &tx, &ty, &dbgQ, 1);
            float scl = fontSize / m_font.bakedSize;
            float sY  = ry + rh * 0.5f - fontSize * 0.5f + fontSize * 0.75f;
            std::cout << "[UIRenderer] DrawText first-glyph: char='" << str[0]
                      << "' uv=(" << dbgQ.s0 << "," << dbgQ.t0 << ")-(" << dbgQ.s1 << "," << dbgQ.t1 << ")"
                      << " gx=" << (rx + dbgQ.x0 * scl) << " gy=" << (sY + dbgQ.y0 * scl)
                      << " gw=" << (dbgQ.x1 - dbgQ.x0) * scl << " gh=" << (dbgQ.y1 - dbgQ.y0) * scl
                      << " bakedSize=" << m_font.bakedSize << " fontSize=" << fontSize << " scale=" << scl
                      << " fontDS=" << (m_fontDS ? "valid" : "NULL") << "\n";
        }
    }
    float scale = fontSize / m_font.bakedSize;

    float totalW = 0.f;
    float xpos = 0.f, ypos = 0.f;
    for (char c : str)
    {
        int ci = (int)(uint8_t)c - UIFontAtlas::FIRST_CHAR;
        if (ci < 0 || ci >= UIFontAtlas::NUM_CHARS) continue;
        stbtt_aligned_quad q;
        stbtt_GetBakedQuad(m_font.glyphs.data(), UIFontAtlas::W, UIFontAtlas::H, ci, &xpos, &ypos, &q, 1);
        totalW = xpos;
    }
    totalW *= scale;

    float startX = rx;
    float startY = ry + rh * 0.5f - fontSize * 0.5f + fontSize * 0.75f;

    if (centered)
        startX = rx + (rw - totalW) * 0.5f;

    xpos = 0.f;
    ypos = 0.f;

    for (char c : str)
    {
        int ci = (int)(uint8_t)c - UIFontAtlas::FIRST_CHAR;
        if (ci < 0 || ci >= UIFontAtlas::NUM_CHARS) continue;

        stbtt_aligned_quad q;
        stbtt_GetBakedQuad(m_font.glyphs.data(), UIFontAtlas::W, UIFontAtlas::H, ci, &xpos, &ypos, &q, 1);

        float gx = startX + q.x0 * scale;
        float gy = startY + q.y0 * scale;
        float gw = (q.x1 - q.x0) * scale;
        float gh = (q.y1 - q.y0) * scale;

        DrawQuad(cmd, gx, gy, gw, gh, q.s0, q.t0, q.s1, q.t1, color, 2.f, m_fontDS);
    }
}

void UIRenderer::DrawImage(vk::CommandBuffer cmd, const UIElement& e)
{
    glm::vec4 rect = UISystem::Get().ComputeVirtualRect(e);

    vk::DescriptorSet ds = m_whiteDS;
    if (!e.texturePath.empty())
    {
        auto it = m_texCache.find(e.texturePath);
        if (it == m_texCache.end())
        {
            auto tex = std::make_unique<VulkanTexture>();
            tex->Load(e.texturePath);
            if (tex->IsValid())
            {
                m_texStorage.push_back(std::move(tex));
                m_texCache[e.texturePath] = m_texStorage.back().get();
            }
        }
        it = m_texCache.find(e.texturePath);
        if (it != m_texCache.end() && it->second->IsValid())
            ds = GetOrCreateDescriptorSet(it->second->GetImageView(), it->second->GetSampler());
    }

    float mode = e.texturePath.empty() ? 0.f : 1.f;
    DrawQuad(cmd, rect.x, rect.y, rect.z, rect.w, 0.f, 0.f, 1.f, 1.f, e.color, mode, ds);
}

void UIRenderer::DrawButton(vk::CommandBuffer cmd, const UIElement& e)
{
    glm::vec4 rect = UISystem::Get().ComputeVirtualRect(e);
    DrawQuad(cmd, rect.x, rect.y, rect.z, rect.w, 0.f, 0.f, 1.f, 1.f, e.color, 0.f, m_whiteDS);
    DrawText(cmd, e, rect.x, rect.y, rect.z, rect.w, e.textColor, e.fontSize, true);
}

void UIRenderer::DrawSlider(vk::CommandBuffer cmd, const UIElement& e)
{
    glm::vec4 rect = UISystem::Get().ComputeVirtualRect(e);
    float rx = rect.x, ry = rect.y, rw = rect.z, rh = rect.w;

    DrawQuad(cmd, rx, ry, rw, rh, 0.f, 0.f, 1.f, 1.f, e.color, 0.f, m_whiteDS);

    float t   = (e.maxValue > e.minValue) ? (e.value - e.minValue) / (e.maxValue - e.minValue) : 0.f;
    float fillW = rw * t;
    if (fillW > 0.f)
        DrawQuad(cmd, rx, ry, fillW, rh, 0.f, 0.f, 1.f, 1.f, e.fillColor, 0.f, m_whiteDS);

    float handleW  = rh;
    float handleX  = rx + fillW - handleW * 0.5f;
    handleX = std::clamp(handleX, rx, rx + rw - handleW);
    DrawQuad(cmd, handleX, ry, handleW, rh, 0.f, 0.f, 1.f, 1.f, e.handleColor, 0.f, m_whiteDS);
}

void UIRenderer::DrawInputField(vk::CommandBuffer cmd, const UIElement& e)
{
    glm::vec4 rect = UISystem::Get().ComputeVirtualRect(e);
    float rx = rect.x, ry = rect.y, rw = rect.z, rh = rect.w;

    DrawQuad(cmd, rx, ry, rw, rh, 0.f, 0.f, 1.f, 1.f, e.color, 0.f, m_whiteDS);

    glm::vec4 borderColor = e.focused
        ? glm::vec4(0.4f, 0.7f, 1.f, 1.f)
        : glm::vec4(0.5f, 0.5f, 0.5f, 1.f);
    float bw = 2.f;
    DrawQuad(cmd, rx,        ry,        rw, bw, 0.f, 0.f, 1.f, 1.f, borderColor, 0.f, m_whiteDS);
    DrawQuad(cmd, rx,        ry+rh-bw,  rw, bw, 0.f, 0.f, 1.f, 1.f, borderColor, 0.f, m_whiteDS);
    DrawQuad(cmd, rx,        ry,        bw, rh, 0.f, 0.f, 1.f, 1.f, borderColor, 0.f, m_whiteDS);
    DrawQuad(cmd, rx+rw-bw,  ry,        bw, rh, 0.f, 0.f, 1.f, 1.f, borderColor, 0.f, m_whiteDS);

    UIElement textElem    = e;
    const std::string& displayStr = e.inputText.empty() && !e.focused
        ? e.placeholder
        : e.inputText;
    textElem.text      = displayStr + (e.focused && e.cursorVisible ? "|" : "");
    glm::vec4 tc       = e.inputText.empty() && !e.focused
        ? glm::vec4(0.5f, 0.5f, 0.5f, 1.f)
        : e.textColor;

    float pad = 8.f;
    DrawText(cmd, textElem, rx + pad, ry, rw - pad * 2.f, rh, tc, e.fontSize, false);
}

void UIRenderer::DrawElement(vk::CommandBuffer cmd, const UIElement& e)
{
    switch (e.type)
    {
    case UIElementType::Image:      DrawImage(cmd, e);      break;
    case UIElementType::Text:
    {
        glm::vec4 rect = UISystem::Get().ComputeVirtualRect(e);
        DrawText(cmd, e, rect.x, rect.y, rect.z, rect.w, e.textColor, e.fontSize, false);
        break;
    }
    case UIElementType::Button:     DrawButton(cmd, e);     break;
    case UIElementType::Slider:     DrawSlider(cmd, e);     break;
    case UIElementType::InputField: DrawInputField(cmd, e); break;
    }
}

void UIRenderer::Draw(vk::CommandBuffer cmd)
{
    auto& swapchain = VulkanSwapchain::Get();
    m_screenW = (float)swapchain.GetExtent().width;
    m_screenH = (float)swapchain.GetExtent().height;

    UISystem::Get().SetScreenSize(m_screenW, m_screenH);
    m_scale   = UISystem::Get().GetScale();
    m_offsetX = UISystem::Get().GetOffsetX();
    m_offsetY = UISystem::Get().GetOffsetY();

    auto elements = UISystem::Get().GetSortedElements();
    if (elements.empty()) return;

    vk::Viewport vp{};
    vp.width    = m_screenW;
    vp.height   = m_screenH;
    vp.minDepth = 0.f;
    vp.maxDepth = 1.f;
    cmd.setViewport(0, vp);
    cmd.setScissor(0, vk::Rect2D{ {0,0}, swapchain.GetExtent() });

    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_pipeline);

    for (auto* e : elements)
    {
        if (e->visible)
            DrawElement(cmd, *e);
    }
}

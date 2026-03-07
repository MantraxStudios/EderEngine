#include "VulkanPostProcessPass.h"
#include "Renderer/Vulkan/VulkanInstance.h"
#include "Renderer/VulkanRenderer.h"
#include <IO/AssetManager.h>
#include <fstream>
#include <stdexcept>
#include <cstring>
#include <array>

// ─────────────────────────────────────────────────────────────────────────────
// Shader loading (mirrors the pattern from all other post-process passes)
// ─────────────────────────────────────────────────────────────────────────────
std::vector<uint32_t> VulkanPostProcessPass::LoadSpv(const std::string& path)
{
    auto& am = Krayon::AssetManager::Get();
    if (!am.GetWorkDir().empty())
    {
        auto bytes = am.GetBytes(path);
        if (!bytes.empty())
        {
            std::vector<uint32_t> buf(bytes.size() / sizeof(uint32_t));
            std::memcpy(buf.data(), bytes.data(), bytes.size());
            return buf;
        }
        throw std::runtime_error("[AssetManager] Shader not found: " + path);
    }
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open())
        throw std::runtime_error("VulkanPostProcessPass: cannot open shader: " + path);
    size_t sz = static_cast<size_t>(f.tellg());
    std::vector<uint32_t> buf(sz / sizeof(uint32_t));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(buf.data()), sz);
    return buf;
}

// ─────────────────────────────────────────────────────────────────────────────
// Create / Destroy / Resize
// ─────────────────────────────────────────────────────────────────────────────
void VulkanPostProcessPass::Create(vk::Format colorFormat, uint32_t w, uint32_t h,
                                    const std::string& fragShaderPath)
{
    m_fmt            = colorFormat;
    m_fragShaderPath = fragShaderPath;

    auto& device = VulkanInstance::Get().GetDevice();

    // ── Descriptor set layout ────────────────────────────────────────────────
    // binding 0 : scene color   (CIS)
    // binding 1 : scene depth   (CIS)
    // binding 2 : ParamsUBO     (UBO, 16 floats)
    vk::DescriptorSetLayoutBinding bindings[3]{};

    bindings[0].binding         = 0;
    bindings[0].descriptorType  = vk::DescriptorType::eCombinedImageSampler;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags      = vk::ShaderStageFlagBits::eFragment;

    bindings[1].binding         = 1;
    bindings[1].descriptorType  = vk::DescriptorType::eCombinedImageSampler;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags      = vk::ShaderStageFlagBits::eFragment;

    bindings[2].binding         = 2;
    bindings[2].descriptorType  = vk::DescriptorType::eUniformBuffer;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags      = vk::ShaderStageFlagBits::eFragment;

    vk::DescriptorSetLayoutCreateInfo layoutCI{};
    layoutCI.bindingCount = 3;
    layoutCI.pBindings    = bindings;
    m_setLayout = vk::raii::DescriptorSetLayout(device, layoutCI);

    // ── Descriptor pool ──────────────────────────────────────────────────────
    vk::DescriptorPoolSize poolSizes[2]{};
    poolSizes[0].type            = vk::DescriptorType::eCombinedImageSampler;
    poolSizes[0].descriptorCount = 2;
    poolSizes[1].type            = vk::DescriptorType::eUniformBuffer;
    poolSizes[1].descriptorCount = 1;

    vk::DescriptorPoolCreateInfo poolCI{};
    poolCI.flags         = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    poolCI.maxSets       = 1;
    poolCI.poolSizeCount = 2;
    poolCI.pPoolSizes    = poolSizes;
    m_descriptorPool = vk::raii::DescriptorPool(device, poolCI);

    vk::DescriptorSetLayout dsl = *m_setLayout;
    vk::DescriptorSetAllocateInfo allocInfo{};
    allocInfo.descriptorPool     = *m_descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts        = &dsl;
    auto sets    = device.allocateDescriptorSets(allocInfo);
    m_descriptorSet = std::move(sets[0]);

    // ── UBO buffer ───────────────────────────────────────────────────────────
    m_uboBuffer.Create(sizeof(ParamsUBO),
                       vk::BufferUsageFlagBits::eUniformBuffer,
                       vk::MemoryPropertyFlagBits::eHostVisible |
                       vk::MemoryPropertyFlagBits::eHostCoherent);

    // Write UBO descriptor once (pointer doesn't change)
    {
        vk::DescriptorBufferInfo bufInfo{};
        bufInfo.buffer = m_uboBuffer.GetBuffer();
        bufInfo.offset = 0;
        bufInfo.range  = sizeof(ParamsUBO);

        vk::WriteDescriptorSet w{};
        w.dstSet          = *m_descriptorSet;
        w.dstBinding      = 2;
        w.descriptorCount = 1;
        w.descriptorType  = vk::DescriptorType::eUniformBuffer;
        w.pBufferInfo     = &bufInfo;
        device.updateDescriptorSets(w, nullptr);
    }

    // ── Output framebuffer ───────────────────────────────────────────────────
    m_outputFb.Create(w, h, colorFormat, VulkanRenderer::Get().GetDepthFormat());

    // ── Pipeline ─────────────────────────────────────────────────────────────
    BuildPipeline(fragShaderPath);
}

void VulkanPostProcessPass::Destroy()
{
    m_outputFb.Destroy();
    m_uboBuffer.Destroy();
    m_pipeline       = nullptr;
    m_pipelineLayout = nullptr;
    m_descriptorSet  = nullptr;
    m_descriptorPool = nullptr;
    m_setLayout      = nullptr;
    m_lastSrcView    = nullptr;
    m_lastSrcSmp     = nullptr;
    m_lastDepthView  = nullptr;
    m_lastDepthSmp   = nullptr;
}

void VulkanPostProcessPass::Resize(uint32_t w, uint32_t h)
{
    m_outputFb.Recreate(w, h);
    // Force descriptor update on next Draw()
    m_lastSrcView   = nullptr;
    m_lastSrcSmp    = nullptr;
    m_lastDepthView = nullptr;
    m_lastDepthSmp  = nullptr;
}

void VulkanPostProcessPass::Rebuild(const std::string& fragShaderPath)
{
    m_fragShaderPath = fragShaderPath;
    m_pipeline       = nullptr;
    m_pipelineLayout = nullptr;
    BuildPipeline(fragShaderPath);
}

// ─────────────────────────────────────────────────────────────────────────────
// Pipeline
// ─────────────────────────────────────────────────────────────────────────────
void VulkanPostProcessPass::BuildPipeline(const std::string& fragShaderPath)
{
    auto& device = VulkanInstance::Get().GetDevice();

    auto mkModule = [&](const std::vector<uint32_t>& code)
    {
        vk::ShaderModuleCreateInfo ci{};
        ci.codeSize = code.size() * sizeof(uint32_t);
        ci.pCode    = code.data();
        return vk::raii::ShaderModule(device, ci);
    };

    auto vertMod = mkModule(LoadSpv("shaders/fog.vert.spv"));
    auto fragMod = mkModule(LoadSpv(fragShaderPath));

    vk::PipelineShaderStageCreateInfo stages[2]{};
    stages[0].stage  = vk::ShaderStageFlagBits::eVertex;
    stages[0].module = *vertMod;
    stages[0].pName  = "main";
    stages[1].stage  = vk::ShaderStageFlagBits::eFragment;
    stages[1].module = *fragMod;
    stages[1].pName  = "main";

    vk::PipelineVertexInputStateCreateInfo   vertexInput{};
    vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;

    vk::PipelineViewportStateCreateInfo viewportState{};
    viewportState.viewportCount = 1;
    viewportState.scissorCount  = 1;

    vk::PipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.polygonMode = vk::PolygonMode::eFill;
    rasterizer.cullMode    = vk::CullModeFlagBits::eNone;
    rasterizer.lineWidth   = 1.0f;

    vk::PipelineMultisampleStateCreateInfo multisampling{};
    multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

    vk::PipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.depthTestEnable  = vk::False;
    depthStencil.depthWriteEnable = vk::False;

    vk::PipelineColorBlendAttachmentState colorAtt{};
    colorAtt.blendEnable    = vk::False;
    colorAtt.colorWriteMask = vk::ColorComponentFlagBits::eR |
                              vk::ColorComponentFlagBits::eG |
                              vk::ColorComponentFlagBits::eB |
                              vk::ColorComponentFlagBits::eA;

    vk::PipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments    = &colorAtt;

    std::array<vk::DynamicState, 2> dynStates = {
        vk::DynamicState::eViewport, vk::DynamicState::eScissor };
    vk::PipelineDynamicStateCreateInfo dynState{};
    dynState.dynamicStateCount = static_cast<uint32_t>(dynStates.size());
    dynState.pDynamicStates    = dynStates.data();

    vk::DescriptorSetLayout dsl = *m_setLayout;
    vk::PipelineLayoutCreateInfo pipeLayoutCI{};
    pipeLayoutCI.setLayoutCount = 1;
    pipeLayoutCI.pSetLayouts    = &dsl;
    m_pipelineLayout = vk::raii::PipelineLayout(device, pipeLayoutCI);

    vk::PipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.colorAttachmentCount    = 1;
    renderingInfo.pColorAttachmentFormats = &m_fmt;

    vk::GraphicsPipelineCreateInfo pipelineCI{};
    pipelineCI.pNext               = &renderingInfo;
    pipelineCI.stageCount          = 2;
    pipelineCI.pStages             = stages;
    pipelineCI.pVertexInputState   = &vertexInput;
    pipelineCI.pInputAssemblyState = &inputAssembly;
    pipelineCI.pViewportState      = &viewportState;
    pipelineCI.pRasterizationState = &rasterizer;
    pipelineCI.pMultisampleState   = &multisampling;
    pipelineCI.pDepthStencilState  = &depthStencil;
    pipelineCI.pColorBlendState    = &colorBlend;
    pipelineCI.pDynamicState       = &dynState;
    pipelineCI.layout              = *m_pipelineLayout;

    m_pipeline = vk::raii::Pipeline(device, nullptr, pipelineCI);
}

// ─────────────────────────────────────────────────────────────────────────────
// Descriptor update (lazy, only when views change)
// ─────────────────────────────────────────────────────────────────────────────
void VulkanPostProcessPass::UpdateDescriptor(vk::ImageView srcView,   vk::Sampler srcSampler,
                                              vk::ImageView depthView, vk::Sampler depthSampler)
{
    if (srcView   == m_lastSrcView && srcSampler == m_lastSrcSmp &&
        depthView == m_lastDepthView && depthSampler == m_lastDepthSmp) return;

    m_lastSrcView   = srcView;
    m_lastSrcSmp    = srcSampler;
    m_lastDepthView = depthView;
    m_lastDepthSmp  = depthSampler;

    auto& device = VulkanInstance::Get().GetDevice();

    vk::DescriptorImageInfo srcInfo{};
    srcInfo.sampler     = srcSampler;
    srcInfo.imageView   = srcView;
    srcInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

    vk::DescriptorImageInfo depthInfo{};
    depthInfo.sampler     = depthSampler;
    depthInfo.imageView   = depthView;
    depthInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

    vk::WriteDescriptorSet writes[2]{};
    writes[0].dstSet          = *m_descriptorSet;
    writes[0].dstBinding      = 0;
    writes[0].descriptorType  = vk::DescriptorType::eCombinedImageSampler;
    writes[0].descriptorCount = 1;
    writes[0].pImageInfo      = &srcInfo;

    writes[1].dstSet          = *m_descriptorSet;
    writes[1].dstBinding      = 1;
    writes[1].descriptorType  = vk::DescriptorType::eCombinedImageSampler;
    writes[1].descriptorCount = 1;
    writes[1].pImageInfo      = &depthInfo;

    device.updateDescriptorSets(writes, {});
}

// ─────────────────────────────────────────────────────────────────────────────
// Draw
// ─────────────────────────────────────────────────────────────────────────────
void VulkanPostProcessPass::Draw(vk::CommandBuffer cmd,
                                  vk::ImageView     srcView,
                                  vk::Sampler       srcSampler,
                                  vk::ImageView     depthView,
                                  vk::Sampler       depthSampler,
                                  const float       params[16])
{
    // Upload params UBO
    ParamsUBO ubo{};
    std::memcpy(ubo.data, params, sizeof(float) * 16);
    m_uboBuffer.Upload(&ubo, sizeof(ubo));

    UpdateDescriptor(srcView, srcSampler, depthView, depthSampler);

    m_outputFb.BeginRendering(cmd);

    vk::Extent2D ext = m_outputFb.GetExtent();

    vk::Viewport vp{};
    vp.width    = static_cast<float>(ext.width);
    vp.height   = static_cast<float>(ext.height);
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    cmd.setViewport(0, vp);

    vk::Rect2D scissor{};
    scissor.extent = ext;
    cmd.setScissor(0, scissor);

    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_pipeline);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                           *m_pipelineLayout, 0, *m_descriptorSet, {});
    cmd.draw(3, 1, 0, 0);

    m_outputFb.EndRendering(cmd);
}

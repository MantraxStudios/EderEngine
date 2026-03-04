#include "VulkanVolumetricFog.h"
#include "Renderer/Vulkan/VulkanInstance.h"
#include "Renderer/VulkanRenderer.h"
#include <IO/AssetManager.h>
#include <fstream>
#include <stdexcept>
#include <cstring>

std::vector<uint32_t> VulkanVolumetricFog::LoadSpv(const std::string& path)
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
        throw std::runtime_error("VulkanVolumetricFog: cannot open shader: " + path);
    size_t sz = static_cast<size_t>(f.tellg());
    std::vector<uint32_t> buf(sz / sizeof(uint32_t));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(buf.data()), sz);
    return buf;
}

// ─────────────────────────────────────────────────────────────────────────────
// Create / Destroy / Resize
// ─────────────────────────────────────────────────────────────────────────────

void VulkanVolumetricFog::Create(vk::Format colorFormat, uint32_t w, uint32_t h,
                                  vk::DescriptorSetLayout lightDSL)
{
    fmt = colorFormat;
    auto& device = VulkanInstance::Get().GetDevice();

    // ── Descriptor set layout ──────────────────────────────────────────────
    // binding 0 : scene colour (CIS)
    // binding 1 : depth        (CIS)
    // binding 2 : FogUBO       (UBO)
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
    setLayout = vk::raii::DescriptorSetLayout(device, layoutCI);

    // ── Descriptor pool ────────────────────────────────────────────────────
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
    descriptorPool = vk::raii::DescriptorPool(device, poolCI);

    // ── Allocate descriptor set ────────────────────────────────────────────
    vk::DescriptorSetLayout dsl = *setLayout;
    vk::DescriptorSetAllocateInfo allocInfo{};
    allocInfo.descriptorPool     = *descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts        = &dsl;
    auto sets     = device.allocateDescriptorSets(allocInfo);
    descriptorSet = std::move(sets[0]);

    // ── UBO buffer ─────────────────────────────────────────────────────────
    uboBuffer.Create(sizeof(FogUBO),
                     vk::BufferUsageFlagBits::eUniformBuffer,
                     vk::MemoryPropertyFlagBits::eHostVisible |
                     vk::MemoryPropertyFlagBits::eHostCoherent);

    // Write UBO descriptor once (pointer never changes)
    {
        vk::DescriptorBufferInfo bufInfo{};
        bufInfo.buffer = uboBuffer.GetBuffer();
        bufInfo.offset = 0;
        bufInfo.range  = sizeof(FogUBO);

        vk::WriteDescriptorSet w{};
        w.dstSet          = *descriptorSet;
        w.dstBinding      = 2;
        w.descriptorCount = 1;
        w.descriptorType  = vk::DescriptorType::eUniformBuffer;
        w.pBufferInfo     = &bufInfo;
        device.updateDescriptorSets(w, nullptr);
    }

    BuildPipeline(colorFormat, lightDSL);
    outputFb.Create(w, h, colorFormat, VulkanRenderer::Get().GetDepthFormat());
}

void VulkanVolumetricFog::BuildPipeline(vk::Format colorFormat,
                                         vk::DescriptorSetLayout lightDSL)
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
    auto fragMod = mkModule(LoadSpv("shaders/fog.frag.spv"));

    vk::PipelineShaderStageCreateInfo stages[2]{};
    stages[0].stage  = vk::ShaderStageFlagBits::eVertex;
    stages[0].module = *vertMod;
    stages[0].pName  = "main";
    stages[1].stage  = vk::ShaderStageFlagBits::eFragment;
    stages[1].module = *fragMod;
    stages[1].pName  = "main";

    vk::PipelineVertexInputStateCreateInfo     vertexInput{};
    vk::PipelineInputAssemblyStateCreateInfo   inputAssembly{};
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
    dynState.dynamicStateCount = (uint32_t)dynStates.size();
    dynState.pDynamicStates    = dynStates.data();

    vk::DescriptorSetLayout dsls[2] = { *setLayout, lightDSL };
    vk::PipelineLayoutCreateInfo pipeLayoutCI{};
    pipeLayoutCI.setLayoutCount = 2;
    pipeLayoutCI.pSetLayouts    = dsls;
    pipelineLayout = vk::raii::PipelineLayout(device, pipeLayoutCI);

    vk::PipelineRenderingCreateInfo dynRendering{};
    dynRendering.colorAttachmentCount    = 1;
    dynRendering.pColorAttachmentFormats = &colorFormat;

    vk::GraphicsPipelineCreateInfo pipeCI{};
    pipeCI.pNext               = &dynRendering;
    pipeCI.stageCount          = 2;
    pipeCI.pStages             = stages;
    pipeCI.pVertexInputState   = &vertexInput;
    pipeCI.pInputAssemblyState = &inputAssembly;
    pipeCI.pViewportState      = &viewportState;
    pipeCI.pRasterizationState = &rasterizer;
    pipeCI.pMultisampleState   = &multisampling;
    pipeCI.pDepthStencilState  = &depthStencil;
    pipeCI.pColorBlendState    = &colorBlend;
    pipeCI.pDynamicState       = &dynState;
    pipeCI.layout              = *pipelineLayout;

    pipeline = vk::raii::Pipeline(device, nullptr, pipeCI);
}

void VulkanVolumetricFog::UpdateDescriptor(vk::ImageView sceneView, vk::Sampler sceneSampler,
                                            vk::ImageView depthView,  vk::Sampler depthSampler)
{
    bool needsUpdate = (sceneView != lastSceneView || sceneSampler != lastSceneSmp ||
                        depthView != lastDepthView  || depthSampler != lastDepthSmp);
    if (!needsUpdate) return;

    lastSceneView = sceneView; lastSceneSmp = sceneSampler;
    lastDepthView = depthView; lastDepthSmp = depthSampler;

    vk::DescriptorImageInfo imgInfos[2]{};
    imgInfos[0].imageView   = sceneView;
    imgInfos[0].sampler     = sceneSampler;
    imgInfos[0].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

    imgInfos[1].imageView   = depthView;
    imgInfos[1].sampler     = depthSampler;
    imgInfos[1].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

    vk::WriteDescriptorSet writes[2]{};
    for (int i = 0; i < 2; i++)
    {
        writes[i].dstSet          = *descriptorSet;
        writes[i].dstBinding      = static_cast<uint32_t>(i);
        writes[i].descriptorCount = 1;
        writes[i].descriptorType  = vk::DescriptorType::eCombinedImageSampler;
        writes[i].pImageInfo      = &imgInfos[i];
    }
    VulkanInstance::Get().GetDevice().updateDescriptorSets(writes, nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// Draw
// ─────────────────────────────────────────────────────────────────────────────

void VulkanVolumetricFog::Draw(
    vk::CommandBuffer   cmd,
    vk::ImageView       sceneView,
    vk::Sampler         sceneSampler,
    vk::ImageView       depthView,
    vk::Sampler         depthSampler,
    const glm::mat4&    invViewProj,
    const glm::vec3&    cameraPos,
    const glm::vec3&    fogColor,
    float               density,
    const glm::vec3&    horizonColor,
    float               heightFalloff,
    const glm::vec3&    sunScatterColor,
    float               scatterStrength,
    const glm::vec3&    lightDir,
    float               sunIntensity,
    float               heightOffset,
    float               maxFogAmount,
    float               fogStart,
    float               fogEnd,
    vk::DescriptorSet   lightDS)
{
    // ── Upload UBO ───────────────────────────────────────────────────────────
    FogUBO ubo{};
    ubo.invViewProj    = invViewProj;
    ubo.camPos         = glm::vec4(cameraPos, 0.0f);
    ubo.fogColor       = glm::vec4(fogColor,         density);
    ubo.horizonColor   = glm::vec4(horizonColor,     heightFalloff);
    ubo.sunScatterColor= glm::vec4(sunScatterColor,  scatterStrength);
    ubo.lightDir       = glm::vec4(lightDir,         sunIntensity);
    ubo.params         = glm::vec4(heightOffset, maxFogAmount, fogStart, fogEnd);
    uboBuffer.Upload(&ubo, sizeof(ubo));

    UpdateDescriptor(sceneView, sceneSampler, depthView, depthSampler);

    // ── Begin rendering into outputFb ────────────────────────────────────────
    outputFb.BeginRendering(cmd);

    vk::Extent2D ext = outputFb.GetExtent();
    vk::Viewport vp{};
    vp.width    = static_cast<float>(ext.width);
    vp.height   = static_cast<float>(ext.height);
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    cmd.setViewport(0, vp);
    cmd.setScissor(0, vk::Rect2D{ vk::Offset2D{}, ext });

    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);
    vk::DescriptorSet ds = *descriptorSet;
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipelineLayout, 0, ds, nullptr);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipelineLayout, 1, lightDS, nullptr);
    cmd.draw(3, 1, 0, 0);

    outputFb.EndRendering(cmd);
}

// ─────────────────────────────────────────────────────────────────────────────
// Resize / Destroy
// ─────────────────────────────────────────────────────────────────────────────

void VulkanVolumetricFog::Resize(uint32_t w, uint32_t h)
{
    VulkanInstance::Get().GetDevice().waitIdle();
    outputFb.Recreate(w, h);
    lastSceneView = nullptr;
    lastDepthView = nullptr;
}

void VulkanVolumetricFog::Destroy()
{
    VulkanInstance::Get().GetDevice().waitIdle();
    outputFb.Destroy();
    uboBuffer.Destroy();
    pipeline       = nullptr;
    pipelineLayout = nullptr;
    descriptorSet  = nullptr;
    descriptorPool = nullptr;
    setLayout      = nullptr;
}

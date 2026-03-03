#include "VulkanVolumetricLight.h"
#include "Renderer/Vulkan/VulkanInstance.h"
#include "Renderer/VulkanRenderer.h"
#include <fstream>
#include <stdexcept>

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

std::vector<uint32_t> VulkanVolumetricLight::LoadSpv(const std::string& path)
{
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open())
        throw std::runtime_error("VulkanVolumetricLight: cannot open shader: " + path);
    size_t sz = static_cast<size_t>(f.tellg());
    std::vector<uint32_t> buf(sz / sizeof(uint32_t));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(buf.data()), sz);
    return buf;
}

// ─────────────────────────────────────────────────────────────────────────────
// Create / Destroy / Resize
// ─────────────────────────────────────────────────────────────────────────────

void VulkanVolumetricLight::Create(vk::Format colorFormat, uint32_t w, uint32_t h,
                                    vk::DescriptorSetLayout lightDSL)
{
    fmt = colorFormat;
    auto& device = VulkanInstance::Get().GetDevice();

    // ── Descriptor set layout ──────────────────────────────────────────────
    // binding 0 : scene colour  (combined image sampler)
    // binding 1 : depth         (combined image sampler)
    // binding 2 : shadow map    (combined image sampler – 4-layer 2DArray)
    // binding 3 : VolumetricUBO (uniform buffer)
    vk::DescriptorSetLayoutBinding bindings[4]{};

    bindings[0].binding         = 0;
    bindings[0].descriptorType  = vk::DescriptorType::eCombinedImageSampler;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags      = vk::ShaderStageFlagBits::eFragment;

    bindings[1].binding         = 1;
    bindings[1].descriptorType  = vk::DescriptorType::eCombinedImageSampler;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags      = vk::ShaderStageFlagBits::eFragment;

    bindings[2].binding         = 2;
    bindings[2].descriptorType  = vk::DescriptorType::eCombinedImageSampler;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags      = vk::ShaderStageFlagBits::eFragment;

    bindings[3].binding         = 3;
    bindings[3].descriptorType  = vk::DescriptorType::eUniformBuffer;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags      = vk::ShaderStageFlagBits::eFragment;

    vk::DescriptorSetLayoutCreateInfo layoutCI{};
    layoutCI.bindingCount = 4;
    layoutCI.pBindings    = bindings;
    setLayout = vk::raii::DescriptorSetLayout(device, layoutCI);

    // ── Descriptor pool ────────────────────────────────────────────────────
    vk::DescriptorPoolSize poolSizes[2]{};
    poolSizes[0].type            = vk::DescriptorType::eCombinedImageSampler;
    poolSizes[0].descriptorCount = 3;
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

    // ── UBO buffer (host-visible, mapped each Draw) ────────────────────────
    uboBuffer.Create(sizeof(VolumetricUBO),
                     vk::BufferUsageFlagBits::eUniformBuffer,
                     vk::MemoryPropertyFlagBits::eHostVisible |
                     vk::MemoryPropertyFlagBits::eHostCoherent);

    // Write UBO descriptor once (pointer never changes)
    {
        vk::DescriptorBufferInfo bufInfo{};
        bufInfo.buffer = uboBuffer.GetBuffer();
        bufInfo.offset = 0;
        bufInfo.range  = sizeof(VolumetricUBO);

        vk::WriteDescriptorSet w{};
        w.dstSet          = *descriptorSet;
        w.dstBinding      = 3;
        w.descriptorCount = 1;
        w.descriptorType  = vk::DescriptorType::eUniformBuffer;
        w.pBufferInfo     = &bufInfo;
        device.updateDescriptorSets(w, nullptr);
    }

    BuildPipeline(colorFormat, lightDSL);

    // Half-resolution: 4× fewer fragments = 4× less ray-march cost.
    // Downstream passes (sun shafts, editor) bilinearly upsample it transparently.
    outputFb.Create(w, h, colorFormat, VulkanRenderer::Get().GetDepthFormat());
}

void VulkanVolumetricLight::BuildPipeline(vk::Format colorFormat,
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

    auto vertMod = mkModule(LoadSpv("shaders/volumetric.vert.spv"));
    auto fragMod = mkModule(LoadSpv("shaders/volumetric.frag.spv"));

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

    vk::PipelineViewportStateCreateInfo        viewportState{};
    viewportState.viewportCount = 1;
    viewportState.scissorCount  = 1;

    vk::PipelineRasterizationStateCreateInfo   rasterizer{};
    rasterizer.polygonMode = vk::PolygonMode::eFill;
    rasterizer.cullMode    = vk::CullModeFlagBits::eNone;
    rasterizer.lineWidth   = 1.0f;

    vk::PipelineMultisampleStateCreateInfo     multisampling{};
    multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

    vk::PipelineDepthStencilStateCreateInfo    depthStencil{};
    depthStencil.depthTestEnable  = vk::False;
    depthStencil.depthWriteEnable = vk::False;

    // Additive blend — scatter light added on top of scene colour
    vk::PipelineColorBlendAttachmentState colorAtt{};
    colorAtt.blendEnable         = vk::False;   // Shader already composites final colour
    colorAtt.colorWriteMask      = vk::ColorComponentFlagBits::eR |
                                   vk::ColorComponentFlagBits::eG |
                                   vk::ColorComponentFlagBits::eB |
                                   vk::ColorComponentFlagBits::eA;

    vk::PipelineColorBlendStateCreateInfo      colorBlend{};
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

void VulkanVolumetricLight::UpdateDescriptor(vk::ImageView sceneView,
                                              vk::Sampler   sceneSampler,
                                              vk::ImageView depthView,
                                              vk::Sampler   depthSampler,
                                              vk::ImageView shadowMapView,
                                              vk::Sampler   shadowMapSampler)
{
    bool needsUpdate = (sceneView    != lastSceneView  || sceneSampler    != lastSceneSmp  ||
                        depthView    != lastDepthView  || depthSampler    != lastDepthSmp  ||
                        shadowMapView != lastShadowView || shadowMapSampler != lastShadowSmp);
    if (!needsUpdate) return;

    lastSceneView  = sceneView;    lastSceneSmp  = sceneSampler;
    lastDepthView  = depthView;    lastDepthSmp  = depthSampler;
    lastShadowView = shadowMapView; lastShadowSmp = shadowMapSampler;

    vk::DescriptorImageInfo imgInfos[3]{};
    imgInfos[0].imageView   = sceneView;
    imgInfos[0].sampler     = sceneSampler;
    imgInfos[0].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

    imgInfos[1].imageView   = depthView;
    imgInfos[1].sampler     = depthSampler;
    imgInfos[1].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

    imgInfos[2].imageView   = shadowMapView;
    imgInfos[2].sampler     = shadowMapSampler;
    imgInfos[2].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

    vk::WriteDescriptorSet writes[3]{};
    for (int i = 0; i < 3; i++)
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

void VulkanVolumetricLight::Draw(
    vk::CommandBuffer   cmd,
    vk::ImageView       sceneView,
    vk::Sampler         sceneSampler,
    vk::ImageView       depthView,
    vk::Sampler         depthSampler,
    vk::ImageView       shadowMapView,
    vk::Sampler         shadowMapSampler,
    const glm::mat4&    invViewProj,
    const glm::mat4     shadowMatrices[4],
    const glm::vec4&    cascadeSplits,
    const glm::vec3&    lightDir,
    const glm::vec3&    lightColor,
    float               lightIntensity,
    const glm::vec3&    cameraPos,
    vk::DescriptorSet   lightDS,
    int                 numSteps,
    float               density,
    float               absorption,
    float               g,
    float               intensity,
    float               maxDistance,
    float               jitter,
    const glm::vec3&    tint)
{
    // ── Upload UBO ───────────────────────────────────────────────────────────
    VolumetricUBO ubo{};
    ubo.invViewProj   = invViewProj;
    for (int i = 0; i < 4; i++) ubo.shadowMatrix[i] = shadowMatrices[i];
    ubo.cascadeSplits = cascadeSplits;
    ubo.lightDir      = glm::vec4(lightDir, 0.0f);
    ubo.lightColor    = glm::vec4(lightColor, lightIntensity);
    ubo.camPosMaxDist = glm::vec4(cameraPos, maxDistance);
    ubo.params        = glm::vec4(density, absorption, g, jitter);
    ubo.iparams       = glm::ivec4(numSteps, 0, 0, 0);
    ubo.tint          = glm::vec4(tint, intensity);
    uboBuffer.Upload(&ubo, sizeof(ubo));

    // ── Update descriptors (only when views change) ──────────────────────────
    UpdateDescriptor(sceneView, sceneSampler,
                     depthView, depthSampler,
                     shadowMapView, shadowMapSampler);

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
    // set 0 — our own (scene, depth, cascade shadow map, ubo)
    vk::DescriptorSet ds = *descriptorSet;
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipelineLayout, 0, ds, nullptr);
    // set 1 — LightBuffer (LightUBO + spot shadow map + point shadow map)
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipelineLayout, 1, lightDS, nullptr);

    cmd.draw(3, 1, 0, 0);

    outputFb.EndRendering(cmd);
}

// ─────────────────────────────────────────────────────────────────────────────
// Resize / Destroy
// ─────────────────────────────────────────────────────────────────────────────

void VulkanVolumetricLight::Resize(uint32_t w, uint32_t h)
{
    VulkanInstance::Get().GetDevice().waitIdle();
    outputFb.Recreate(w, h);

    // Force descriptor re-write on next Draw (views may have changed after resize)
    lastSceneView  = nullptr;
    lastDepthView  = nullptr;
    lastShadowView = nullptr;
}

void VulkanVolumetricLight::Destroy()
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

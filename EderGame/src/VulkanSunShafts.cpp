#include "VulkanSunShafts.h"
#include "Renderer/Vulkan/VulkanInstance.h"
#include "Renderer/VulkanRenderer.h"
#include <fstream>
#include <stdexcept>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

std::vector<uint32_t> VulkanSunShafts::LoadSpv(const std::string& path)
{
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open())
        throw std::runtime_error("VulkanSunShafts: cannot open shader: " + path);
    size_t sz = static_cast<size_t>(f.tellg());
    std::vector<uint32_t> buf(sz / sizeof(uint32_t));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(buf.data()), sz);
    return buf;
}

// ---------------------------------------------------------------------------
// Create / Destroy
// ---------------------------------------------------------------------------

void VulkanSunShafts::Create(vk::Format colorFormat, uint32_t w, uint32_t h)
{
    fmt = colorFormat;
    auto& device = VulkanInstance::Get().GetDevice();

    // Descriptor set layout: binding 0 = scene colour, binding 1 = scene depth
    vk::DescriptorSetLayoutBinding bindings[2]{};
    bindings[0].binding         = 0;
    bindings[0].descriptorType  = vk::DescriptorType::eCombinedImageSampler;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags      = vk::ShaderStageFlagBits::eFragment;
    bindings[1].binding         = 1;
    bindings[1].descriptorType  = vk::DescriptorType::eCombinedImageSampler;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags      = vk::ShaderStageFlagBits::eFragment;

    vk::DescriptorSetLayoutCreateInfo layoutCI{};
    layoutCI.bindingCount = 2;
    layoutCI.pBindings    = bindings;
    setLayout = vk::raii::DescriptorSetLayout(device, layoutCI);

    // Descriptor pool (max 1 set with 2 combined image samplers)
    vk::DescriptorPoolSize poolSize{};
    poolSize.type            = vk::DescriptorType::eCombinedImageSampler;
    poolSize.descriptorCount = 2;

    vk::DescriptorPoolCreateInfo poolCI{};
    poolCI.flags         = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    poolCI.maxSets       = 1;
    poolCI.poolSizeCount = 1;
    poolCI.pPoolSizes    = &poolSize;
    descriptorPool = vk::raii::DescriptorPool(device, poolCI);

    // Allocate descriptor set
    vk::DescriptorSetLayout dsl = *setLayout;
    vk::DescriptorSetAllocateInfo allocInfo{};
    allocInfo.descriptorPool     = *descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts        = &dsl;
    auto sets     = device.allocateDescriptorSets(allocInfo);
    descriptorSet = std::move(sets[0]);

    BuildPipeline(colorFormat);

    outputFb.Create(w, h, colorFormat, VulkanRenderer::Get().GetDepthFormat());
}

void VulkanSunShafts::BuildPipeline(vk::Format colorFormat)
{
    auto& device = VulkanInstance::Get().GetDevice();

    auto mkModule = [&](const std::vector<uint32_t>& code)
    {
        vk::ShaderModuleCreateInfo ci{};
        ci.codeSize = code.size() * sizeof(uint32_t);
        ci.pCode    = code.data();
        return vk::raii::ShaderModule(device, ci);
    };

    auto vertMod = mkModule(LoadSpv("shaders/sunshafts.vert.spv"));
    auto fragMod = mkModule(LoadSpv("shaders/sunshafts.frag.spv"));

    vk::PipelineShaderStageCreateInfo stages[2]{};
    stages[0].stage  = vk::ShaderStageFlagBits::eVertex;
    stages[0].module = *vertMod;
    stages[0].pName  = "main";
    stages[1].stage  = vk::ShaderStageFlagBits::eFragment;
    stages[1].module = *fragMod;
    stages[1].pName  = "main";

    // No vertex input — full-screen tri generated in vertex shader
    vk::PipelineVertexInputStateCreateInfo vertexInput{};

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

    // No depth test — pure post-process
    vk::PipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.depthTestEnable  = vk::False;
    depthStencil.depthWriteEnable = vk::False;

    // Opaque output (scene already composited inside shader)
    vk::PipelineColorBlendAttachmentState colorAtt{};
    colorAtt.blendEnable    = vk::False;
    colorAtt.colorWriteMask =
        vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
        vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;

    vk::PipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments    = &colorAtt;

    std::array<vk::DynamicState, 2> dynStates = {
        vk::DynamicState::eViewport, vk::DynamicState::eScissor };
    vk::PipelineDynamicStateCreateInfo dynState{};
    dynState.dynamicStateCount = (uint32_t)dynStates.size();
    dynState.pDynamicStates    = dynStates.data();

    // Push constant: PushData = 2+4+4+4+4+12+4 = 34... align to 48
    vk::PushConstantRange pcRange{};
    pcRange.stageFlags = vk::ShaderStageFlagBits::eFragment;
    pcRange.offset     = 0;
    pcRange.size       = sizeof(PushData);

    vk::DescriptorSetLayout dsl = *setLayout;
    vk::PipelineLayoutCreateInfo layoutCI{};
    layoutCI.setLayoutCount         = 1;
    layoutCI.pSetLayouts            = &dsl;
    layoutCI.pushConstantRangeCount = 1;
    layoutCI.pPushConstantRanges    = &pcRange;
    pipelineLayout = vk::raii::PipelineLayout(device, layoutCI);

    vk::PipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.colorAttachmentCount    = 1;
    renderingInfo.pColorAttachmentFormats = &colorFormat;

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
    pipelineCI.layout              = *pipelineLayout;

    pipeline = vk::raii::Pipeline(device, nullptr, pipelineCI);
}

void VulkanSunShafts::UpdateDescriptor(vk::ImageView sceneView, vk::Sampler sampler, vk::ImageView depthView)
{
    if (sceneView == lastView && sampler == lastSampler && depthView == lastDepthView) return;
    lastView      = sceneView;
    lastSampler   = sampler;
    lastDepthView = depthView;

    vk::DescriptorImageInfo colorInfo{};
    colorInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    colorInfo.imageView   = sceneView;
    colorInfo.sampler     = sampler;

    vk::DescriptorImageInfo depthInfo{};
    depthInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    depthInfo.imageView   = depthView;
    depthInfo.sampler     = sampler;

    vk::WriteDescriptorSet writes[2]{};
    writes[0].dstSet          = *descriptorSet;
    writes[0].dstBinding      = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType  = vk::DescriptorType::eCombinedImageSampler;
    writes[0].pImageInfo      = &colorInfo;

    writes[1].dstSet          = *descriptorSet;
    writes[1].dstBinding      = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType  = vk::DescriptorType::eCombinedImageSampler;
    writes[1].pImageInfo      = &depthInfo;

    VulkanInstance::Get().GetDevice().updateDescriptorSets(writes, nullptr);
}

void VulkanSunShafts::Resize(uint32_t w, uint32_t h)
{
    if (w == outputFb.GetExtent().width && h == outputFb.GetExtent().height) return;
    VulkanInstance::Get().GetDevice().waitIdle();
    outputFb.Recreate(w, h);
    lastView      = nullptr;   // force descriptor update
    lastSampler   = nullptr;
    lastDepthView = nullptr;
}

void VulkanSunShafts::Destroy()
{
    outputFb.Destroy();
    pipeline       = nullptr;
    pipelineLayout = nullptr;
    descriptorSet  = nullptr;
    descriptorPool = nullptr;
    setLayout      = nullptr;
}

// ---------------------------------------------------------------------------
// Draw
// ---------------------------------------------------------------------------

void VulkanSunShafts::Draw(vk::CommandBuffer cmd,
                            vk::ImageView    sceneView,
                            vk::Sampler      sceneSampler,
                            vk::ImageView    depthView,
                            glm::vec2        sunUV,
                            float intensity, float decay,
                            float weight,    float exposure,
                            const glm::vec3& tint,
                            float            sunHeight)
{
    UpdateDescriptor(sceneView, sceneSampler, depthView);

    outputFb.BeginRendering(cmd);

    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipelineLayout,
                           0, *descriptorSet, nullptr);

    vk::Extent2D ext = outputFb.GetExtent();
    vk::Viewport vp{};
    vp.width    =  static_cast<float>(ext.width);
    vp.height   =  static_cast<float>(ext.height);
    vp.minDepth =  0.0f;
    vp.maxDepth =  1.0f;
    cmd.setViewport(0, vp);
    cmd.setScissor(0, vk::Rect2D{{0,0}, ext});

    PushData push{};
    push.sunUV     = sunUV;
    push.decay     = decay;
    push.weight    = weight;
    push.exposure  = exposure;
    push.intensity = intensity;
    push.tint      = tint;
    push.sunHeight = sunHeight;
    cmd.pushConstants(*pipelineLayout,
        vk::ShaderStageFlagBits::eFragment,
        0, sizeof(PushData), &push);

    cmd.draw(3, 1, 0, 0);

    outputFb.EndRendering(cmd);
}

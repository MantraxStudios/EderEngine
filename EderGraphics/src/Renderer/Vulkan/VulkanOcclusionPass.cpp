#include "VulkanOcclusionPass.h"
#include "Renderer/Vulkan/VulkanInstance.h"
#include <fstream>
#include <stdexcept>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

std::vector<uint32_t> VulkanOcclusionPass::LoadSpv(const std::string& path)
{
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open())
        throw std::runtime_error("VulkanOcclusionPass: cannot open shader: " + path);
    size_t sz = static_cast<size_t>(f.tellg());
    std::vector<uint32_t> buf(sz / sizeof(uint32_t));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(buf.data()), sz);
    return buf;
}

uint32_t VulkanOcclusionPass::FindMemoryType(uint32_t filter, vk::MemoryPropertyFlags props)
{
    auto mem = VulkanInstance::Get().GetPhysicalDevice().getMemoryProperties();
    for (uint32_t i = 0; i < mem.memoryTypeCount; i++)
        if ((filter & (1 << i)) && (mem.memoryTypes[i].propertyFlags & props) == props)
            return i;
    throw std::runtime_error("VulkanOcclusionPass: no suitable memory type");
}

// ---------------------------------------------------------------------------
// Image management
// ---------------------------------------------------------------------------

void VulkanOcclusionPass::CreateImages(uint32_t w, uint32_t h)
{
    auto& device = VulkanInstance::Get().GetDevice();
    extent = vk::Extent2D{ w, h };

    // Single-channel R8Unorm — 0 = blocked geometry, 1 = sun disk / sky
    vk::ImageCreateInfo ci{};
    ci.imageType     = vk::ImageType::e2D;
    ci.format        = vk::Format::eR8Unorm;
    ci.extent        = vk::Extent3D{ w, h, 1 };
    ci.mipLevels     = 1;
    ci.arrayLayers   = 1;
    ci.samples       = vk::SampleCountFlagBits::e1;
    ci.tiling        = vk::ImageTiling::eOptimal;
    ci.initialLayout = vk::ImageLayout::eUndefined;
    ci.usage         = vk::ImageUsageFlagBits::eColorAttachment |
                       vk::ImageUsageFlagBits::eSampled;
    occImage = vk::raii::Image(device, ci);

    auto req = occImage.getMemoryRequirements();
    vk::MemoryAllocateInfo ai{ req.size,
        FindMemoryType(req.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal) };
    occMemory = vk::raii::DeviceMemory(device, ai);
    occImage.bindMemory(*occMemory, 0);

    vk::ImageViewCreateInfo vi{};
    vi.image            = *occImage;
    vi.viewType         = vk::ImageViewType::e2D;
    vi.format           = vk::Format::eR8Unorm;
    vi.subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 };
    occView = vk::raii::ImageView(device, vi);

    vk::SamplerCreateInfo si{};
    si.magFilter    = vk::Filter::eLinear;
    si.minFilter    = vk::Filter::eLinear;
    si.addressModeU = vk::SamplerAddressMode::eClampToEdge;
    si.addressModeV = vk::SamplerAddressMode::eClampToEdge;
    si.addressModeW = vk::SamplerAddressMode::eClampToEdge;
    occSampler = vk::raii::Sampler(device, si);

    occLayout = vk::ImageLayout::eUndefined;
}

void VulkanOcclusionPass::DestroyImages()
{
    occSampler = nullptr;
    occView    = nullptr;
    occImage   = nullptr;
    occMemory  = nullptr;
}

// ---------------------------------------------------------------------------
// Pipeline
// ---------------------------------------------------------------------------

void VulkanOcclusionPass::BuildPipeline()
{
    auto& device = VulkanInstance::Get().GetDevice();

    auto mkModule = [&](const std::vector<uint32_t>& code)
    {
        vk::ShaderModuleCreateInfo ci{};
        ci.codeSize = code.size() * sizeof(uint32_t);
        ci.pCode    = code.data();
        return vk::raii::ShaderModule(device, ci);
    };

    auto vertMod = mkModule(LoadSpv("shaders/occlusion.vert.spv"));
    auto fragMod = mkModule(LoadSpv("shaders/occlusion.frag.spv"));

    vk::PipelineShaderStageCreateInfo stages[2]{};
    stages[0].stage  = vk::ShaderStageFlagBits::eVertex;
    stages[0].module = *vertMod;
    stages[0].pName  = "main";
    stages[1].stage  = vk::ShaderStageFlagBits::eFragment;
    stages[1].module = *fragMod;
    stages[1].pName  = "main";

    // Descriptor set: binding 0 = depthTex
    vk::DescriptorSetLayoutBinding depthBinding{};
    depthBinding.binding         = 0;
    depthBinding.descriptorType  = vk::DescriptorType::eCombinedImageSampler;
    depthBinding.descriptorCount = 1;
    depthBinding.stageFlags      = vk::ShaderStageFlagBits::eFragment;

    vk::DescriptorSetLayoutCreateInfo dslCI{};
    dslCI.bindingCount = 1;
    dslCI.pBindings    = &depthBinding;
    setLayout = vk::raii::DescriptorSetLayout(device, dslCI);

    vk::DescriptorPoolSize poolSize{};
    poolSize.type            = vk::DescriptorType::eCombinedImageSampler;
    poolSize.descriptorCount = 1;

    vk::DescriptorPoolCreateInfo poolCI{};
    poolCI.flags         = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    poolCI.maxSets       = 1;
    poolCI.poolSizeCount = 1;
    poolCI.pPoolSizes    = &poolSize;
    descriptorPool = vk::raii::DescriptorPool(device, poolCI);

    vk::DescriptorSetLayout dsl = *setLayout;
    vk::DescriptorSetAllocateInfo allocCI{};
    allocCI.descriptorPool     = *descriptorPool;
    allocCI.descriptorSetCount = 1;
    allocCI.pSetLayouts        = &dsl;
    auto sets     = device.allocateDescriptorSets(allocCI);
    descriptorSet = std::move(sets[0]);

    // Push constants: vec2 sunUV + float sunRadius = 12 bytes
    vk::PushConstantRange pcRange{};
    pcRange.stageFlags = vk::ShaderStageFlagBits::eFragment;
    pcRange.offset     = 0;
    pcRange.size       = sizeof(PushData);

    vk::DescriptorSetLayout layouts[1] = { *setLayout };
    vk::PipelineLayoutCreateInfo plCI{};
    plCI.setLayoutCount         = 1;
    plCI.pSetLayouts            = layouts;
    plCI.pushConstantRangeCount = 1;
    plCI.pPushConstantRanges    = &pcRange;
    pipelineLayout = vk::raii::PipelineLayout(device, plCI);

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

    // Only write R channel; no depth test
    vk::PipelineColorBlendAttachmentState blendAtt{};
    blendAtt.colorWriteMask = vk::ColorComponentFlagBits::eR;

    vk::PipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments    = &blendAtt;

    vk::PipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.depthTestEnable  = vk::False;
    depthStencil.depthWriteEnable = vk::False;

    std::array<vk::DynamicState, 2> dynStates = {
        vk::DynamicState::eViewport, vk::DynamicState::eScissor };
    vk::PipelineDynamicStateCreateInfo dynState{};
    dynState.dynamicStateCount = 2;
    dynState.pDynamicStates    = dynStates.data();

    vk::Format attachFmt = vk::Format::eR8Unorm;
    vk::PipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.colorAttachmentCount    = 1;
    renderingInfo.pColorAttachmentFormats = &attachFmt;

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

// ---------------------------------------------------------------------------
// Descriptor update
// ---------------------------------------------------------------------------

void VulkanOcclusionPass::UpdateDescriptor(vk::ImageView depthView, vk::Sampler depthSampler)
{
    if (depthView == lastDepthView && depthSampler == lastDepthSmp) return;
    lastDepthView = depthView;
    lastDepthSmp  = depthSampler;

    vk::DescriptorImageInfo depthInfo{};
    depthInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    depthInfo.imageView   = depthView;
    depthInfo.sampler     = depthSampler;

    vk::WriteDescriptorSet write{};
    write.dstSet          = *descriptorSet;
    write.dstBinding      = 0;
    write.descriptorCount = 1;
    write.descriptorType  = vk::DescriptorType::eCombinedImageSampler;
    write.pImageInfo      = &depthInfo;

    VulkanInstance::Get().GetDevice().updateDescriptorSets(write, nullptr);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void VulkanOcclusionPass::Create(uint32_t w, uint32_t h)
{
    CreateImages(w, h);
    BuildPipeline();
}

void VulkanOcclusionPass::Destroy()
{
    pipeline       = nullptr;
    pipelineLayout = nullptr;
    descriptorSet  = nullptr;
    descriptorPool = nullptr;
    setLayout      = nullptr;
    DestroyImages();
}

void VulkanOcclusionPass::Resize(uint32_t w, uint32_t h)
{
    if (w == extent.width && h == extent.height) return;
    VulkanInstance::Get().GetDevice().waitIdle();
    DestroyImages();
    CreateImages(w, h);
    lastDepthView = nullptr;
    lastDepthSmp  = nullptr;
}

void VulkanOcclusionPass::Draw(vk::CommandBuffer cmd,
                                vk::ImageView     depthView,
                                vk::Sampler       depthSampler,
                                glm::vec2         sunUV,
                                float             sunRadius)
{
    UpdateDescriptor(depthView, depthSampler);

    // Transition occlusion image to ColorAttachmentOptimal
    vk::AccessFlags        srcAccess = occLayout == vk::ImageLayout::eShaderReadOnlyOptimal
                                       ? vk::AccessFlagBits::eShaderRead
                                       : vk::AccessFlagBits::eNone;
    vk::PipelineStageFlags srcStage  = occLayout == vk::ImageLayout::eShaderReadOnlyOptimal
                                       ? vk::PipelineStageFlagBits::eFragmentShader
                                       : vk::PipelineStageFlagBits::eTopOfPipe;

    vk::ImageMemoryBarrier barrier{};
    barrier.oldLayout           = occLayout;
    barrier.newLayout           = vk::ImageLayout::eColorAttachmentOptimal;
    barrier.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
    barrier.dstQueueFamilyIndex = vk::QueueFamilyIgnored;
    barrier.image               = *occImage;
    barrier.subresourceRange    = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 };
    barrier.srcAccessMask       = srcAccess;
    barrier.dstAccessMask       = vk::AccessFlagBits::eColorAttachmentWrite;
    cmd.pipelineBarrier(srcStage,
                        vk::PipelineStageFlagBits::eColorAttachmentOutput,
                        {}, {}, {}, barrier);
    occLayout = vk::ImageLayout::eColorAttachmentOptimal;

    // Begin dynamic rendering
    vk::RenderingAttachmentInfo colorAtt{};
    colorAtt.imageView   = *occView;
    colorAtt.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
    colorAtt.loadOp      = vk::AttachmentLoadOp::eClear;
    colorAtt.storeOp     = vk::AttachmentStoreOp::eStore;
    colorAtt.clearValue  = vk::ClearValue{
        vk::ClearColorValue{ std::array<float,4>{ 0.0f, 0.0f, 0.0f, 1.0f } } };

    vk::RenderingInfo ri{};
    ri.renderArea           = vk::Rect2D{ {0, 0}, extent };
    ri.layerCount           = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments    = &colorAtt;
    cmd.beginRendering(ri);

    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipelineLayout,
                           0, *descriptorSet, nullptr);

    vk::Viewport vp{};
    vp.width    = static_cast<float>(extent.width);
    vp.height   = static_cast<float>(extent.height);
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    cmd.setViewport(0, vp);
    cmd.setScissor(0, vk::Rect2D{ {0, 0}, extent });

    PushData push{ sunUV, sunRadius };
    cmd.pushConstants(*pipelineLayout, vk::ShaderStageFlagBits::eFragment,
                      0, sizeof(PushData), &push);
    cmd.draw(3, 1, 0, 0);

    cmd.endRendering();

    // Transition to ShaderReadOnlyOptimal for sampling in sun shafts pass
    barrier.oldLayout     = vk::ImageLayout::eColorAttachmentOptimal;
    barrier.newLayout     = vk::ImageLayout::eShaderReadOnlyOptimal;
    barrier.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
    barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput,
                        vk::PipelineStageFlagBits::eFragmentShader,
                        {}, {}, {}, barrier);
    occLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
}

#include "VulkanDebugOverlay.h"
#include "VulkanFramebuffer.h"
#include "VulkanShadowMap.h"
#include "VulkanInstance.h"
#include "VulkanSwapchain.h"
#include <IO/AssetManager.h>
#include <fstream>
#include <cstring>

std::vector<uint32_t> VulkanDebugOverlay::LoadSpv(const std::string& path)
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
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open())
        throw std::runtime_error("VulkanDebugOverlay: cannot open shader: " + path);
    size_t size = static_cast<size_t>(file.tellg());
    std::vector<uint32_t> buf(size / sizeof(uint32_t));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(buf.data()), size);
    return buf;
}

vk::raii::Pipeline VulkanDebugOverlay::BuildPipeline(
    vk::Format swapchainFormat, vk::Format depthFormat,
    const std::string& vertSpv, const std::string& fragSpv)
{
    auto& device = VulkanInstance::Get().GetDevice();

    auto mkModule = [&](const std::vector<uint32_t>& code) {
        vk::ShaderModuleCreateInfo ci{};
        ci.codeSize = code.size() * sizeof(uint32_t);
        ci.pCode    = code.data();
        return vk::raii::ShaderModule(device, ci);
    };

    auto vertMod = mkModule(LoadSpv(vertSpv));
    auto fragMod = mkModule(LoadSpv(fragSpv));

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

    vk::PipelineColorBlendAttachmentState colorBlendAtt{};
    colorBlendAtt.colorWriteMask =
        vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
        vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;

    vk::PipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments    = &colorBlendAtt;

    std::array<vk::DynamicState, 2> dynStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
    vk::PipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynStates.size());
    dynamicState.pDynamicStates    = dynStates.data();

    vk::PipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.colorAttachmentCount    = 1;
    renderingInfo.pColorAttachmentFormats = &swapchainFormat;
    renderingInfo.depthAttachmentFormat   = depthFormat;

    vk::GraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.pNext               = &renderingInfo;
    pipelineInfo.stageCount          = 2;
    pipelineInfo.pStages             = stages;
    pipelineInfo.pVertexInputState   = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState      = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState   = &multisampling;
    pipelineInfo.pDepthStencilState  = &depthStencil;
    pipelineInfo.pColorBlendState    = &colorBlending;
    pipelineInfo.pDynamicState       = &dynamicState;
    pipelineInfo.layout              = *pipelineLayout;

    return vk::raii::Pipeline(device, nullptr, pipelineInfo);
}

void VulkanDebugOverlay::Create(vk::Format swapchainFormat, vk::Format depthFormat)
{
    auto& device = VulkanInstance::Get().GetDevice();

    vk::DescriptorSetLayoutBinding samplerBinding{};
    samplerBinding.binding         = 0;
    samplerBinding.descriptorType  = vk::DescriptorType::eCombinedImageSampler;
    samplerBinding.descriptorCount = 1;
    samplerBinding.stageFlags      = vk::ShaderStageFlagBits::eFragment;

    vk::DescriptorSetLayoutCreateInfo dslInfo{};
    dslInfo.bindingCount = 1;
    dslInfo.pBindings    = &samplerBinding;
    descriptorSetLayout  = vk::raii::DescriptorSetLayout(device, dslInfo);

    vk::DescriptorSetLayout dsl = *descriptorSetLayout;
    vk::PipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts    = &dsl;
    pipelineLayout = vk::raii::PipelineLayout(device, layoutInfo);

    vk::DescriptorPoolSize poolSize{ vk::DescriptorType::eCombinedImageSampler, 2 };
    vk::DescriptorPoolCreateInfo poolInfo{};
    poolInfo.flags         = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    poolInfo.maxSets       = 2;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes    = &poolSize;
    descriptorPool = vk::raii::DescriptorPool(device, poolInfo);

    std::array<vk::DescriptorSetLayout, 2> dsls = { dsl, dsl };
    vk::DescriptorSetAllocateInfo allocInfo{};
    allocInfo.descriptorPool     = *descriptorPool;
    allocInfo.descriptorSetCount = 2;
    allocInfo.pSetLayouts        = dsls.data();
    auto sets           = device.allocateDescriptorSets(allocInfo);
    fbDescriptorSet     = std::move(sets[0]);
    shadowDescriptorSet = std::move(sets[1]);

    fbPipeline     = BuildPipeline(swapchainFormat, depthFormat,
                                   "shaders/debug.vert.spv",        "shaders/debug.frag.spv");
    shadowPipeline = BuildPipeline(swapchainFormat, depthFormat,
                                   "shaders/shadow_debug.vert.spv", "shaders/shadow_debug.frag.spv");
}

void VulkanDebugOverlay::Draw(vk::CommandBuffer cmd, VulkanFramebuffer& framebuffer, VulkanShadowMap& shadowMap)
{
    auto& device    = VulkanInstance::Get().GetDevice();
    auto& swapchain = VulkanSwapchain::Get();

    {
        vk::DescriptorImageInfo imgInfo{};
        imgInfo.sampler     = framebuffer.GetSampler();
        imgInfo.imageView   = framebuffer.GetColorView();
        imgInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        vk::WriteDescriptorSet write{};
        write.dstSet          = *fbDescriptorSet;
        write.dstBinding      = 0;
        write.descriptorCount = 1;
        write.descriptorType  = vk::DescriptorType::eCombinedImageSampler;
        write.pImageInfo      = &imgInfo;
        device.updateDescriptorSets(write, nullptr);
    }

    {
        vk::DescriptorImageInfo imgInfo{};
        imgInfo.sampler     = shadowMap.GetSampler();
        imgInfo.imageView   = shadowMap.GetArrayView();
        imgInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        vk::WriteDescriptorSet write{};
        write.dstSet          = *shadowDescriptorSet;
        write.dstBinding      = 0;
        write.descriptorCount = 1;
        write.descriptorType  = vk::DescriptorType::eCombinedImageSampler;
        write.pImageInfo      = &imgInfo;
        device.updateDescriptorSets(write, nullptr);
    }

    vk::Viewport vp{};
    vp.width    = static_cast<float>(swapchain.GetExtent().width);
    vp.height   = static_cast<float>(swapchain.GetExtent().height);
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    cmd.setViewport(0, vp);
    cmd.setScissor(0, vk::Rect2D{ {0,0}, swapchain.GetExtent() });

    // Framebuffer debug quad (bottom-right)
    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *fbPipeline);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipelineLayout, 0, *fbDescriptorSet, nullptr);
    cmd.draw(6, 1, 0, 0);

    // Shadow map debug quad (bottom-left)
    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *shadowPipeline);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipelineLayout, 0, *shadowDescriptorSet, nullptr);
    cmd.draw(6, 1, 0, 0);
}

void VulkanDebugOverlay::Destroy()
{
    shadowDescriptorSet = nullptr;
    fbDescriptorSet     = nullptr;
    descriptorPool      = nullptr;
    shadowPipeline      = nullptr;
    fbPipeline          = nullptr;
    pipelineLayout      = nullptr;
    descriptorSetLayout = nullptr;
}

#include "VulkanPointShadowPipeline.h"
#include "VulkanInstance.h"
#include "../../Core/Vertex.h"
#include <IO/AssetManager.h>
#include <fstream>
#include <cstring>

std::vector<uint32_t> VulkanPointShadowPipeline::LoadSpv(const std::string& path)
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
        throw std::runtime_error("VulkanPointShadowPipeline: cannot open shader: " + path);
    size_t size = static_cast<size_t>(file.tellg());
    std::vector<uint32_t> buf(size / sizeof(uint32_t));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(buf.data()), size);
    return buf;
}

void VulkanPointShadowPipeline::Create(vk::Format depthFormat)
{
    auto& device = VulkanInstance::Get().GetDevice();

    auto mkMod = [&](const std::vector<uint32_t>& code) {
        vk::ShaderModuleCreateInfo ci{};
        ci.codeSize = code.size() * sizeof(uint32_t);
        ci.pCode    = code.data();
        return vk::raii::ShaderModule(device, ci);
    };

    auto vertMod = mkMod(LoadSpv("shaders/point_shadow.vert.spv"));
    auto fragMod = mkMod(LoadSpv("shaders/point_shadow.frag.spv"));

    vk::PipelineShaderStageCreateInfo stages[2]{};
    stages[0].stage  = vk::ShaderStageFlagBits::eVertex;
    stages[0].module = *vertMod;
    stages[0].pName  = "main";
    stages[1].stage  = vk::ShaderStageFlagBits::eFragment;
    stages[1].module = *fragMod;
    stages[1].pName  = "main";

    auto vertBinding    = Vertex::GetBindingDescription();
    auto vertAttributes = Vertex::GetAttributeDescriptions();

    vk::VertexInputBindingDescription instanceBinding{};
    instanceBinding.binding   = 1;
    instanceBinding.stride    = sizeof(glm::mat4);
    instanceBinding.inputRate = vk::VertexInputRate::eInstance;

    std::array<vk::VertexInputAttributeDescription, 4> instanceAttrs{};
    for (uint32_t i = 0; i < 4; i++)
    {
        instanceAttrs[i].binding  = 1;
        instanceAttrs[i].location = 8 + i;  // 0-7 = vertex (6=boneIdx, 7=boneWeights)
        instanceAttrs[i].format   = vk::Format::eR32G32B32A32Sfloat;
        instanceAttrs[i].offset   = sizeof(glm::vec4) * i;
    }

    std::array<vk::VertexInputBindingDescription, 2> allBindings = { vertBinding, instanceBinding };
    std::vector<vk::VertexInputAttributeDescription> allAttributes(vertAttributes.begin(), vertAttributes.end());
    for (auto& a : instanceAttrs) allAttributes.push_back(a);

    vk::PipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.vertexBindingDescriptionCount   = static_cast<uint32_t>(allBindings.size());
    vertexInput.pVertexBindingDescriptions      = allBindings.data();
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(allAttributes.size());
    vertexInput.pVertexAttributeDescriptions    = allAttributes.data();

    vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;

    vk::PipelineViewportStateCreateInfo viewportState{};
    viewportState.viewportCount = 1;
    viewportState.scissorCount  = 1;

    vk::PipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.polygonMode             = vk::PolygonMode::eFill;
    rasterizer.cullMode                = vk::CullModeFlagBits::eBack;
    rasterizer.frontFace               = vk::FrontFace::eCounterClockwise;
    rasterizer.lineWidth               = 1.0f;
    rasterizer.depthBiasEnable         = vk::True;
    rasterizer.depthBiasConstantFactor = 1.5f;
    rasterizer.depthBiasSlopeFactor    = 2.5f;

    vk::PipelineMultisampleStateCreateInfo multisampling{};
    multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

    vk::PipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.depthTestEnable  = vk::True;
    depthStencil.depthWriteEnable = vk::True;
    depthStencil.depthCompareOp   = vk::CompareOp::eLessOrEqual;

    vk::PipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.attachmentCount = 0;  // depth-only

    std::array<vk::DynamicState, 2> dynStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
    vk::PipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynStates.size());
    dynamicState.pDynamicStates    = dynStates.data();

    // Push constant: mat4 lightViewProj (64) + vec4 lightPosAndFar (16) = 80 bytes
    vk::PushConstantRange pc{};
    pc.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
    pc.offset     = 0;
    pc.size       = 80;

    // Set 0: bone SSBO
    vk::DescriptorSetLayoutBinding boneBinding{};
    boneBinding.binding         = 0;
    boneBinding.descriptorType  = vk::DescriptorType::eStorageBuffer;
    boneBinding.descriptorCount = 1;
    boneBinding.stageFlags      = vk::ShaderStageFlagBits::eVertex;

    vk::DescriptorSetLayoutCreateInfo boneDslInfo{};
    boneDslInfo.bindingCount = 1;
    boneDslInfo.pBindings    = &boneBinding;
    boneDescriptorSetLayout = vk::raii::DescriptorSetLayout(device, boneDslInfo);

    vk::DescriptorSetLayout dsl = *boneDescriptorSetLayout;
    vk::PipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.setLayoutCount         = 1;
    layoutInfo.pSetLayouts            = &dsl;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges    = &pc;
    pipelineLayout = vk::raii::PipelineLayout(device, layoutInfo);

    vk::PipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.colorAttachmentCount  = 0;
    renderingInfo.depthAttachmentFormat = depthFormat;

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

    pipeline = vk::raii::Pipeline(device, nullptr, pipelineInfo);
    std::cout << "[Vulkan] PointShadowPipeline OK" << std::endl;
}

void VulkanPointShadowPipeline::Bind(vk::CommandBuffer cmd)
{
    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);
}

void VulkanPointShadowPipeline::Destroy()
{
    pipeline                = nullptr;
    pipelineLayout          = nullptr;
    boneDescriptorSetLayout = nullptr;
}

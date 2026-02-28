#include "VulkanShadowPipeline.h"
#include "VulkanInstance.h"
#include <fstream>

std::vector<uint32_t> VulkanShadowPipeline::LoadSpv(const std::string& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open())
        throw std::runtime_error("VulkanShadowPipeline: cannot open shader: " + path);
    size_t size = static_cast<size_t>(file.tellg());
    std::vector<uint32_t> buf(size / sizeof(uint32_t));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(buf.data()), size);
    return buf;
}

void VulkanShadowPipeline::Create(vk::Format depthFormat)
{
    auto& device = VulkanInstance::Get().GetDevice();

    auto vertCode = LoadSpv("shaders/shadow.vert.spv");
    vk::ShaderModuleCreateInfo mci{};
    mci.codeSize = vertCode.size() * sizeof(uint32_t);
    mci.pCode    = vertCode.data();
    vk::raii::ShaderModule vertMod(device, mci);

    vk::PipelineShaderStageCreateInfo vertStage{};
    vertStage.stage  = vk::ShaderStageFlagBits::eVertex;
    vertStage.module = *vertMod;
    vertStage.pName  = "main";

    // Match the main pipeline's vertex input exactly
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
        instanceAttrs[i].location = 6 + i;
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
    rasterizer.cullMode                = vk::CullModeFlagBits::eFront;
    rasterizer.frontFace               = vk::FrontFace::eCounterClockwise;
    rasterizer.lineWidth               = 1.0f;
    rasterizer.depthBiasEnable         = vk::True;
    rasterizer.depthBiasConstantFactor = 0.5f;
    rasterizer.depthBiasSlopeFactor    = 1.0f;

    vk::PipelineMultisampleStateCreateInfo multisampling{};
    multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

    vk::PipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.depthTestEnable  = vk::True;
    depthStencil.depthWriteEnable = vk::True;
    depthStencil.depthCompareOp   = vk::CompareOp::eLess;

    vk::PipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.attachmentCount = 0;

    std::array<vk::DynamicState, 2> dynStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
    vk::PipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynStates.size());
    dynamicState.pDynamicStates    = dynStates.data();

    vk::PushConstantRange pushConstant{};
    pushConstant.stageFlags = vk::ShaderStageFlagBits::eVertex;
    pushConstant.offset     = 0;
    pushConstant.size       = sizeof(glm::mat4);

    vk::PipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges    = &pushConstant;
    pipelineLayout = vk::raii::PipelineLayout(device, layoutInfo);

    vk::PipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.colorAttachmentCount  = 0;
    renderingInfo.depthAttachmentFormat = depthFormat;

    vk::GraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.pNext               = &renderingInfo;
    pipelineInfo.stageCount          = 1;
    pipelineInfo.pStages             = &vertStage;
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
}

void VulkanShadowPipeline::Bind(vk::CommandBuffer cmd)
{
    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);
}

void VulkanShadowPipeline::Destroy()
{
    pipeline       = nullptr;
    pipelineLayout = nullptr;
}

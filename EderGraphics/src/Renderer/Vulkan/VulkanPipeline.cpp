#include "VulkanPipeline.h"
#include "VulkanInstance.h"
#include "VulkanSwapchain.h"
#include <fstream>

std::vector<uint32_t> VulkanPipeline::LoadSpv(const std::string& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open())
        throw std::runtime_error("Failed to open shader: " + path);

    size_t fileSize = static_cast<size_t>(file.tellg());
    std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(buffer.data()), fileSize);
    return buffer;
}

vk::raii::ShaderModule VulkanPipeline::CreateShaderModule(const std::vector<uint32_t>& code)
{
    vk::ShaderModuleCreateInfo createInfo{};
    createInfo.codeSize = code.size() * sizeof(uint32_t);
    createInfo.pCode    = code.data();
    return vk::raii::ShaderModule(VulkanInstance::Get().GetDevice(), createInfo);
}

void VulkanPipeline::Create(const std::string& vertPath, const std::string& fragPath, vk::Format swapchainFormat, vk::Format depthFormat)
{
    auto& device = VulkanInstance::Get().GetDevice();

    auto vertCode = LoadSpv(vertPath);
    auto fragCode = LoadSpv(fragPath);

    auto vertModule = CreateShaderModule(vertCode);
    auto fragModule = CreateShaderModule(fragCode);

    vk::PipelineShaderStageCreateInfo vertStage{};
    vertStage.stage  = vk::ShaderStageFlagBits::eVertex;
    vertStage.module = *vertModule;
    vertStage.pName  = "main";

    vk::PipelineShaderStageCreateInfo fragStage{};
    fragStage.stage  = vk::ShaderStageFlagBits::eFragment;
    fragStage.module = *fragModule;
    fragStage.pName  = "main";

    std::array<vk::PipelineShaderStageCreateInfo, 2> stages = { vertStage, fragStage };

    auto binding    = Vertex::GetBindingDescription();
    auto attributes = Vertex::GetAttributeDescriptions();

    vk::PipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.vertexBindingDescriptionCount   = 1;
    vertexInput.pVertexBindingDescriptions      = &binding;
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
    vertexInput.pVertexAttributeDescriptions    = attributes.data();

    vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;

    vk::PipelineViewportStateCreateInfo viewportState{};
    viewportState.viewportCount = 1;
    viewportState.scissorCount  = 1;

    vk::PipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.polygonMode = vk::PolygonMode::eFill;
    rasterizer.cullMode    = vk::CullModeFlagBits::eBack;
    rasterizer.frontFace   = vk::FrontFace::eCounterClockwise;
    rasterizer.lineWidth   = 1.0f;

    vk::PipelineMultisampleStateCreateInfo multisampling{};
    multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

    vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask =
        vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
        vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;

    vk::PipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments    = &colorBlendAttachment;

    std::array<vk::DynamicState, 2> dynamicStates = {
        vk::DynamicState::eViewport,
        vk::DynamicState::eScissor
    };

    vk::PipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates    = dynamicStates.data();

    vk::DescriptorSetLayoutBinding uboBinding{};
    uboBinding.binding         = 0;
    uboBinding.descriptorType  = vk::DescriptorType::eUniformBuffer;
    uboBinding.descriptorCount = 1;
    uboBinding.stageFlags      = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;

    vk::DescriptorSetLayoutBinding samplerBinding{};
    samplerBinding.binding         = 1;
    samplerBinding.descriptorType  = vk::DescriptorType::eCombinedImageSampler;
    samplerBinding.descriptorCount = 1;
    samplerBinding.stageFlags      = vk::ShaderStageFlagBits::eFragment;

    std::array<vk::DescriptorSetLayoutBinding, 2> bindings = { uboBinding, samplerBinding };

    vk::DescriptorSetLayoutCreateInfo dslInfo{};
    dslInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    dslInfo.pBindings    = bindings.data();

    descriptorSetLayout = vk::raii::DescriptorSetLayout(device, dslInfo);

    vk::PushConstantRange pushConstant{};
    pushConstant.stageFlags = vk::ShaderStageFlagBits::eVertex;
    pushConstant.offset     = 0;
    pushConstant.size       = sizeof(glm::mat4) * 2;

    vk::DescriptorSetLayout dsl = *descriptorSetLayout;

    vk::PipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.setLayoutCount         = 1;
    layoutInfo.pSetLayouts            = &dsl;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges    = &pushConstant;

    pipelineLayout = vk::raii::PipelineLayout(device, layoutInfo);

    vk::PipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.depthTestEnable       = vk::True;
    depthStencil.depthWriteEnable      = vk::True;
    depthStencil.depthCompareOp        = vk::CompareOp::eLess;
    depthStencil.depthBoundsTestEnable = vk::False;
    depthStencil.stencilTestEnable     = vk::False;

    vk::PipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.colorAttachmentCount    = 1;
    renderingInfo.pColorAttachmentFormats = &swapchainFormat;
    renderingInfo.depthAttachmentFormat   = depthFormat;

    vk::GraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.pNext               = &renderingInfo;
    pipelineInfo.stageCount          = static_cast<uint32_t>(stages.size());
    pipelineInfo.pStages             = stages.data();
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
    std::cout << "[Vulkan] Pipeline OK" << std::endl;
}

void VulkanPipeline::Bind(vk::CommandBuffer cmd)
{
    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);
}

void VulkanPipeline::Destroy()
{
    pipeline            = nullptr;
    pipelineLayout      = nullptr;
    descriptorSetLayout = nullptr;
}

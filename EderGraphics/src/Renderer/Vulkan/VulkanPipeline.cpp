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

    vk::DescriptorSetLayoutBinding lightUboBinding{};
    lightUboBinding.binding         = 0;
    lightUboBinding.descriptorType  = vk::DescriptorType::eUniformBuffer;
    lightUboBinding.descriptorCount = 1;
    lightUboBinding.stageFlags      = vk::ShaderStageFlagBits::eFragment;

    vk::DescriptorSetLayoutBinding shadowSamplerBinding{};
    shadowSamplerBinding.binding         = 1;
    shadowSamplerBinding.descriptorType  = vk::DescriptorType::eCombinedImageSampler;
    shadowSamplerBinding.descriptorCount = 1;
    shadowSamplerBinding.stageFlags      = vk::ShaderStageFlagBits::eFragment;

    vk::DescriptorSetLayoutBinding spotShadowBinding{};
    spotShadowBinding.binding         = 2;
    spotShadowBinding.descriptorType  = vk::DescriptorType::eCombinedImageSampler;
    spotShadowBinding.descriptorCount = 1;
    spotShadowBinding.stageFlags      = vk::ShaderStageFlagBits::eFragment;

    vk::DescriptorSetLayoutBinding pointShadowBinding{};
    pointShadowBinding.binding         = 3;
    pointShadowBinding.descriptorType  = vk::DescriptorType::eCombinedImageSampler;
    pointShadowBinding.descriptorCount = 1;
    pointShadowBinding.stageFlags      = vk::ShaderStageFlagBits::eFragment;

    std::array<vk::DescriptorSetLayoutBinding, 4> lightBindings = {
        lightUboBinding, shadowSamplerBinding, spotShadowBinding, pointShadowBinding };

    vk::DescriptorSetLayoutCreateInfo lightDslInfo{};
    lightDslInfo.bindingCount = static_cast<uint32_t>(lightBindings.size());
    lightDslInfo.pBindings    = lightBindings.data();

    lightDescriptorSetLayout = vk::raii::DescriptorSetLayout(device, lightDslInfo);

    vk::PushConstantRange pushConstant{};
    pushConstant.stageFlags = vk::ShaderStageFlagBits::eVertex;
    pushConstant.offset     = 0;
    pushConstant.size       = sizeof(glm::mat4);

    std::array<vk::DescriptorSetLayout, 2> dsls = { *descriptorSetLayout, *lightDescriptorSetLayout };

    vk::PipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.setLayoutCount         = static_cast<uint32_t>(dsls.size());
    layoutInfo.pSetLayouts            = dsls.data();
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

    // -----------------------------------------------------------------------
    // Transparent pipeline — mismo shader, blend habilitado, depth write off
    // -----------------------------------------------------------------------
    vk::PipelineColorBlendAttachmentState blendAtt{};
    blendAtt.blendEnable         = vk::True;
    blendAtt.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
    blendAtt.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
    blendAtt.colorBlendOp        = vk::BlendOp::eAdd;
    blendAtt.srcAlphaBlendFactor = vk::BlendFactor::eOne;
    blendAtt.dstAlphaBlendFactor = vk::BlendFactor::eZero;
    blendAtt.alphaBlendOp        = vk::BlendOp::eAdd;
    blendAtt.colorWriteMask      =
        vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
        vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;

    vk::PipelineColorBlendStateCreateInfo blendInfo{};
    blendInfo.attachmentCount = 1;
    blendInfo.pAttachments    = &blendAtt;

    vk::PipelineDepthStencilStateCreateInfo depthNoWrite{};
    depthNoWrite.depthTestEnable  = vk::True;   // sigue testeando (opacos delante)
    depthNoWrite.depthWriteEnable = vk::False;  // NO escribe depth (orden manual)
    depthNoWrite.depthCompareOp   = vk::CompareOp::eLess;

    vk::GraphicsPipelineCreateInfo transpInfo = pipelineInfo;
    transpInfo.pColorBlendState  = &blendInfo;
    transpInfo.pDepthStencilState = &depthNoWrite;

    pipelineTransparent = vk::raii::Pipeline(device, nullptr, transpInfo);
    std::cout << "[Vulkan] Transparent Pipeline OK" << std::endl;
}

void VulkanPipeline::Bind(vk::CommandBuffer cmd)
{
    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);
}

void VulkanPipeline::BindTransparent(vk::CommandBuffer cmd)
{
    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipelineTransparent);
}

void VulkanPipeline::Destroy()
{
    pipelineTransparent      = nullptr;
    pipeline                 = nullptr;
    pipelineLayout           = nullptr;
    lightDescriptorSetLayout = nullptr;
    descriptorSetLayout      = nullptr;
}

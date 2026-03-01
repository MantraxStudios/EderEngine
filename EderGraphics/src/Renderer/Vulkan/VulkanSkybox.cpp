#include "VulkanSkybox.h"
#include "VulkanInstance.h"
#include <fstream>
#include <glm/gtc/type_ptr.hpp>

std::vector<uint32_t> VulkanSkybox::LoadSpv(const std::string& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open())
        throw std::runtime_error("VulkanSkybox: cannot open shader: " + path);
    size_t size = static_cast<size_t>(file.tellg());
    std::vector<uint32_t> buf(size / sizeof(uint32_t));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(buf.data()), size);
    return buf;
}

void VulkanSkybox::Create(vk::Format swapchainFormat, vk::Format depthFormat)
{
    auto& device = VulkanInstance::Get().GetDevice();

    // Push constant: mat4 invViewProj (64 bytes) + vec4 sunDir (16 bytes) = 80 bytes
    vk::PushConstantRange pc{};
    pc.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
    pc.offset     = 0;
    pc.size       = 80;

    vk::PipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges    = &pc;
    pipelineLayout = vk::raii::PipelineLayout(device, layoutInfo);

    auto mkModule = [&](const std::vector<uint32_t>& code)
    {
        vk::ShaderModuleCreateInfo ci{};
        ci.codeSize = code.size() * sizeof(uint32_t);
        ci.pCode    = code.data();
        return vk::raii::ShaderModule(device, ci);
    };

    auto vertMod = mkModule(LoadSpv("shaders/skybox.vert.spv"));
    auto fragMod = mkModule(LoadSpv("shaders/skybox.frag.spv"));

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

    // Depth test eLessOrEqual (skybox sets depth = 1.0 via gl_Position.z = gl_Position.w)
    // No depth write so it doesn't overwrite scene geometry
    vk::PipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.depthTestEnable  = vk::True;
    depthStencil.depthWriteEnable = vk::False;
    depthStencil.depthCompareOp   = vk::CompareOp::eLessOrEqual;

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

    pipeline = vk::raii::Pipeline(device, nullptr, pipelineInfo);

    std::cout << "[Vulkan] Skybox OK" << std::endl;
}

void VulkanSkybox::Draw(vk::CommandBuffer cmd,
                        const glm::mat4& invViewProj,
                        glm::vec3        sunDir,
                        float            sunIntensity)
{
    struct PushData
    {
        glm::mat4 invViewProj;  // 64 bytes
        glm::vec4 sunDir;       // 16 bytes: xyz=direction, w=intensity
    } push;

    push.invViewProj = invViewProj;
    push.sunDir      = glm::vec4(glm::normalize(sunDir), sunIntensity);

    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);
    cmd.pushConstants(
        *pipelineLayout,
        vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        0u, static_cast<uint32_t>(sizeof(PushData)),
        &push);
    cmd.draw(3, 1, 0, 0);
}

void VulkanSkybox::Destroy()
{
    pipeline       = nullptr;
    pipelineLayout = nullptr;
}

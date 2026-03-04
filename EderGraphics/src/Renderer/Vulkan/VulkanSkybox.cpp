#include "VulkanSkybox.h"
#include "VulkanInstance.h"
#include <IO/AssetManager.h>
#include <SDL3/SDL.h>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cstring>

std::vector<uint32_t> VulkanSkybox::LoadSpv(const std::string& path)
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
    SDL_IOStream* io = SDL_IOFromFile(path.c_str(), "rb");
    if (!io)
        throw std::runtime_error("VulkanSkybox: cannot open shader: " + path +
                                 " — " + SDL_GetError());

    Sint64 size = SDL_GetIOSize(io);
    if (size <= 0 || (size % sizeof(uint32_t)) != 0)
    {
        SDL_CloseIO(io);
        throw std::runtime_error("VulkanSkybox: invalid SPV size in: " + path);
    }

    std::vector<uint32_t> buf(static_cast<size_t>(size) / sizeof(uint32_t));
    SDL_ReadIO(io, buf.data(), static_cast<size_t>(size));
    SDL_CloseIO(io);
    return buf;
}

void VulkanSkybox::Create(vk::Format swapchainFormat, vk::Format depthFormat)
{
    auto& device = VulkanInstance::Get().GetDevice();

    // ── Camera UBO ───────────────────────────────────────────────────────────
    cameraBuffer.Create(sizeof(CameraUBO),
                        vk::BufferUsageFlagBits::eUniformBuffer,
                        vk::MemoryPropertyFlagBits::eHostVisible |
                        vk::MemoryPropertyFlagBits::eHostCoherent);

    // ── Descriptor set layout  (set=0, binding=0 = uniform buffer) ───────────
    vk::DescriptorSetLayoutBinding camBinding{};
    camBinding.binding         = 0;
    camBinding.descriptorType  = vk::DescriptorType::eUniformBuffer;
    camBinding.descriptorCount = 1;
    camBinding.stageFlags      = vk::ShaderStageFlagBits::eFragment;

    vk::DescriptorSetLayoutCreateInfo dslCI{};
    dslCI.bindingCount = 1;
    dslCI.pBindings    = &camBinding;
    setLayout = vk::raii::DescriptorSetLayout(device, dslCI);

    // ── Descriptor pool ───────────────────────────────────────────────────────
    vk::DescriptorPoolSize poolSize{};
    poolSize.type            = vk::DescriptorType::eUniformBuffer;
    poolSize.descriptorCount = 1;

    vk::DescriptorPoolCreateInfo poolCI{};
    poolCI.flags         = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    poolCI.maxSets       = 1;
    poolCI.poolSizeCount = 1;
    poolCI.pPoolSizes    = &poolSize;
    descPool = vk::raii::DescriptorPool(device, poolCI);

    // ── Descriptor set ────────────────────────────────────────────────────────
    vk::DescriptorSetLayout dsl = *setLayout;
    vk::DescriptorSetAllocateInfo allocCI{};
    allocCI.descriptorPool     = *descPool;
    allocCI.descriptorSetCount = 1;
    allocCI.pSetLayouts        = &dsl;
    auto sets = device.allocateDescriptorSets(allocCI);
    descSet   = std::move(sets[0]);

    vk::DescriptorBufferInfo bufInfo{};
    bufInfo.buffer = cameraBuffer.GetBuffer();
    bufInfo.offset = 0;
    bufInfo.range  = sizeof(CameraUBO);

    vk::WriteDescriptorSet write{};
    write.dstSet          = *descSet;
    write.dstBinding      = 0;
    write.descriptorCount = 1;
    write.descriptorType  = vk::DescriptorType::eUniformBuffer;
    write.pBufferInfo     = &bufInfo;
    device.updateDescriptorSets(write, nullptr);

    // ── Pipeline layout ───────────────────────────────────────────────────────
    // set=0: camera UBO | push_constant: vec4 sunDir (16 bytes, fragment only)
    vk::PushConstantRange pc{};
    pc.stageFlags = vk::ShaderStageFlagBits::eFragment;
    pc.offset     = 0;
    pc.size       = sizeof(glm::vec4);

    vk::DescriptorSetLayout layouts[1] = { *setLayout };
    vk::PipelineLayoutCreateInfo layoutCI{};
    layoutCI.setLayoutCount         = 1;
    layoutCI.pSetLayouts            = layouts;
    layoutCI.pushConstantRangeCount = 1;
    layoutCI.pPushConstantRanges    = &pc;
    pipelineLayout = vk::raii::PipelineLayout(device, layoutCI);

    // ── Shaders ───────────────────────────────────────────────────────────────
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

    // ── Pipeline state ────────────────────────────────────────────────────────
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

    // Depth test: eLessOrEqual so skybox only draws where depth = 1.0 (far plane)
    // No writes so it doesn't overwrite scene geometry depth
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

    std::array<vk::DynamicState, 2> dynStates = {
        vk::DynamicState::eViewport, vk::DynamicState::eScissor };
    vk::PipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynStates.size());
    dynamicState.pDynamicStates    = dynStates.data();

    vk::PipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.colorAttachmentCount    = 1;
    renderingInfo.pColorAttachmentFormats = &swapchainFormat;
    renderingInfo.depthAttachmentFormat   = depthFormat;

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
    pipelineCI.pColorBlendState    = &colorBlending;
    pipelineCI.pDynamicState       = &dynamicState;
    pipelineCI.layout              = *pipelineLayout;

    pipeline = vk::raii::Pipeline(device, nullptr, pipelineCI);

    std::cout << "[Vulkan] Skybox OK" << std::endl;
}

void VulkanSkybox::Draw(vk::CommandBuffer cmd,
                        const glm::mat4&  view,
                        const glm::mat4&  proj,
                        glm::vec3         sunDir,
                        float             sunIntensity)
{
    // Upload camera UBO
    CameraUBO ubo{};
    ubo.inverseView = glm::inverse(view);
    ubo.inverseProj = glm::inverse(proj);
    ubo.cameraPos   = glm::vec4(glm::vec3(ubo.inverseView[3]), 0.0f);
    cameraBuffer.Upload(&ubo, sizeof(ubo));

    glm::vec4 sunData = glm::vec4(glm::normalize(sunDir), sunIntensity);

    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipelineLayout,
                           0, *descSet, nullptr);
    cmd.pushConstants(*pipelineLayout,
                      vk::ShaderStageFlagBits::eFragment,
                      0, sizeof(glm::vec4), &sunData);
    cmd.draw(3, 1, 0, 0);
}

void VulkanSkybox::Destroy()
{
    pipeline       = nullptr;
    pipelineLayout = nullptr;
    descSet        = nullptr;
    descPool       = nullptr;
    setLayout      = nullptr;
    cameraBuffer.Destroy();
}

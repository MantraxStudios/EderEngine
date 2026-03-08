#include "VulkanBlit.h"
#include "Renderer/Vulkan/VulkanInstance.h"
#include <IO/AssetManager.h>
#include <fstream>
#include <cstring>
#include <array>

// ─────────────────────────────────────────────────────────────────────────────
std::vector<uint32_t> VulkanBlit::LoadSpv(const std::string& path)
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
        throw std::runtime_error("[VulkanBlit] Shader not found in AssetManager: " + path);
    }
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open())
        throw std::runtime_error("[VulkanBlit] Cannot open shader: " + path);
    size_t sz = static_cast<size_t>(f.tellg());
    std::vector<uint32_t> buf(sz / sizeof(uint32_t));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(buf.data()), sz);
    return buf;
}

// ─────────────────────────────────────────────────────────────────────────────
void VulkanBlit::Create(vk::Format swapchainFormat, vk::Format depthFormat)
{
    auto& device = VulkanInstance::Get().GetDevice();

    // Descriptor layout: one combined-image-sampler (the source colour)
    vk::DescriptorSetLayoutBinding binding{};
    binding.binding         = 0;
    binding.descriptorType  = vk::DescriptorType::eCombinedImageSampler;
    binding.descriptorCount = 1;
    binding.stageFlags      = vk::ShaderStageFlagBits::eFragment;

    vk::DescriptorSetLayoutCreateInfo dci{};
    dci.bindingCount = 1;
    dci.pBindings    = &binding;
    m_setLayout = vk::raii::DescriptorSetLayout(device, dci);

    // Pool + set
    vk::DescriptorPoolSize poolSize{ vk::DescriptorType::eCombinedImageSampler, 1 };
    vk::DescriptorPoolCreateInfo poolCI{};
    poolCI.flags         = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    poolCI.maxSets       = 1;
    poolCI.poolSizeCount = 1;
    poolCI.pPoolSizes    = &poolSize;
    m_descriptorPool = vk::raii::DescriptorPool(device, poolCI);

    vk::DescriptorSetLayout dsl = *m_setLayout;
    vk::DescriptorSetAllocateInfo allocInfo{};
    allocInfo.descriptorPool     = *m_descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts        = &dsl;
    m_descriptorSet = std::move(device.allocateDescriptorSets(allocInfo)[0]);

    // Pipeline layout
    vk::PipelineLayoutCreateInfo plci{};
    plci.setLayoutCount = 1;
    plci.pSetLayouts    = &*m_setLayout;
    m_pipelineLayout = vk::raii::PipelineLayout(device, plci);

    // Shaders
    auto mkMod = [&](const std::vector<uint32_t>& code)
    {
        vk::ShaderModuleCreateInfo ci{};
        ci.codeSize = code.size() * sizeof(uint32_t);
        ci.pCode    = code.data();
        return vk::raii::ShaderModule(device, ci);
    };
    auto vertMod = mkMod(LoadSpv("shaders/fog.vert.spv"));
    auto fragMod = mkMod(LoadSpv("shaders/blit.frag.spv"));

    vk::PipelineShaderStageCreateInfo stages[2]{};
    stages[0].stage  = vk::ShaderStageFlagBits::eVertex;
    stages[0].module = *vertMod;
    stages[0].pName  = "main";
    stages[1].stage  = vk::ShaderStageFlagBits::eFragment;
    stages[1].module = *fragMod;
    stages[1].pName  = "main";

    // No vertex input
    vk::PipelineVertexInputStateCreateInfo  vi{};
    vk::PipelineInputAssemblyStateCreateInfo ia{};
    ia.topology = vk::PrimitiveTopology::eTriangleList;

    vk::PipelineViewportStateCreateInfo vps{};
    vps.viewportCount = 1;
    vps.scissorCount  = 1;

    vk::PipelineRasterizationStateCreateInfo rast{};
    rast.polygonMode = vk::PolygonMode::eFill;
    rast.cullMode    = vk::CullModeFlagBits::eNone;
    rast.frontFace   = vk::FrontFace::eCounterClockwise;
    rast.lineWidth   = 1.0f;

    vk::PipelineMultisampleStateCreateInfo ms{};
    ms.rasterizationSamples = vk::SampleCountFlagBits::e1;

    // No depth write/test — we're just blitting colour
    vk::PipelineDepthStencilStateCreateInfo ds{};
    ds.depthTestEnable  = VK_FALSE;
    ds.depthWriteEnable = VK_FALSE;

    vk::PipelineColorBlendAttachmentState blendAtt{};
    blendAtt.colorWriteMask =
        vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
        vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;

    vk::PipelineColorBlendStateCreateInfo blend{};
    blend.attachmentCount = 1;
    blend.pAttachments    = &blendAtt;

    std::array<vk::DynamicState, 2> dynStates{
        vk::DynamicState::eViewport, vk::DynamicState::eScissor };
    vk::PipelineDynamicStateCreateInfo dyn{};
    dyn.dynamicStateCount = static_cast<uint32_t>(dynStates.size());
    dyn.pDynamicStates    = dynStates.data();

    // Dynamic rendering — must match the active render pass attachments
    vk::PipelineRenderingCreateInfo renderingCI{};
    renderingCI.colorAttachmentCount    = 1;
    renderingCI.pColorAttachmentFormats = &swapchainFormat;
    renderingCI.depthAttachmentFormat   = depthFormat;

    vk::GraphicsPipelineCreateInfo gpci{};
    gpci.stageCount          = 2;
    gpci.pStages             = stages;
    gpci.pVertexInputState   = &vi;
    gpci.pInputAssemblyState = &ia;
    gpci.pViewportState      = &vps;
    gpci.pRasterizationState = &rast;
    gpci.pMultisampleState   = &ms;
    gpci.pDepthStencilState  = &ds;
    gpci.pColorBlendState    = &blend;
    gpci.pDynamicState       = &dyn;
    gpci.layout              = *m_pipelineLayout;
    gpci.pNext               = &renderingCI;

    m_pipeline = std::move(device.createGraphicsPipelines(nullptr, gpci)[0]);
}

void VulkanBlit::Destroy()
{
    m_pipeline       = nullptr;
    m_pipelineLayout = nullptr;
    m_descriptorSet  = nullptr;
    m_descriptorPool = nullptr;
    m_setLayout      = nullptr;
    m_lastView       = nullptr;
    m_lastSampler    = nullptr;
}

void VulkanBlit::UpdateDescriptor(vk::ImageView view, vk::Sampler sampler)
{
    if (view == m_lastView && sampler == m_lastSampler) return;
    m_lastView    = view;
    m_lastSampler = sampler;

    vk::DescriptorImageInfo img{};
    img.sampler     = sampler;
    img.imageView   = view;
    img.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

    vk::WriteDescriptorSet w{};
    w.dstSet          = *m_descriptorSet;
    w.dstBinding      = 0;
    w.descriptorCount = 1;
    w.descriptorType  = vk::DescriptorType::eCombinedImageSampler;
    w.pImageInfo      = &img;
    VulkanInstance::Get().GetDevice().updateDescriptorSets(w, nullptr);
}

void VulkanBlit::Draw(vk::CommandBuffer cmd,
                      vk::ImageView srcView, vk::Sampler srcSampler)
{
    UpdateDescriptor(srcView, srcSampler);
    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_pipeline);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                           *m_pipelineLayout, 0, *m_descriptorSet, nullptr);
    cmd.draw(3, 1, 0, 0);
}

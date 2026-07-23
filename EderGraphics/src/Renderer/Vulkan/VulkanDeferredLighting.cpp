#include "VulkanDeferredLighting.h"
#include "VulkanInstance.h"
#include <IO/AssetManager.h>
#include <fstream>
#include <cstring>
#include <array>

std::vector<uint32_t> VulkanDeferredLighting::LoadSpv(const std::string& path)
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
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) throw std::runtime_error("VulkanDeferredLighting: cannot open shader: " + path);
    size_t sz = static_cast<size_t>(f.tellg());
    std::vector<uint32_t> buf(sz / sizeof(uint32_t));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(buf.data()), sz);
    return buf;
}

void VulkanDeferredLighting::Create(vk::Format colorFormat, vk::Format depthFormat,
                                    vk::DescriptorSetLayout lightDSL)
{
    auto& device = VulkanInstance::Get().GetDevice();

    std::array<vk::DescriptorSetLayoutBinding, 6> b{};
    b[0].binding = 0; b[0].descriptorType = vk::DescriptorType::eUniformBuffer;
    b[0].descriptorCount = 1; b[0].stageFlags = vk::ShaderStageFlagBits::eFragment;
    for (uint32_t i = 1; i < 6; i++) {
        b[i].binding = i; b[i].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        b[i].descriptorCount = 1; b[i].stageFlags = vk::ShaderStageFlagBits::eFragment;
    }
    vk::DescriptorSetLayoutCreateInfo lci{}; lci.bindingCount = 6; lci.pBindings = b.data();
    m_setLayout = vk::raii::DescriptorSetLayout(device, lci);

    vk::DescriptorPoolSize ps[2]{};
    ps[0].type = vk::DescriptorType::eUniformBuffer;        ps[0].descriptorCount = 1;
    ps[1].type = vk::DescriptorType::eCombinedImageSampler; ps[1].descriptorCount = 5;
    vk::DescriptorPoolCreateInfo pci{};
    pci.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    pci.maxSets = 1; pci.poolSizeCount = 2; pci.pPoolSizes = ps;
    m_descriptorPool = vk::raii::DescriptorPool(device, pci);

    vk::DescriptorSetLayout dsl = *m_setLayout;
    vk::DescriptorSetAllocateInfo ai{};
    ai.descriptorPool = *m_descriptorPool; ai.descriptorSetCount = 1; ai.pSetLayouts = &dsl;
    m_descriptorSet = std::move(device.allocateDescriptorSets(ai)[0]);

    m_ubo.Create(sizeof(DeferredUBO), vk::BufferUsageFlagBits::eUniformBuffer,
                 vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
    {
        vk::DescriptorBufferInfo bi{}; bi.buffer = m_ubo.GetBuffer(); bi.offset = 0; bi.range = sizeof(DeferredUBO);
        vk::WriteDescriptorSet wr{};
        wr.dstSet = *m_descriptorSet; wr.dstBinding = 0; wr.descriptorCount = 1;
        wr.descriptorType = vk::DescriptorType::eUniformBuffer; wr.pBufferInfo = &bi;
        device.updateDescriptorSets(wr, nullptr);
    }

    BuildPipeline(colorFormat, depthFormat, lightDSL);
}

void VulkanDeferredLighting::BuildPipeline(vk::Format colorFormat, vk::Format depthFormat,
                                           vk::DescriptorSetLayout lightDSL)
{
    auto& device = VulkanInstance::Get().GetDevice();
    auto mk = [&](const std::vector<uint32_t>& c){
        vk::ShaderModuleCreateInfo ci{}; ci.codeSize = c.size()*sizeof(uint32_t); ci.pCode = c.data();
        return vk::raii::ShaderModule(device, ci);
    };
    auto vs = mk(LoadSpv("shaders/fog.vert.spv"));
    auto fs = mk(LoadSpv("shaders/deferred_lighting.frag.spv"));
    vk::PipelineShaderStageCreateInfo st[2]{};
    st[0].stage = vk::ShaderStageFlagBits::eVertex;   st[0].module = *vs; st[0].pName = "main";
    st[1].stage = vk::ShaderStageFlagBits::eFragment; st[1].module = *fs; st[1].pName = "main";

    vk::PipelineVertexInputStateCreateInfo vi{};
    vk::PipelineInputAssemblyStateCreateInfo ia{}; ia.topology = vk::PrimitiveTopology::eTriangleList;
    vk::PipelineViewportStateCreateInfo vp{}; vp.viewportCount = 1; vp.scissorCount = 1;
    vk::PipelineRasterizationStateCreateInfo rs{}; rs.polygonMode = vk::PolygonMode::eFill;
    rs.cullMode = vk::CullModeFlagBits::eNone; rs.lineWidth = 1.0f;
    vk::PipelineMultisampleStateCreateInfo ms{}; ms.rasterizationSamples = vk::SampleCountFlagBits::e1;

    // Depth test Always + write ON so the fragment shader can re-emit the
    // G-buffer depth (gl_FragDepth) into the scene framebuffer's depth buffer.
    vk::PipelineDepthStencilStateCreateInfo ds{};
    ds.depthTestEnable  = vk::True;
    ds.depthWriteEnable = vk::True;
    ds.depthCompareOp   = vk::CompareOp::eAlways;

    vk::PipelineColorBlendAttachmentState cba{}; cba.blendEnable = vk::False;
    cba.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                         vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
    vk::PipelineColorBlendStateCreateInfo cb{}; cb.attachmentCount = 1; cb.pAttachments = &cba;
    std::array<vk::DynamicState,2> dyn = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
    vk::PipelineDynamicStateCreateInfo dss{}; dss.dynamicStateCount = 2; dss.pDynamicStates = dyn.data();

    std::array<vk::DescriptorSetLayout, 2> sets = { *m_setLayout, lightDSL };
    vk::PipelineLayoutCreateInfo plci{}; plci.setLayoutCount = 2; plci.pSetLayouts = sets.data();
    m_pipelineLayout = vk::raii::PipelineLayout(device, plci);

    vk::PipelineRenderingCreateInfo ri{};
    ri.colorAttachmentCount    = 1;
    ri.pColorAttachmentFormats = &colorFormat;
    ri.depthAttachmentFormat   = depthFormat;

    vk::GraphicsPipelineCreateInfo pci{};
    pci.pNext = &ri; pci.stageCount = 2; pci.pStages = st;
    pci.pVertexInputState = &vi; pci.pInputAssemblyState = &ia; pci.pViewportState = &vp;
    pci.pRasterizationState = &rs; pci.pMultisampleState = &ms; pci.pDepthStencilState = &ds;
    pci.pColorBlendState = &cb; pci.pDynamicState = &dss; pci.layout = *m_pipelineLayout;
    m_pipeline = vk::raii::Pipeline(device, nullptr, pci);
}

void VulkanDeferredLighting::UpdateDescriptor(vk::ImageView a, vk::ImageView n, vk::ImageView m,
                                              vk::ImageView d, vk::Sampler gs,
                                              vk::ImageView ssao, vk::Sampler ss)
{
    if (a == m_lastA && n == m_lastN && m == m_lastM && d == m_lastD &&
        ssao == m_lastSSAO && gs == m_lastGS && ss == m_lastSS) return;
    m_lastA = a; m_lastN = n; m_lastM = m; m_lastD = d; m_lastSSAO = ssao; m_lastGS = gs; m_lastSS = ss;

    auto& device = VulkanInstance::Get().GetDevice();
    auto img = [](vk::ImageView v, vk::Sampler s){
        vk::DescriptorImageInfo i{}; i.sampler = s; i.imageView = v; i.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal; return i;
    };
    vk::DescriptorImageInfo infos[5] = { img(a, gs), img(n, gs), img(m, gs), img(d, gs), img(ssao, ss) };
    vk::WriteDescriptorSet w[5]{};
    for (uint32_t i = 0; i < 5; i++) {
        w[i].dstSet = *m_descriptorSet; w[i].dstBinding = 1 + i;
        w[i].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        w[i].descriptorCount = 1; w[i].pImageInfo = &infos[i];
    }
    device.updateDescriptorSets(w, nullptr);
}

void VulkanDeferredLighting::Record(vk::CommandBuffer cmd,
    vk::ImageView albedoView, vk::ImageView normalView,
    vk::ImageView materialView, vk::ImageView depthView, vk::Sampler gSampler,
    vk::ImageView ssaoView, vk::Sampler ssaoSampler,
    const glm::mat4& invViewProj, vk::DescriptorSet lightDS)
{
    DeferredUBO u{}; u.invViewProj = invViewProj;
    m_ubo.Upload(&u, sizeof(u));
    UpdateDescriptor(albedoView, normalView, materialView, depthView, gSampler, ssaoView, ssaoSampler);

    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_pipeline);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *m_pipelineLayout, 0, *m_descriptorSet, {});
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *m_pipelineLayout, 1, lightDS, {});
    cmd.draw(3, 1, 0, 0);
}

void VulkanDeferredLighting::ResetBindings()
{
    m_lastA = m_lastN = m_lastM = m_lastD = m_lastSSAO = nullptr;
    m_lastGS = m_lastSS = nullptr;
}

void VulkanDeferredLighting::Destroy()
{
    m_ubo.Destroy();
    m_pipeline = nullptr; m_pipelineLayout = nullptr;
    m_descriptorSet = nullptr; m_descriptorPool = nullptr; m_setLayout = nullptr;
}

#include "VulkanSSAO.h"
#include "VulkanInstance.h"
#include "Renderer/VulkanRenderer.h"
#include <IO/AssetManager.h>
#include <fstream>
#include <cstring>
#include <array>
#include <glm/gtc/matrix_inverse.hpp>

std::vector<uint32_t> VulkanSSAO::LoadSpv(const std::string& path)
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
    if (!f.is_open()) throw std::runtime_error("VulkanSSAO: cannot open shader: " + path);
    size_t sz = static_cast<size_t>(f.tellg());
    std::vector<uint32_t> buf(sz / sizeof(uint32_t));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(buf.data()), sz);
    return buf;
}

void VulkanSSAO::Create(uint32_t w, uint32_t h)
{
    auto& device = VulkanInstance::Get().GetDevice();

    vk::DescriptorSetLayoutBinding b[3]{};
    b[0].binding = 0; b[0].descriptorType = vk::DescriptorType::eUniformBuffer;
    b[0].descriptorCount = 1; b[0].stageFlags = vk::ShaderStageFlagBits::eFragment;
    b[1].binding = 1; b[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
    b[1].descriptorCount = 1; b[1].stageFlags = vk::ShaderStageFlagBits::eFragment;
    b[2].binding = 2; b[2].descriptorType = vk::DescriptorType::eCombinedImageSampler;
    b[2].descriptorCount = 1; b[2].stageFlags = vk::ShaderStageFlagBits::eFragment;

    vk::DescriptorSetLayoutCreateInfo lci{};
    lci.bindingCount = 3; lci.pBindings = b;
    m_setLayout = vk::raii::DescriptorSetLayout(device, lci);

    vk::DescriptorPoolSize ps[2]{};
    ps[0].type = vk::DescriptorType::eUniformBuffer;        ps[0].descriptorCount = 1;
    ps[1].type = vk::DescriptorType::eCombinedImageSampler; ps[1].descriptorCount = 2;
    vk::DescriptorPoolCreateInfo pci{};
    pci.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    pci.maxSets = 1; pci.poolSizeCount = 2; pci.pPoolSizes = ps;
    m_descriptorPool = vk::raii::DescriptorPool(device, pci);

    vk::DescriptorSetLayout dsl = *m_setLayout;
    vk::DescriptorSetAllocateInfo ai{};
    ai.descriptorPool = *m_descriptorPool; ai.descriptorSetCount = 1; ai.pSetLayouts = &dsl;
    m_descriptorSet = std::move(device.allocateDescriptorSets(ai)[0]);

    m_ubo.Create(sizeof(SSAOUBO), vk::BufferUsageFlagBits::eUniformBuffer,
                 vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
    {
        vk::DescriptorBufferInfo bi{}; bi.buffer = m_ubo.GetBuffer(); bi.offset = 0; bi.range = sizeof(SSAOUBO);
        vk::WriteDescriptorSet wr{};
        wr.dstSet = *m_descriptorSet; wr.dstBinding = 0; wr.descriptorCount = 1;
        wr.descriptorType = vk::DescriptorType::eUniformBuffer; wr.pBufferInfo = &bi;
        device.updateDescriptorSets(wr, nullptr);
    }

    m_outputFb.Create(w, h, vk::Format::eR8Unorm, VulkanRenderer::Get().GetDepthFormat());
    BuildPipeline();
}

void VulkanSSAO::BuildPipeline()
{
    auto& device = VulkanInstance::Get().GetDevice();
    auto mk = [&](const std::vector<uint32_t>& c){
        vk::ShaderModuleCreateInfo ci{}; ci.codeSize = c.size()*sizeof(uint32_t); ci.pCode = c.data();
        return vk::raii::ShaderModule(device, ci);
    };
    auto vs = mk(LoadSpv("shaders/fog.vert.spv"));
    auto fs = mk(LoadSpv("shaders/ssao.frag.spv"));
    vk::PipelineShaderStageCreateInfo st[2]{};
    st[0].stage = vk::ShaderStageFlagBits::eVertex;   st[0].module = *vs; st[0].pName = "main";
    st[1].stage = vk::ShaderStageFlagBits::eFragment; st[1].module = *fs; st[1].pName = "main";

    vk::PipelineVertexInputStateCreateInfo vi{};
    vk::PipelineInputAssemblyStateCreateInfo ia{}; ia.topology = vk::PrimitiveTopology::eTriangleList;
    vk::PipelineViewportStateCreateInfo vp{}; vp.viewportCount = 1; vp.scissorCount = 1;
    vk::PipelineRasterizationStateCreateInfo rs{}; rs.polygonMode = vk::PolygonMode::eFill;
    rs.cullMode = vk::CullModeFlagBits::eNone; rs.lineWidth = 1.0f;
    vk::PipelineMultisampleStateCreateInfo ms{}; ms.rasterizationSamples = vk::SampleCountFlagBits::e1;
    vk::PipelineDepthStencilStateCreateInfo ds{}; ds.depthTestEnable = vk::False; ds.depthWriteEnable = vk::False;
    vk::PipelineColorBlendAttachmentState cba{}; cba.blendEnable = vk::False;
    cba.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                         vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
    vk::PipelineColorBlendStateCreateInfo cb{}; cb.attachmentCount = 1; cb.pAttachments = &cba;
    std::array<vk::DynamicState,2> dyn = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
    vk::PipelineDynamicStateCreateInfo dss{}; dss.dynamicStateCount = 2; dss.pDynamicStates = dyn.data();

    vk::DescriptorSetLayout dsl = *m_setLayout;
    vk::PipelineLayoutCreateInfo plci{}; plci.setLayoutCount = 1; plci.pSetLayouts = &dsl;
    m_pipelineLayout = vk::raii::PipelineLayout(device, plci);

    vk::Format fmt = vk::Format::eR8Unorm;
    vk::PipelineRenderingCreateInfo ri{}; ri.colorAttachmentCount = 1; ri.pColorAttachmentFormats = &fmt;

    vk::GraphicsPipelineCreateInfo pci{};
    pci.pNext = &ri; pci.stageCount = 2; pci.pStages = st;
    pci.pVertexInputState = &vi; pci.pInputAssemblyState = &ia; pci.pViewportState = &vp;
    pci.pRasterizationState = &rs; pci.pMultisampleState = &ms; pci.pDepthStencilState = &ds;
    pci.pColorBlendState = &cb; pci.pDynamicState = &dss; pci.layout = *m_pipelineLayout;
    m_pipeline = vk::raii::Pipeline(device, nullptr, pci);
}

void VulkanSSAO::UpdateDescriptor(vk::ImageView nv, vk::ImageView dv, vk::Sampler s)
{
    if (nv == m_lastNormal && dv == m_lastDepth && s == m_lastSmp) return;
    m_lastNormal = nv; m_lastDepth = dv; m_lastSmp = s;
    auto& device = VulkanInstance::Get().GetDevice();
    vk::DescriptorImageInfo ni{}; ni.sampler = s; ni.imageView = nv; ni.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    vk::DescriptorImageInfo di{}; di.sampler = s; di.imageView = dv; di.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    vk::WriteDescriptorSet w[2]{};
    w[0].dstSet = *m_descriptorSet; w[0].dstBinding = 1; w[0].descriptorType = vk::DescriptorType::eCombinedImageSampler; w[0].descriptorCount = 1; w[0].pImageInfo = &ni;
    w[1].dstSet = *m_descriptorSet; w[1].dstBinding = 2; w[1].descriptorType = vk::DescriptorType::eCombinedImageSampler; w[1].descriptorCount = 1; w[1].pImageInfo = &di;
    device.updateDescriptorSets(w, {});
}

void VulkanSSAO::Draw(vk::CommandBuffer cmd,
                      vk::ImageView normalView, vk::ImageView depthView, vk::Sampler gSampler,
                      const glm::mat4& proj, const glm::mat4& view,
                      float radius, float bias, float intensity, float power)
{
    SSAOUBO u{};
    u.proj    = proj;
    u.invProj = glm::inverse(proj);
    u.view    = view;
    u.params  = glm::vec4(radius, bias, intensity, power);
    m_ubo.Upload(&u, sizeof(u));

    UpdateDescriptor(normalView, depthView, gSampler);

    m_outputFb.BeginRendering(cmd);
    vk::Extent2D ext = m_outputFb.GetExtent();
    vk::Viewport vp{}; vp.width = float(ext.width); vp.height = float(ext.height); vp.maxDepth = 1.0f;
    cmd.setViewport(0, vp);
    vk::Rect2D sc{}; sc.extent = ext; cmd.setScissor(0, sc);
    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_pipeline);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *m_pipelineLayout, 0, *m_descriptorSet, {});
    cmd.draw(3, 1, 0, 0);
    m_outputFb.EndRendering(cmd);
}

void VulkanSSAO::Resize(uint32_t w, uint32_t h)
{
    m_outputFb.Recreate(w, h);
    m_lastNormal = nullptr; m_lastDepth = nullptr; m_lastSmp = nullptr;
}

void VulkanSSAO::Destroy()
{
    m_outputFb.Destroy();
    m_ubo.Destroy();
    m_pipeline = nullptr; m_pipelineLayout = nullptr;
    m_descriptorSet = nullptr; m_descriptorPool = nullptr; m_setLayout = nullptr;
}

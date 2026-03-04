#include "VulkanGizmo.h"
#include "Renderer/Vulkan/VulkanInstance.h"
#include "ECS/Registry.h"
#include "ECS/Components/TransformComponent.h"
#include "ECS/Components/LightComponent.h"
#include <IO/AssetManager.h>
#include <fstream>
#include <stdexcept>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <glm/gtc/constants.hpp>

// ---------------------------------------------------------------------------
// Shader loading
// ---------------------------------------------------------------------------

std::vector<uint32_t> VulkanGizmo::LoadSpv(const std::string& path)
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
        throw std::runtime_error("VulkanGizmo: cannot open shader: " + path);
    size_t sz = static_cast<size_t>(file.tellg());
    std::vector<uint32_t> buf(sz / sizeof(uint32_t));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(buf.data()), sz);
    return buf;
}

// ---------------------------------------------------------------------------
// Create / Destroy
// ---------------------------------------------------------------------------

void VulkanGizmo::Create(vk::Format colorFormat, vk::Format depthFormat)
{
    auto& device = VulkanInstance::Get().GetDevice();

    // Push constant: mat4 viewProj (64 B) + vec4 color (16 B) = 80 B
    vk::PushConstantRange pcRange{};
    pcRange.stageFlags = vk::ShaderStageFlagBits::eVertex |
                         vk::ShaderStageFlagBits::eFragment;
    pcRange.offset = 0;
    pcRange.size   = sizeof(PushData);

    vk::PipelineLayoutCreateInfo layoutCI{};
    layoutCI.pushConstantRangeCount = 1;
    layoutCI.pPushConstantRanges    = &pcRange;
    pipelineLayout = vk::raii::PipelineLayout(device, layoutCI);

    BuildPipeline(colorFormat, depthFormat);

    vertexBuffer.Create(
        MAX_VERTS * sizeof(Vertex),
        vk::BufferUsageFlagBits::eVertexBuffer,
        vk::MemoryPropertyFlagBits::eHostVisible |
        vk::MemoryPropertyFlagBits::eHostCoherent);
}

void VulkanGizmo::BuildPipeline(vk::Format colorFormat, vk::Format depthFormat)
{
    auto& device = VulkanInstance::Get().GetDevice();

    auto mkModule = [&](const std::vector<uint32_t>& code)
    {
        vk::ShaderModuleCreateInfo ci{};
        ci.codeSize = code.size() * sizeof(uint32_t);
        ci.pCode    = code.data();
        return vk::raii::ShaderModule(device, ci);
    };

    auto vertMod = mkModule(LoadSpv("shaders/gizmo.vert.spv"));
    auto fragMod = mkModule(LoadSpv("shaders/gizmo.frag.spv"));

    vk::PipelineShaderStageCreateInfo stages[2]{};
    stages[0].stage  = vk::ShaderStageFlagBits::eVertex;
    stages[0].module = *vertMod;
    stages[0].pName  = "main";
    stages[1].stage  = vk::ShaderStageFlagBits::eFragment;
    stages[1].module = *fragMod;
    stages[1].pName  = "main";

    // Vertex input: binding 0, stride = 12 bytes (vec3)
    vk::VertexInputBindingDescription binding{};
    binding.binding   = 0;
    binding.stride    = sizeof(Vertex);
    binding.inputRate = vk::VertexInputRate::eVertex;

    vk::VertexInputAttributeDescription attrib{};
    attrib.binding  = 0;
    attrib.location = 0;
    attrib.format   = vk::Format::eR32G32B32Sfloat;
    attrib.offset   = 0;

    vk::PipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.vertexBindingDescriptionCount   = 1;
    vertexInput.pVertexBindingDescriptions      = &binding;
    vertexInput.vertexAttributeDescriptionCount = 1;
    vertexInput.pVertexAttributeDescriptions    = &attrib;

    vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.topology = vk::PrimitiveTopology::eLineList;

    vk::PipelineViewportStateCreateInfo viewportState{};
    viewportState.viewportCount = 1;
    viewportState.scissorCount  = 1;

    vk::PipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.polygonMode = vk::PolygonMode::eFill;
    rasterizer.cullMode    = vk::CullModeFlagBits::eNone;
    rasterizer.lineWidth   = 1.0f;

    vk::PipelineMultisampleStateCreateInfo multisampling{};
    multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

    // No depth test — gizmos always visible on top
    vk::PipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.depthTestEnable  = vk::False;
    depthStencil.depthWriteEnable = vk::False;

    // Alpha blend so gizmos don't completely cover the scene
    vk::PipelineColorBlendAttachmentState colorAtt{};
    colorAtt.blendEnable         = vk::True;
    colorAtt.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
    colorAtt.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
    colorAtt.colorBlendOp        = vk::BlendOp::eAdd;
    colorAtt.srcAlphaBlendFactor = vk::BlendFactor::eOne;
    colorAtt.dstAlphaBlendFactor = vk::BlendFactor::eZero;
    colorAtt.alphaBlendOp        = vk::BlendOp::eAdd;
    colorAtt.colorWriteMask      =
        vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
        vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;

    vk::PipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments    = &colorAtt;

    std::array<vk::DynamicState, 2> dynStates = {
        vk::DynamicState::eViewport, vk::DynamicState::eScissor };
    vk::PipelineDynamicStateCreateInfo dynState{};
    dynState.dynamicStateCount = static_cast<uint32_t>(dynStates.size());
    dynState.pDynamicStates    = dynStates.data();

    vk::PipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.colorAttachmentCount    = 1;
    renderingInfo.pColorAttachmentFormats = &colorFormat;
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
    pipelineCI.pColorBlendState    = &colorBlend;
    pipelineCI.pDynamicState       = &dynState;
    pipelineCI.layout              = *pipelineLayout;

    pipeline = vk::raii::Pipeline(device, nullptr, pipelineCI);
}

void VulkanGizmo::Destroy()
{
    vertexBuffer.Destroy();
    pipeline       = nullptr;
    pipelineLayout = nullptr;
}

// ---------------------------------------------------------------------------
// Geometry helpers
// ---------------------------------------------------------------------------

void VulkanGizmo::AddCross(std::vector<Vertex>& v, glm::vec3 p, float s)
{
    v.push_back({p.x - s, p.y,     p.z    }); v.push_back({p.x + s, p.y,     p.z    });
    v.push_back({p.x,     p.y - s, p.z    }); v.push_back({p.x,     p.y + s, p.z    });
    v.push_back({p.x,     p.y,     p.z - s}); v.push_back({p.x,     p.y,     p.z + s});
}

void VulkanGizmo::AddCircle(std::vector<Vertex>& v, glm::vec3 c,
                             glm::vec3 r, glm::vec3 u, float radius, int N)
{
    const float step = glm::two_pi<float>() / static_cast<float>(N);
    for (int i = 0; i < N; i++)
    {
        float     a0 = step * static_cast<float>(i);
        float     a1 = step * static_cast<float>(i + 1);
        glm::vec3 p0 = c + r * (radius * std::cos(a0)) + u * (radius * std::sin(a0));
        glm::vec3 p1 = c + r * (radius * std::cos(a1)) + u * (radius * std::sin(a1));
        v.push_back({p0.x, p0.y, p0.z});
        v.push_back({p1.x, p1.y, p1.z});
    }
}

void VulkanGizmo::AddCone(std::vector<Vertex>& v, glm::vec3 origin,
                           glm::vec3 dir, float range, float angleDeg, int N)
{
    glm::vec3 d   = glm::normalize(dir);
    glm::vec3 tmp = glm::abs(d.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
    glm::vec3 right = glm::normalize(glm::cross(d, tmp));
    glm::vec3 up    = glm::cross(right, d);

    float     r   = range * std::tan(glm::radians(angleDeg));
    glm::vec3 tip = origin + d * range;

    // Circle at the open end
    AddCircle(v, tip, right, up, r, N);

    // 4 rays from origin to circle edge
    for (int i = 0; i < 4; i++)
    {
        float     a    = glm::two_pi<float>() * static_cast<float>(i) / 4.0f;
        glm::vec3 edge = tip + right * (r * std::cos(a)) + up * (r * std::sin(a));
        v.push_back({origin.x, origin.y, origin.z});
        v.push_back({edge.x,   edge.y,   edge.z  });
    }
}

// ---------------------------------------------------------------------------
// Draw
// ---------------------------------------------------------------------------

void VulkanGizmo::Draw(vk::CommandBuffer cmd,
                        const Registry&  registry,
                        const glm::mat4& viewProj,
                        uint32_t         selectedEntity)
{
    std::vector<Vertex>   verts;
    std::vector<DrawCall> calls;
    verts.reserve(512);

    for (Entity e : registry.GetEntities())
    {
        if (!registry.Has<LightComponent>(e)     ) continue;
        if (!registry.Has<TransformComponent>(e) ) continue;

        const auto& t = registry.Get<TransformComponent>(e);
        const auto& l = registry.Get<LightComponent>(e);

        bool      isSel = (e == selectedEntity);
        glm::vec3 pos   = t.position;
        // Selected: bright yellow. Normal: light color, semi-transparent.
        glm::vec4 col = isSel
            ? glm::vec4(1.0f, 0.95f, 0.20f, 1.0f)
            : glm::vec4(l.color, 0.70f);

        // Scale factor: selected gizmos are slightly larger
        float selScale = isSel ? 1.35f : 1.0f;

        uint32_t start = static_cast<uint32_t>(verts.size());

        if (l.type == LightType::Point)
        {
            // Small cross at the pivot + 3 circles at the ACTUAL range radius
            constexpr float cross = 0.35f;
            float ringR = l.range * selScale;   // exact range — no scaling/clamping
            AddCross (verts, pos, cross * selScale);
            AddCircle(verts, pos, {1,0,0}, {0,1,0}, ringR, 32);   // XY plane
            AddCircle(verts, pos, {1,0,0}, {0,0,1}, ringR, 32);   // XZ plane
            AddCircle(verts, pos, {0,1,0}, {0,0,1}, ringR, 32);   // YZ plane
        }
        else if (l.type == LightType::Directional)
        {
            // 5 downward arrows in a fan
            float arrowLen = 2.0f * selScale;
            for (int i = -2; i <= 2; i++)
            {
                glm::vec3 p = pos + glm::vec3(static_cast<float>(i) * 0.6f * selScale, 0.0f, 0.0f);
                verts.push_back({p.x, p.y + arrowLen * 0.75f, p.z});
                verts.push_back({p.x, p.y - arrowLen * 0.25f, p.z});
            }
        }
        else if (l.type == LightType::Spot)
        {
            // Cross at origin + cone sized by actual range and outer angle
            AddCross(verts, pos, 0.35f * selScale);

            glm::mat4 m   = t.GetMatrix();
            glm::vec3 dir = glm::normalize(glm::vec3(m * glm::vec4(0.0f, -1.0f, 0.0f, 0.0f)));

            float coneRange = l.range * selScale;   // exact range — no scaling/clamping
            AddCone(verts, pos, dir, coneRange, l.outerConeAngle, 20);
        }

        uint32_t count = static_cast<uint32_t>(verts.size()) - start;
        if (count > 0)
            calls.push_back({start, count, col});
    }

    if (verts.empty()) return;
    if (verts.size() > MAX_VERTS) verts.resize(MAX_VERTS);

    vertexBuffer.Upload(verts.data(), verts.size() * sizeof(Vertex));

    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);

    vk::Buffer    buf    = vertexBuffer.GetBuffer();
    vk::DeviceSize offset = 0;
    cmd.bindVertexBuffers(0, buf, offset);

    for (const auto& dc : calls)
    {
        PushData push{ viewProj, dc.color };
        cmd.pushConstants(*pipelineLayout,
            vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
            0, sizeof(PushData), &push);
        cmd.draw(dc.count, 1, dc.start, 0);
    }
}

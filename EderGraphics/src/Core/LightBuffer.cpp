#include "LightBuffer.h"
#include "Renderer/Vulkan/VulkanInstance.h"
#include "Renderer/Vulkan/VulkanPipeline.h"

void LightBuffer::Build(VulkanPipeline& pipeline)
{
    auto& device = VulkanInstance::Get().GetDevice();

    buffer.Create(
        sizeof(LightUBO),
        vk::BufferUsageFlagBits::eUniformBuffer,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

    std::array<vk::DescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type            = vk::DescriptorType::eUniformBuffer;
    poolSizes[0].descriptorCount = 1;
    poolSizes[1].type            = vk::DescriptorType::eCombinedImageSampler;
    poolSizes[1].descriptorCount = 1;

    vk::DescriptorPoolCreateInfo poolInfo{};
    poolInfo.flags         = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    poolInfo.maxSets       = 1;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes    = poolSizes.data();

    descriptorPool = vk::raii::DescriptorPool(device, poolInfo);

    vk::DescriptorSetLayout dsl = *pipeline.GetLightDescriptorSetLayout();

    vk::DescriptorSetAllocateInfo allocInfo{};
    allocInfo.descriptorPool     = *descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts        = &dsl;

    auto sets     = device.allocateDescriptorSets(allocInfo);
    descriptorSet = std::move(sets[0]);

    vk::DescriptorBufferInfo bufInfo{};
    bufInfo.buffer = buffer.GetBuffer();
    bufInfo.offset = 0;
    bufInfo.range  = sizeof(LightUBO);

    vk::WriteDescriptorSet write{};
    write.dstSet          = *descriptorSet;
    write.dstBinding      = 0;
    write.descriptorCount = 1;
    write.descriptorType  = vk::DescriptorType::eUniformBuffer;
    write.pBufferInfo     = &bufInfo;

    device.updateDescriptorSets(write, nullptr);
}

void LightBuffer::Update(const glm::vec3& cameraPos)
{
    ubo.cameraPos = cameraPos;
    buffer.Upload(&ubo, sizeof(LightUBO));
}

void LightBuffer::Bind(vk::CommandBuffer cmd, vk::PipelineLayout layout)
{
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, 1, *descriptorSet, nullptr);
}

void LightBuffer::Destroy()
{
    descriptorSet  = nullptr;
    descriptorPool = nullptr;
    buffer.Destroy();
}

void LightBuffer::AddDirectional(const DirectionalLight& light)
{
    if (ubo.numDirLights < MAX_DIR_LIGHTS)
        ubo.dirLights[ubo.numDirLights++] = light;
}

void LightBuffer::AddPoint(const PointLight& light)
{
    if (ubo.numPointLights < MAX_POINT_LIGHTS)
        ubo.pointLights[ubo.numPointLights++] = light;
}

void LightBuffer::AddSpot(const SpotLight& light)
{
    if (ubo.numSpotLights < MAX_SPOT_LIGHTS)
        ubo.spotLights[ubo.numSpotLights++] = light;
}

void LightBuffer::ClearLights()
{
    ubo.numDirLights   = 0;
    ubo.numPointLights = 0;
    ubo.numSpotLights  = 0;
}

void LightBuffer::SetCascadeData(const glm::mat4 matrices[4], const glm::vec4& splits)
{
    for (int i = 0; i < 4; i++) ubo.cascadeMatrices[i] = matrices[i];
    ubo.cascadeSplits = splits;
}

void LightBuffer::SetCameraForward(const glm::vec3& forward)
{
    ubo.cameraForward = forward;
}

void LightBuffer::BindShadowMap(vk::ImageView arrayView, vk::Sampler sampler)
{
    vk::DescriptorImageInfo imgInfo{};
    imgInfo.sampler     = sampler;
    imgInfo.imageView   = arrayView;
    imgInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

    vk::WriteDescriptorSet write{};
    write.dstSet          = *descriptorSet;
    write.dstBinding      = 1;
    write.descriptorCount = 1;
    write.descriptorType  = vk::DescriptorType::eCombinedImageSampler;
    write.pImageInfo      = &imgInfo;

    VulkanInstance::Get().GetDevice().updateDescriptorSets(write, nullptr);
}

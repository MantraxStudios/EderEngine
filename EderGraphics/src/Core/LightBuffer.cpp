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

    vk::DescriptorPoolSize poolSize{};
    poolSize.type            = vk::DescriptorType::eUniformBuffer;
    poolSize.descriptorCount = 1;

    vk::DescriptorPoolCreateInfo poolInfo{};
    poolInfo.flags         = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    poolInfo.maxSets       = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes    = &poolSize;

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

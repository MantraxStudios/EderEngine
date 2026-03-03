#include "BoneSSBO.h"
#include "VulkanInstance.h"
#include "VulkanPipeline.h"
#include <algorithm>

void BoneSSBO::Create(VulkanPipeline& pipeline)
{
    auto& device = VulkanInstance::Get().GetDevice();

    // Allocate a storage buffer large enough for MAX_BONES matrices
    buffer.Create(
        sizeof(glm::mat4) * MAX_BONES,
        vk::BufferUsageFlagBits::eStorageBuffer,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

    // Pre-fill with identity matrices
    std::vector<glm::mat4> identity(MAX_BONES, glm::mat4(1.0f));
    buffer.Upload(identity.data(), sizeof(glm::mat4) * MAX_BONES);

    // Descriptor pool — 1 SSBO
    vk::DescriptorPoolSize poolSize{};
    poolSize.type            = vk::DescriptorType::eStorageBuffer;
    poolSize.descriptorCount = 1;

    vk::DescriptorPoolCreateInfo poolInfo{};
    poolInfo.flags         = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    poolInfo.maxSets       = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes    = &poolSize;

    descriptorPool = vk::raii::DescriptorPool(device, poolInfo);

    vk::DescriptorSetLayout dsl = *pipeline.GetBoneDescriptorSetLayout();

    vk::DescriptorSetAllocateInfo allocInfo{};
    allocInfo.descriptorPool     = *descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts        = &dsl;

    auto sets     = device.allocateDescriptorSets(allocInfo);
    descriptorSet = std::move(sets[0]);

    // Write SSBO to descriptor set
    vk::DescriptorBufferInfo bufInfo{};
    bufInfo.buffer = buffer.GetBuffer();
    bufInfo.offset = 0;
    bufInfo.range  = sizeof(glm::mat4) * MAX_BONES;

    vk::WriteDescriptorSet write{};
    write.dstSet          = *descriptorSet;
    write.dstBinding      = 0;
    write.descriptorCount = 1;
    write.descriptorType  = vk::DescriptorType::eStorageBuffer;
    write.pBufferInfo     = &bufInfo;

    device.updateDescriptorSets(write, nullptr);
}

void BoneSSBO::Upload(const std::vector<glm::mat4>& boneMatrices)
{
    uint32_t count = static_cast<uint32_t>(std::min((size_t)MAX_BONES, boneMatrices.size()));
    buffer.Upload(boneMatrices.data(), sizeof(glm::mat4) * count);
}

void BoneSSBO::Bind(vk::CommandBuffer cmd, vk::PipelineLayout layout)
{
    // Bind as set 2
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, 2, *descriptorSet, nullptr);
}

void BoneSSBO::Destroy()
{
    descriptorSet  = nullptr;
    descriptorPool = nullptr;
    buffer.Destroy();
}

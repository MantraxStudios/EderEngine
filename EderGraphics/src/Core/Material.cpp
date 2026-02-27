#include "Material.h"
#include "Renderer/Vulkan/VulkanInstance.h"
#include "Renderer/Vulkan/VulkanPipeline.h"

void Material::Build(const MaterialLayout& inLayout, VulkanPipeline& pipeline)
{
    layout = inLayout;

    size_t blockSize = layout.GetBlockSize();
    cpuBuffer.assign(blockSize, 0);

    ubo.Create(
        static_cast<vk::DeviceSize>(blockSize),
        vk::BufferUsageFlagBits::eUniformBuffer,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

    auto& device = VulkanInstance::Get().GetDevice();

    vk::DescriptorPoolSize poolSize{};
    poolSize.type            = vk::DescriptorType::eUniformBuffer;
    poolSize.descriptorCount = 1;

    vk::DescriptorPoolCreateInfo poolInfo{};
    poolInfo.flags         = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    poolInfo.maxSets       = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes    = &poolSize;

    descriptorPool = vk::raii::DescriptorPool(device, poolInfo);

    vk::DescriptorSetLayout dsl = *pipeline.GetDescriptorSetLayout();

    vk::DescriptorSetAllocateInfo allocInfo{};
    allocInfo.descriptorPool     = *descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts        = &dsl;

    auto sets     = device.allocateDescriptorSets(allocInfo);
    descriptorSet = std::move(sets[0]);

    vk::DescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = ubo.GetBuffer();
    bufferInfo.offset = 0;
    bufferInfo.range  = static_cast<vk::DeviceSize>(blockSize);

    vk::WriteDescriptorSet write{};
    write.dstSet          = *descriptorSet;
    write.dstBinding      = 0;
    write.descriptorCount = 1;
    write.descriptorType  = vk::DescriptorType::eUniformBuffer;
    write.pBufferInfo     = &bufferInfo;

    device.updateDescriptorSets(write, nullptr);
    dirty = true;
}

void Material::SetProperty(const std::string& name, MaterialPropertyValue value)
{
    if (!layout.Find(name))
        throw std::runtime_error("Material: unknown property '" + name + "'");

    properties[name] = std::move(value);
    dirty = true;
}

void Material::SetFloat(const std::string& name, float value)        { SetProperty(name, value); }
void Material::SetInt  (const std::string& name, int32_t value)      { SetProperty(name, value); }
void Material::SetVec2 (const std::string& name, const glm::vec2& v) { SetProperty(name, v); }
void Material::SetVec3 (const std::string& name, const glm::vec3& v) { SetProperty(name, v); }
void Material::SetVec4 (const std::string& name, const glm::vec4& v) { SetProperty(name, v); }
void Material::SetMat4 (const std::string& name, const glm::mat4& v) { SetProperty(name, v); }

void Material::FlushToGPU()
{
    if (!dirty) return;
    dirty = false;

    for (const auto& [name, value] : properties)
    {
        const MaterialFieldInfo* field = layout.Find(name);
        if (!field) continue;

        std::visit([&](const auto& v)
        {
            memcpy(cpuBuffer.data() + field->offset, &v, field->size);
        }, value);
    }

    ubo.Upload(cpuBuffer.data(), static_cast<vk::DeviceSize>(cpuBuffer.size()));
}

void Material::Bind(vk::CommandBuffer cmd, vk::PipelineLayout pipelineLayout)
{
    FlushToGPU();
    cmd.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        pipelineLayout,
        0,
        *descriptorSet,
        nullptr);
}

void Material::Destroy()
{
    descriptorSet  = nullptr;
    descriptorPool = nullptr;
    ubo.Destroy();
}
#include "Material.h"
#include "Renderer/Vulkan/VulkanInstance.h"
#include "Renderer/Vulkan/VulkanPipeline.h"
#include "Renderer/Vulkan/VulkanTexture.h"

void Material::Build(const MaterialLayout& inLayout, VulkanPipeline& pipeline)
{
    layout = inLayout;

    size_t blockSize = layout.GetBlockSize();
    
    if (blockSize == 0)
        throw std::runtime_error("Material: layout has zero block size");
    
    std::cout << "[Material] Block size: " << blockSize << " bytes" << std::endl;
    
    cpuBuffer.assign(blockSize, 0);

    ubo.Create(
        static_cast<vk::DeviceSize>(blockSize),
        vk::BufferUsageFlagBits::eUniformBuffer,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

    std::cout << "[Material] UBO created" << std::endl;

    auto& device = VulkanInstance::Get().GetDevice();

    vk::DescriptorPoolSize uboPoolSize{};
    uboPoolSize.type            = vk::DescriptorType::eUniformBuffer;
    uboPoolSize.descriptorCount = 1;

    vk::DescriptorPoolSize samplerPoolSize{};
    samplerPoolSize.type            = vk::DescriptorType::eCombinedImageSampler;
    samplerPoolSize.descriptorCount = 1;

    std::array<vk::DescriptorPoolSize, 2> poolSizes = { uboPoolSize, samplerPoolSize };

    vk::DescriptorPoolCreateInfo poolInfo{};
    poolInfo.flags         = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    poolInfo.maxSets       = 1;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes    = poolSizes.data();

    descriptorPool = vk::raii::DescriptorPool(device, poolInfo);
    
    std::cout << "[Material] Descriptor pool created" << std::endl;

    vk::DescriptorSetLayout dsl = *pipeline.GetDescriptorSetLayout();

    vk::DescriptorSetAllocateInfo allocInfo{};
    allocInfo.descriptorPool     = *descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts        = &dsl;

    auto sets     = device.allocateDescriptorSets(allocInfo);
    descriptorSet = std::move(sets[0]);
    
    std::cout << "[Material] Descriptor set allocated" << std::endl;

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
    
    std::cout << "[Material] UBO descriptor updated" << std::endl;
    
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

void Material::BindTexture(uint32_t slot, VulkanTexture& texture)
{
    if (!texture.IsValid())
        throw std::runtime_error("Material: attempting to bind invalid texture");
    
    std::cout << "[Material] Binding texture to slot " << slot << " (binding " << (1 + slot) << ")" << std::endl;
    
    vk::DescriptorImageInfo imageInfo{};
    imageInfo.sampler     = texture.GetSampler();
    imageInfo.imageView   = texture.GetImageView();
    imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

    vk::WriteDescriptorSet write{};
    write.dstSet          = *descriptorSet;
    write.dstBinding      = 1 + slot;
    write.descriptorCount = 1;
    write.descriptorType  = vk::DescriptorType::eCombinedImageSampler;
    write.pImageInfo      = &imageInfo;

    VulkanInstance::Get().GetDevice().updateDescriptorSets(write, nullptr);
    
    std::cout << "[Material] Texture bound OK" << std::endl;
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

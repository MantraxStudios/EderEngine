#include "VulkanDepthBuffer.h"
#include "VulkanInstance.h"

vk::Format VulkanDepthBuffer::FindDepthFormat()
{
    for (vk::Format candidate : {
        vk::Format::eD32Sfloat,
        vk::Format::eD32SfloatS8Uint,
        vk::Format::eD24UnormS8Uint })
    {
        auto props = VulkanInstance::Get().GetPhysicalDevice()
                     .getFormatProperties(candidate);

        if (props.optimalTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment)
            return candidate;
    }
    throw std::runtime_error("VulkanDepthBuffer: no supported depth format");
}

uint32_t VulkanDepthBuffer::FindMemoryType(uint32_t filter, vk::MemoryPropertyFlags props)
{
    auto memProps = VulkanInstance::Get().GetPhysicalDevice().getMemoryProperties();
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++)
        if ((filter & (1 << i)) && (memProps.memoryTypes[i].propertyFlags & props) == props)
            return i;
    throw std::runtime_error("VulkanDepthBuffer: no suitable memory type");
}

void VulkanDepthBuffer::Create(uint32_t width, uint32_t height)
{
    auto& device = VulkanInstance::Get().GetDevice();
    format = FindDepthFormat();

    vk::ImageCreateInfo imgInfo{};
    imgInfo.imageType   = vk::ImageType::e2D;
    imgInfo.format      = format;
    imgInfo.extent      = vk::Extent3D{ width, height, 1 };
    imgInfo.mipLevels   = 1;
    imgInfo.arrayLayers = 1;
    imgInfo.samples     = vk::SampleCountFlagBits::e1;
    imgInfo.tiling      = vk::ImageTiling::eOptimal;
    imgInfo.usage       = vk::ImageUsageFlagBits::eDepthStencilAttachment;

    image = vk::raii::Image(device, imgInfo);

    auto memReqs = image.getMemoryRequirements();

    vk::MemoryAllocateInfo allocInfo{};
    allocInfo.allocationSize  = memReqs.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memReqs.memoryTypeBits,
        vk::MemoryPropertyFlagBits::eDeviceLocal);

    memory = vk::raii::DeviceMemory(device, allocInfo);
    image.bindMemory(*memory, 0);

    vk::ImageViewCreateInfo viewInfo{};
    viewInfo.image                       = *image;
    viewInfo.viewType                    = vk::ImageViewType::e2D;
    viewInfo.format                      = format;
    viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;

    imageView = vk::raii::ImageView(device, viewInfo);
}

void VulkanDepthBuffer::Recreate(uint32_t width, uint32_t height)
{
    Destroy();
    Create(width, height);
}

void VulkanDepthBuffer::Destroy()
{
    imageView = nullptr;
    memory    = nullptr;
    image     = nullptr;
}

#include "VulkanTexture.h"
#include "VulkanInstance.h"
#include "VulkanBuffer.h"
#include <IO/AssetManager.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

vk::CommandBuffer VulkanTexture::BeginOneTime()
{
    auto& device = VulkanInstance::Get().GetDevice();

    vk::CommandPoolCreateInfo poolInfo{};
    poolInfo.flags            = vk::CommandPoolCreateFlagBits::eTransient;
    poolInfo.queueFamilyIndex = VulkanInstance::Get().GetGraphicsIndex();

    oneTimePool = vk::raii::CommandPool(device, poolInfo);

    vk::CommandBufferAllocateInfo allocInfo{};
    allocInfo.commandPool        = *oneTimePool;
    allocInfo.level              = vk::CommandBufferLevel::ePrimary;
    allocInfo.commandBufferCount = 1;

    auto cmds = (*device).allocateCommandBuffers(allocInfo);
    vk::CommandBuffer cmd = cmds[0];

    vk::CommandBufferBeginInfo beginInfo{};
    beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
    cmd.begin(beginInfo);

    return cmd;
}

void VulkanTexture::EndOneTime(vk::CommandBuffer cmd)
{
    cmd.end();

    vk::SubmitInfo submitInfo{};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers    = &cmd;

    auto& queue = VulkanInstance::Get().GetGraphicsQueue();
    queue.submit(submitInfo, nullptr);
    queue.waitIdle();

    oneTimePool = nullptr;
}

uint32_t VulkanTexture::FindMemoryType(uint32_t filter, vk::MemoryPropertyFlags props)
{
    auto memProps = VulkanInstance::Get().GetPhysicalDevice().getMemoryProperties();
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++)
        if ((filter & (1 << i)) && (memProps.memoryTypes[i].propertyFlags & props) == props)
            return i;
    throw std::runtime_error("VulkanTexture: no suitable memory type");
}

void VulkanTexture::TransitionLayout(vk::CommandBuffer cmd, vk::ImageLayout from, vk::ImageLayout to)
{
    vk::ImageMemoryBarrier barrier{};
    barrier.oldLayout           = from;
    barrier.newLayout           = to;
    barrier.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
    barrier.dstQueueFamilyIndex = vk::QueueFamilyIgnored;
    barrier.image               = *image;
    barrier.subresourceRange    = vk::ImageSubresourceRange{ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 };

    vk::PipelineStageFlags srcStage, dstStage;

    if (from == vk::ImageLayout::eUndefined && to == vk::ImageLayout::eTransferDstOptimal)
    {
        barrier.srcAccessMask = vk::AccessFlagBits::eNone;
        barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
        srcStage = vk::PipelineStageFlagBits::eTopOfPipe;
        dstStage = vk::PipelineStageFlagBits::eTransfer;
    }
    else if (from == vk::ImageLayout::eTransferDstOptimal && to == vk::ImageLayout::eShaderReadOnlyOptimal)
    {
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
        srcStage = vk::PipelineStageFlagBits::eTransfer;
        dstStage = vk::PipelineStageFlagBits::eFragmentShader;
    }
    else
    {
        throw std::runtime_error("VulkanTexture: unsupported layout transition");
    }

    cmd.pipelineBarrier(srcStage, dstStage, {}, {}, {}, barrier);
}

void VulkanTexture::CopyFromBuffer(vk::CommandBuffer cmd, vk::Buffer src, uint32_t width, uint32_t height)
{
    vk::BufferImageCopy region{};
    region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
    region.imageSubresource.layerCount = 1;
    region.imageExtent                 = vk::Extent3D{ width, height, 1 };
    cmd.copyBufferToImage(src, *image, vk::ImageLayout::eTransferDstOptimal, region);
}

void VulkanTexture::CreateImageResources(uint32_t width, uint32_t height)
{
    auto& device = VulkanInstance::Get().GetDevice();

    vk::ImageCreateInfo imgInfo{};
    imgInfo.imageType     = vk::ImageType::e2D;
    imgInfo.format        = vk::Format::eR8G8B8A8Srgb;
    imgInfo.extent        = vk::Extent3D{ width, height, 1 };
    imgInfo.mipLevels     = 1;
    imgInfo.arrayLayers   = 1;
    imgInfo.samples       = vk::SampleCountFlagBits::e1;
    imgInfo.tiling        = vk::ImageTiling::eOptimal;
    imgInfo.usage         = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
    imgInfo.sharingMode   = vk::SharingMode::eExclusive;
    imgInfo.initialLayout = vk::ImageLayout::eUndefined;

    image = vk::raii::Image(device, imgInfo);

    auto memReqs = image.getMemoryRequirements();

    vk::MemoryAllocateInfo allocInfo{};
    allocInfo.allocationSize  = memReqs.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memReqs.memoryTypeBits,
        vk::MemoryPropertyFlagBits::eDeviceLocal);

    memory = vk::raii::DeviceMemory(device, allocInfo);
    image.bindMemory(*memory, 0);

    vk::ImageViewCreateInfo viewInfo{};
    viewInfo.image                           = *image;
    viewInfo.viewType                        = vk::ImageViewType::e2D;
    viewInfo.format                          = vk::Format::eR8G8B8A8Srgb;
    viewInfo.subresourceRange.aspectMask     = vk::ImageAspectFlagBits::eColor;
    viewInfo.subresourceRange.baseMipLevel   = 0;
    viewInfo.subresourceRange.levelCount     = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount     = 1;

    imageView = vk::raii::ImageView(device, viewInfo);
}

void VulkanTexture::CreateSampler()
{
    auto props = VulkanInstance::Get().GetPhysicalDevice().getProperties();

    vk::SamplerCreateInfo samplerInfo{};
    samplerInfo.magFilter               = vk::Filter::eLinear;
    samplerInfo.minFilter               = vk::Filter::eLinear;
    samplerInfo.addressModeU            = vk::SamplerAddressMode::eRepeat;
    samplerInfo.addressModeV            = vk::SamplerAddressMode::eRepeat;
    samplerInfo.addressModeW            = vk::SamplerAddressMode::eRepeat;
    samplerInfo.anisotropyEnable        = vk::True;
    samplerInfo.maxAnisotropy           = props.limits.maxSamplerAnisotropy;
    samplerInfo.borderColor             = vk::BorderColor::eIntOpaqueBlack;
    samplerInfo.unnormalizedCoordinates = vk::False;
    samplerInfo.compareEnable           = vk::False;
    samplerInfo.mipmapMode              = vk::SamplerMipmapMode::eLinear;

    sampler = vk::raii::Sampler(VulkanInstance::Get().GetDevice(), samplerInfo);
}

void VulkanTexture::UploadPixels(const uint8_t* pixels, uint32_t width, uint32_t height)
{
    vk::DeviceSize size = static_cast<vk::DeviceSize>(width) * height * 4;

    VulkanBuffer staging;
    staging.Create(size,
        vk::BufferUsageFlagBits::eTransferSrc,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
    staging.Upload(pixels, size);

    CreateImageResources(width, height);

    auto cmd = BeginOneTime();
    TransitionLayout(cmd, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal);
    CopyFromBuffer(cmd, staging.GetBuffer(), width, height);
    TransitionLayout(cmd, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal);
    EndOneTime(cmd);

    staging.Destroy();
    CreateSampler();
    created = true;
}

void VulkanTexture::LoadFromMemory(const uint8_t* data, size_t size)
{
    int w, h, channels;
    stbi_uc* pixels = stbi_load_from_memory(
        data, static_cast<int>(size), &w, &h, &channels, STBI_rgb_alpha);
    if (!pixels)
        throw std::runtime_error(
            std::string("VulkanTexture: stbi_load_from_memory failed: ") + stbi_failure_reason());

    UploadPixels(pixels, static_cast<uint32_t>(w), static_cast<uint32_t>(h));
    stbi_image_free(pixels);

    std::cout << "[Texture] LoadedFromMemory " << w << "x" << h << std::endl;
}

void VulkanTexture::Load(const std::string& path)
{
    // Route through AssetManager when available.
    auto& am = Krayon::AssetManager::Get();
    if (!am.GetWorkDir().empty() || am.IsCompiled())
    {
        std::vector<uint8_t> bytes = am.GetBytes(path);
        if (!bytes.empty())
        {
            LoadFromMemory(bytes.data(), bytes.size());
            std::cout << "[Texture] Loaded via AssetManager: " << path << std::endl;
            return;
        }
    }

    // Direct disk fallback.
    int w, h, channels;
    stbi_uc* pixels = stbi_load(path.c_str(), &w, &h, &channels, STBI_rgb_alpha);
    if (!pixels)
        throw std::runtime_error("VulkanTexture: failed to load '" + path + "': " + stbi_failure_reason());

    UploadPixels(pixels, static_cast<uint32_t>(w), static_cast<uint32_t>(h));
    stbi_image_free(pixels);

    std::cout << "[Texture] Loaded: " << path << " (" << w << "x" << h << ")" << std::endl;
}

void VulkanTexture::CreateDefault()
{
    uint8_t white[4] = { 255, 255, 255, 255 };
    UploadPixels(white, 1, 1);
}

void VulkanTexture::Destroy()
{
    sampler     = nullptr;
    imageView   = nullptr;
    memory      = nullptr;
    image       = nullptr;
    oneTimePool = nullptr;
    created     = false;
}
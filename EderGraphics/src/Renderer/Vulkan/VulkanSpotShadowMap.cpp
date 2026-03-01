#include "VulkanSpotShadowMap.h"
#include "VulkanInstance.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

vk::Format VulkanSpotShadowMap::FindDepthFormat()
{
    for (vk::Format f : { vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint, vk::Format::eD24UnormS8Uint })
    {
        auto props = VulkanInstance::Get().GetPhysicalDevice().getFormatProperties(f);
        if (props.optimalTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment)
            return f;
    }
    throw std::runtime_error("VulkanSpotShadowMap: no depth format");
}

uint32_t VulkanSpotShadowMap::FindMemoryType(uint32_t filter, vk::MemoryPropertyFlags props)
{
    auto mem = VulkanInstance::Get().GetPhysicalDevice().getMemoryProperties();
    for (uint32_t i = 0; i < mem.memoryTypeCount; i++)
        if ((filter & (1 << i)) && (mem.memoryTypes[i].propertyFlags & props) == props)
            return i;
    throw std::runtime_error("VulkanSpotShadowMap: no memory type");
}

void VulkanSpotShadowMap::Create(uint32_t size)
{
    mapSize = size;
    auto& device = VulkanInstance::Get().GetDevice();
    format = FindDepthFormat();

    vk::ImageCreateInfo ci{};
    ci.imageType   = vk::ImageType::e2D;
    ci.format      = format;
    ci.extent      = vk::Extent3D{ size, size, 1 };
    ci.mipLevels   = 1;
    ci.arrayLayers = MAX_SLOTS;
    ci.samples     = vk::SampleCountFlagBits::e1;
    ci.tiling      = vk::ImageTiling::eOptimal;
    ci.usage       = vk::ImageUsageFlagBits::eDepthStencilAttachment
                   | vk::ImageUsageFlagBits::eSampled;
    depthImage = vk::raii::Image(device, ci);

    auto req = depthImage.getMemoryRequirements();
    vk::MemoryAllocateInfo ai{ req.size,
        FindMemoryType(req.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal) };
    depthMemory = vk::raii::DeviceMemory(device, ai);
    depthImage.bindMemory(*depthMemory, 0);

    for (uint32_t i = 0; i < MAX_SLOTS; i++)
    {
        vk::ImageViewCreateInfo vi{};
        vi.image                           = *depthImage;
        vi.viewType                        = vk::ImageViewType::e2D;
        vi.format                          = format;
        vi.subresourceRange.aspectMask     = vk::ImageAspectFlagBits::eDepth;
        vi.subresourceRange.levelCount     = 1;
        vi.subresourceRange.baseArrayLayer = i;
        vi.subresourceRange.layerCount     = 1;
        layerViews[i]   = vk::raii::ImageView(device, vi);
        layerLayouts[i] = vk::ImageLayout::eUndefined;
    }

    vk::ImageViewCreateInfo vi{};
    vi.image                       = *depthImage;
    vi.viewType                    = vk::ImageViewType::e2DArray;
    vi.format                      = format;
    vi.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
    vi.subresourceRange.levelCount = 1;
    vi.subresourceRange.layerCount = MAX_SLOTS;
    arrayView = vk::raii::ImageView(device, vi);

    vk::SamplerCreateInfo si{};
    si.magFilter    = vk::Filter::eLinear;
    si.minFilter    = vk::Filter::eLinear;
    si.addressModeU = vk::SamplerAddressMode::eClampToBorder;
    si.addressModeV = vk::SamplerAddressMode::eClampToBorder;
    si.addressModeW = vk::SamplerAddressMode::eClampToBorder;
    si.borderColor  = vk::BorderColor::eFloatOpaqueWhite;
    si.mipmapMode   = vk::SamplerMipmapMode::eNearest;
    sampler = vk::raii::Sampler(device, si);

    std::cout << "[Vulkan] SpotShadowMap OK (" << MAX_SLOTS << " slots, " << size << "px)" << std::endl;
}

void VulkanSpotShadowMap::BeginRendering(vk::CommandBuffer cmd, uint32_t slot)
{
    auto& lay = layerLayouts[slot];

    vk::AccessFlags           srcAccess = (lay == vk::ImageLayout::eShaderReadOnlyOptimal)
                                          ? vk::AccessFlagBits::eShaderRead
                                          : vk::AccessFlagBits::eNone;
    vk::PipelineStageFlagBits srcStage  = (lay == vk::ImageLayout::eShaderReadOnlyOptimal)
                                          ? vk::PipelineStageFlagBits::eFragmentShader
                                          : vk::PipelineStageFlagBits::eTopOfPipe;

    vk::ImageMemoryBarrier barrier{};
    barrier.oldLayout           = lay;
    barrier.newLayout           = vk::ImageLayout::eDepthStencilAttachmentOptimal;
    barrier.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
    barrier.dstQueueFamilyIndex = vk::QueueFamilyIgnored;
    barrier.image               = *depthImage;
    barrier.subresourceRange    = { vk::ImageAspectFlagBits::eDepth, 0, 1, slot, 1 };
    barrier.srcAccessMask       = srcAccess;
    barrier.dstAccessMask       = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
    cmd.pipelineBarrier(srcStage, vk::PipelineStageFlagBits::eEarlyFragmentTests,
                        {}, {}, {}, barrier);

    vk::RenderingAttachmentInfo depthAtt{};
    depthAtt.imageView   = *layerViews[slot];
    depthAtt.imageLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
    depthAtt.loadOp      = vk::AttachmentLoadOp::eClear;
    depthAtt.storeOp     = vk::AttachmentStoreOp::eStore;
    depthAtt.clearValue  = vk::ClearDepthStencilValue{ 1.0f, 0 };

    vk::RenderingInfo ri{};
    ri.renderArea       = vk::Rect2D{ {0,0}, {mapSize, mapSize} };
    ri.layerCount       = 1;
    ri.pDepthAttachment = &depthAtt;
    cmd.beginRendering(ri);
    lay = vk::ImageLayout::eDepthStencilAttachmentOptimal;

    vk::Viewport vp{};
    vp.width = vp.height = static_cast<float>(mapSize);
    vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
    cmd.setViewport(0, vp);
    cmd.setScissor(0, vk::Rect2D{ {0,0}, {mapSize, mapSize} });
}

void VulkanSpotShadowMap::EndRendering(vk::CommandBuffer cmd) { cmd.endRendering(); }

void VulkanSpotShadowMap::TransitionToShaderRead(vk::CommandBuffer cmd)
{
    vk::ImageMemoryBarrier barrier{};
    barrier.oldLayout           = vk::ImageLayout::eDepthStencilAttachmentOptimal;
    barrier.newLayout           = vk::ImageLayout::eShaderReadOnlyOptimal;
    barrier.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
    barrier.dstQueueFamilyIndex = vk::QueueFamilyIgnored;
    barrier.image               = *depthImage;
    barrier.subresourceRange    = { vk::ImageAspectFlagBits::eDepth, 0, 1, 0, MAX_SLOTS };
    barrier.srcAccessMask       = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
    barrier.dstAccessMask       = vk::AccessFlagBits::eShaderRead;
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eLateFragmentTests,
                        vk::PipelineStageFlagBits::eFragmentShader,
                        {}, {}, {}, barrier);
    for (auto& l : layerLayouts) l = vk::ImageLayout::eShaderReadOnlyOptimal;
}

glm::mat4 VulkanSpotShadowMap::ComputeMatrix(const glm::vec3& pos, const glm::vec3& dir,
                                              float outerCos, float nearZ, float farZ)
{
    float fov = 2.0f * std::acos(outerCos);  // full cone angle

    glm::vec3 up = (std::abs(dir.y) < 0.99f)
                   ? glm::vec3(0.0f, 1.0f, 0.0f)
                   : glm::vec3(1.0f, 0.0f, 0.0f);

    glm::mat4 view = glm::lookAt(pos, pos + glm::normalize(dir), up);
    glm::mat4 proj = glm::perspective(fov, 1.0f, nearZ, farZ);
    proj[1][1] *= -1.0f;  // Vulkan Y-flip
    return proj * view;
}

void VulkanSpotShadowMap::Destroy()
{
    sampler   = nullptr;
    arrayView = nullptr;
    for (auto& v : layerViews) v = nullptr;
    depthMemory = nullptr;
    depthImage  = nullptr;
}

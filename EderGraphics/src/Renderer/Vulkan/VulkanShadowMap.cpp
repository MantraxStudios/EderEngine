#include "VulkanShadowMap.h"
#include "VulkanInstance.h"
#include <glm/gtc/matrix_transform.hpp>

vk::Format VulkanShadowMap::FindDepthFormat()
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
    throw std::runtime_error("VulkanShadowMap: no supported depth format");
}

uint32_t VulkanShadowMap::FindMemoryType(uint32_t filter, vk::MemoryPropertyFlags props)
{
    auto mem = VulkanInstance::Get().GetPhysicalDevice().getMemoryProperties();
    for (uint32_t i = 0; i < mem.memoryTypeCount; i++)
        if ((filter & (1 << i)) && (mem.memoryTypes[i].propertyFlags & props) == props)
            return i;
    throw std::runtime_error("VulkanShadowMap: no suitable memory type");
}

void VulkanShadowMap::Create(uint32_t size)
{
    mapSize = size;
    auto& device = VulkanInstance::Get().GetDevice();
    format = FindDepthFormat();

    vk::ImageCreateInfo ci{};
    ci.imageType   = vk::ImageType::e2D;
    ci.format      = format;
    ci.extent      = vk::Extent3D{ size, size, 1 };
    ci.mipLevels   = 1;
    ci.arrayLayers = 1;
    ci.samples     = vk::SampleCountFlagBits::e1;
    ci.tiling      = vk::ImageTiling::eOptimal;
    ci.usage       = vk::ImageUsageFlagBits::eDepthStencilAttachment
                   | vk::ImageUsageFlagBits::eSampled;
    depthImage = vk::raii::Image(device, ci);

    {
        auto req = depthImage.getMemoryRequirements();
        vk::MemoryAllocateInfo ai{ req.size,
            FindMemoryType(req.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal) };
        depthMemory = vk::raii::DeviceMemory(device, ai);
        depthImage.bindMemory(*depthMemory, 0);
    }

    vk::ImageViewCreateInfo vi{};
    vi.image                       = *depthImage;
    vi.viewType                    = vk::ImageViewType::e2D;
    vi.format                      = format;
    vi.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
    vi.subresourceRange.levelCount = 1;
    vi.subresourceRange.layerCount = 1;
    depthView = vk::raii::ImageView(device, vi);

    vk::SamplerCreateInfo si{};
    si.magFilter    = vk::Filter::eLinear;
    si.minFilter    = vk::Filter::eLinear;
    si.addressModeU = vk::SamplerAddressMode::eClampToBorder;
    si.addressModeV = vk::SamplerAddressMode::eClampToBorder;
    si.addressModeW = vk::SamplerAddressMode::eClampToBorder;
    si.borderColor  = vk::BorderColor::eFloatOpaqueWhite;
    si.mipmapMode   = vk::SamplerMipmapMode::eNearest;
    sampler = vk::raii::Sampler(device, si);

    layout = vk::ImageLayout::eUndefined;
}

void VulkanShadowMap::BeginRendering(vk::CommandBuffer cmd)
{
    vk::AccessFlags        srcAccess = (layout == vk::ImageLayout::eShaderReadOnlyOptimal)
                                       ? vk::AccessFlagBits::eShaderRead
                                       : vk::AccessFlagBits::eNone;
    vk::PipelineStageFlagBits srcStage = (layout == vk::ImageLayout::eShaderReadOnlyOptimal)
                                       ? vk::PipelineStageFlagBits::eFragmentShader
                                       : vk::PipelineStageFlagBits::eTopOfPipe;

    vk::ImageMemoryBarrier barrier{};
    barrier.oldLayout           = layout;
    barrier.newLayout           = vk::ImageLayout::eDepthStencilAttachmentOptimal;
    barrier.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
    barrier.dstQueueFamilyIndex = vk::QueueFamilyIgnored;
    barrier.image               = *depthImage;
    barrier.subresourceRange    = { vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1 };
    barrier.srcAccessMask       = srcAccess;
    barrier.dstAccessMask       = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
    cmd.pipelineBarrier(srcStage, vk::PipelineStageFlagBits::eEarlyFragmentTests,
                        {}, {}, {}, barrier);

    vk::RenderingAttachmentInfo depthAtt{};
    depthAtt.imageView   = *depthView;
    depthAtt.imageLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
    depthAtt.loadOp      = vk::AttachmentLoadOp::eClear;
    depthAtt.storeOp     = vk::AttachmentStoreOp::eStore;
    depthAtt.clearValue  = vk::ClearDepthStencilValue{ 1.0f, 0 };

    vk::RenderingInfo ri{};
    ri.renderArea       = vk::Rect2D{ {0,0}, {mapSize, mapSize} };
    ri.layerCount       = 1;
    ri.pDepthAttachment = &depthAtt;
    cmd.beginRendering(ri);
    layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

    vk::Viewport vp{};
    vp.width    = static_cast<float>(mapSize);
    vp.height   = static_cast<float>(mapSize);
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    cmd.setViewport(0, vp);
    cmd.setScissor(0, vk::Rect2D{ {0,0}, {mapSize, mapSize} });
}

void VulkanShadowMap::EndRendering(vk::CommandBuffer cmd)
{
    cmd.endRendering();
}

void VulkanShadowMap::TransitionToShaderRead(vk::CommandBuffer cmd)
{
    vk::ImageMemoryBarrier barrier{};
    barrier.oldLayout           = vk::ImageLayout::eDepthStencilAttachmentOptimal;
    barrier.newLayout           = vk::ImageLayout::eShaderReadOnlyOptimal;
    barrier.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
    barrier.dstQueueFamilyIndex = vk::QueueFamilyIgnored;
    barrier.image               = *depthImage;
    barrier.subresourceRange    = { vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1 };
    barrier.srcAccessMask       = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
    barrier.dstAccessMask       = vk::AccessFlagBits::eShaderRead;
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eLateFragmentTests,
                        vk::PipelineStageFlagBits::eFragmentShader,
                        {}, {}, {}, barrier);
    layout = vk::ImageLayout::eShaderReadOnlyOptimal;
}

glm::mat4 VulkanShadowMap::ComputeLightSpaceMatrix(const glm::vec3& lightDir, float sceneDist) const
{
    glm::vec3 up  = (glm::abs(lightDir.y) < 0.99f) ? glm::vec3(0.0f, 1.0f, 0.0f)
                                                     : glm::vec3(1.0f, 0.0f, 0.0f);
    glm::vec3 pos  = -lightDir * sceneDist;
    glm::mat4 view = glm::lookAt(pos, glm::vec3(0.0f), up);
    glm::mat4 proj = glm::ortho(-sceneDist, sceneDist, -sceneDist, sceneDist,
                                0.1f, sceneDist * 2.0f);
    proj[1][1] *= -1.0f;  // Vulkan Y-flip
    return proj * view;
}

void VulkanShadowMap::Destroy()
{
    sampler     = nullptr;
    depthView   = nullptr;
    depthMemory = nullptr;
    depthImage  = nullptr;
}

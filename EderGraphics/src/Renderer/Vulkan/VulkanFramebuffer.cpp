#include "VulkanFramebuffer.h"
#include "VulkanInstance.h"
#include "VulkanSwapchain.h"

uint32_t VulkanFramebuffer::FindMemoryType(uint32_t filter, vk::MemoryPropertyFlags props)
{
    auto mem = VulkanInstance::Get().GetPhysicalDevice().getMemoryProperties();
    for (uint32_t i = 0; i < mem.memoryTypeCount; i++)
        if ((filter & (1 << i)) && (mem.memoryTypes[i].propertyFlags & props) == props)
            return i;
    throw std::runtime_error("VulkanFramebuffer: no suitable memory type");
}

void VulkanFramebuffer::Create(uint32_t width, uint32_t height, vk::Format inColor, vk::Format inDepth)
{
    auto& device = VulkanInstance::Get().GetDevice();
    colorFormat  = inColor;
    depthFormat  = inDepth;
    extent       = vk::Extent2D{ width, height };

    vk::ImageCreateInfo ci{};
    ci.imageType     = vk::ImageType::e2D;
    ci.extent        = vk::Extent3D{ width, height, 1 };
    ci.mipLevels     = 1;
    ci.arrayLayers   = 1;
    ci.samples       = vk::SampleCountFlagBits::e1;
    ci.tiling        = vk::ImageTiling::eOptimal;
    ci.initialLayout = vk::ImageLayout::eUndefined;

    ci.format = colorFormat;
    ci.usage  = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled;
    colorImage = vk::raii::Image(device, ci);
    {
        auto req = colorImage.getMemoryRequirements();
        vk::MemoryAllocateInfo ai{ req.size, FindMemoryType(req.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal) };
        colorMemory = vk::raii::DeviceMemory(device, ai);
        colorImage.bindMemory(*colorMemory, 0);
    }
    vk::ImageViewCreateInfo vi{};
    vi.image                           = *colorImage;
    vi.viewType                        = vk::ImageViewType::e2D;
    vi.format                          = colorFormat;
    vi.subresourceRange                = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 };
    colorView = vk::raii::ImageView(device, vi);

    vk::SamplerCreateInfo si{};
    si.magFilter    = vk::Filter::eLinear;
    si.minFilter    = vk::Filter::eLinear;
    si.addressModeU = vk::SamplerAddressMode::eClampToEdge;
    si.addressModeV = vk::SamplerAddressMode::eClampToEdge;
    si.addressModeW = vk::SamplerAddressMode::eClampToEdge;
    si.mipmapMode   = vk::SamplerMipmapMode::eLinear;
    sampler = vk::raii::Sampler(device, si);

    ci.format = depthFormat;
    ci.usage  = vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled;
    depthImage = vk::raii::Image(device, ci);
    {
        auto req = depthImage.getMemoryRequirements();
        vk::MemoryAllocateInfo ai{ req.size, FindMemoryType(req.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal) };
        depthMemory = vk::raii::DeviceMemory(device, ai);
        depthImage.bindMemory(*depthMemory, 0);
    }
    vi.image                       = *depthImage;
    vi.format                      = depthFormat;
    vi.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
    depthView = vk::raii::ImageView(device, vi);

    colorLayout = vk::ImageLayout::eUndefined;
}

void VulkanFramebuffer::BeginRendering(vk::CommandBuffer cmd)
{
    vk::AccessFlags      srcAccess = colorLayout == vk::ImageLayout::eShaderReadOnlyOptimal
                                     ? vk::AccessFlagBits::eShaderRead : vk::AccessFlagBits::eNone;
    vk::PipelineStageFlags srcStage = colorLayout == vk::ImageLayout::eShaderReadOnlyOptimal
                                     ? vk::PipelineStageFlagBits::eFragmentShader
                                     : vk::PipelineStageFlagBits::eTopOfPipe;

    vk::ImageMemoryBarrier colorBarrier{};
    colorBarrier.oldLayout           = colorLayout;
    colorBarrier.newLayout           = vk::ImageLayout::eColorAttachmentOptimal;
    colorBarrier.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
    colorBarrier.dstQueueFamilyIndex = vk::QueueFamilyIgnored;
    colorBarrier.image               = *colorImage;
    colorBarrier.subresourceRange    = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 };
    colorBarrier.srcAccessMask       = srcAccess;
    colorBarrier.dstAccessMask       = vk::AccessFlagBits::eColorAttachmentWrite;
    cmd.pipelineBarrier(srcStage, vk::PipelineStageFlagBits::eColorAttachmentOutput,
                        {}, {}, {}, colorBarrier);

    vk::ImageMemoryBarrier depthBarrier{};
    depthBarrier.oldLayout           = vk::ImageLayout::eUndefined;
    depthBarrier.newLayout           = vk::ImageLayout::eDepthStencilAttachmentOptimal;
    depthBarrier.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
    depthBarrier.dstQueueFamilyIndex = vk::QueueFamilyIgnored;
    depthBarrier.image               = *depthImage;
    depthBarrier.subresourceRange    = { vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1 };
    depthBarrier.srcAccessMask       = vk::AccessFlagBits::eNone;
    depthBarrier.dstAccessMask       = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
                        vk::PipelineStageFlagBits::eEarlyFragmentTests,
                        {}, {}, {}, depthBarrier);

    vk::RenderingAttachmentInfo colorAtt{};
    colorAtt.imageView   = *colorView;
    colorAtt.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
    colorAtt.loadOp      = vk::AttachmentLoadOp::eClear;
    colorAtt.storeOp     = vk::AttachmentStoreOp::eStore;
    colorAtt.clearValue  = vk::ClearColorValue{ std::array<float,4>{ 0.05f, 0.05f, 0.05f, 1.0f } };

    vk::RenderingAttachmentInfo depthAtt{};
    depthAtt.imageView   = *depthView;
    depthAtt.imageLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
    depthAtt.loadOp      = vk::AttachmentLoadOp::eClear;
    depthAtt.storeOp     = vk::AttachmentStoreOp::eStore;
    depthAtt.clearValue  = vk::ClearDepthStencilValue{ 1.0f, 0 };

    vk::RenderingInfo ri{};
    ri.renderArea           = vk::Rect2D{ vk::Offset2D{ 0, 0 }, extent };
    ri.layerCount           = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments    = &colorAtt;
    ri.pDepthAttachment     = &depthAtt;
    cmd.beginRendering(ri);

    colorLayout = vk::ImageLayout::eColorAttachmentOptimal;

    vk::Viewport vp{};
    vp.width    = static_cast<float>(extent.width);
    vp.height   = static_cast<float>(extent.height);
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    cmd.setViewport(0, vp);
    cmd.setScissor(0, vk::Rect2D{ {0,0}, extent });
}

void VulkanFramebuffer::EndRendering(vk::CommandBuffer cmd)
{
    cmd.endRendering();
}

void VulkanFramebuffer::TransitionToShaderRead(vk::CommandBuffer cmd)
{
    vk::ImageMemoryBarrier barrier{};
    barrier.oldLayout           = vk::ImageLayout::eColorAttachmentOptimal;
    barrier.newLayout           = vk::ImageLayout::eShaderReadOnlyOptimal;
    barrier.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
    barrier.dstQueueFamilyIndex = vk::QueueFamilyIgnored;
    barrier.image               = *colorImage;
    barrier.subresourceRange    = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 };
    barrier.srcAccessMask       = vk::AccessFlagBits::eColorAttachmentWrite;
    barrier.dstAccessMask       = vk::AccessFlagBits::eShaderRead;
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput,
                        vk::PipelineStageFlagBits::eFragmentShader,
                        {}, {}, {}, barrier);
    colorLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

    // Also transition depth so it can be sampled
    vk::ImageMemoryBarrier depthBarrier{};
    depthBarrier.oldLayout           = vk::ImageLayout::eDepthStencilAttachmentOptimal;
    depthBarrier.newLayout           = vk::ImageLayout::eShaderReadOnlyOptimal;
    depthBarrier.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
    depthBarrier.dstQueueFamilyIndex = vk::QueueFamilyIgnored;
    depthBarrier.image               = *depthImage;
    depthBarrier.subresourceRange    = { vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1 };
    depthBarrier.srcAccessMask       = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
    depthBarrier.dstAccessMask       = vk::AccessFlagBits::eShaderRead;
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eLateFragmentTests,
                        vk::PipelineStageFlagBits::eFragmentShader,
                        {}, {}, {}, depthBarrier);
}

void VulkanFramebuffer::Recreate(uint32_t width, uint32_t height)
{
    vk::Format cf = colorFormat;
    vk::Format df = depthFormat;
    Destroy();
    Create(width, height, cf, df);
}

void VulkanFramebuffer::Destroy()
{
    sampler     = nullptr;
    colorView   = nullptr;
    colorMemory = nullptr;
    colorImage  = nullptr;
    depthView   = nullptr;
    depthMemory = nullptr;
    depthImage  = nullptr;
}

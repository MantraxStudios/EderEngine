#include "VulkanGBuffer.h"
#include "VulkanInstance.h"

const std::array<vk::Format, VulkanGBuffer::COLOR_COUNT>& VulkanGBuffer::ColorFormats()
{
    static const std::array<vk::Format, COLOR_COUNT> fmts = {
        vk::Format::eR8G8B8A8Unorm,        // GB0 albedo + emissive
        vk::Format::eR16G16B16A16Sfloat,   // GB1 world normal
        vk::Format::eR8G8B8A8Unorm,        // GB2 roughness / metallic
    };
    return fmts;
}

uint32_t VulkanGBuffer::FindMemoryType(uint32_t filter, vk::MemoryPropertyFlags props)
{
    auto mem = VulkanInstance::Get().GetPhysicalDevice().getMemoryProperties();
    for (uint32_t i = 0; i < mem.memoryTypeCount; i++)
        if ((filter & (1 << i)) && (mem.memoryTypes[i].propertyFlags & props) == props)
            return i;
    throw std::runtime_error("VulkanGBuffer: no suitable memory type");
}

void VulkanGBuffer::Create(uint32_t width, uint32_t height, vk::Format inDepth)
{
    auto& device = VulkanInstance::Get().GetDevice();
    depthFormat  = inDepth;
    extent       = vk::Extent2D{ width, height };

    const auto& fmts = ColorFormats();

    for (uint32_t i = 0; i < COLOR_COUNT; i++)
    {
        vk::ImageCreateInfo ci{};
        ci.imageType     = vk::ImageType::e2D;
        ci.extent        = vk::Extent3D{ width, height, 1 };
        ci.mipLevels     = 1;
        ci.arrayLayers   = 1;
        ci.samples       = vk::SampleCountFlagBits::e1;
        ci.tiling        = vk::ImageTiling::eOptimal;
        ci.initialLayout = vk::ImageLayout::eUndefined;
        ci.format        = fmts[i];
        ci.usage         = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled;
        colorImages[i]   = vk::raii::Image(device, ci);

        auto req = colorImages[i].getMemoryRequirements();
        vk::MemoryAllocateInfo ai{ req.size,
            FindMemoryType(req.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal) };
        colorMemory[i] = vk::raii::DeviceMemory(device, ai);
        colorImages[i].bindMemory(*colorMemory[i], 0);

        vk::ImageViewCreateInfo vi{};
        vi.image            = *colorImages[i];
        vi.viewType         = vk::ImageViewType::e2D;
        vi.format           = fmts[i];
        vi.subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 };
        colorViews[i]  = vk::raii::ImageView(device, vi);
        colorLayouts[i] = vk::ImageLayout::eUndefined;
    }

    // Depth
    {
        vk::ImageCreateInfo ci{};
        ci.imageType   = vk::ImageType::e2D;
        ci.extent      = vk::Extent3D{ width, height, 1 };
        ci.mipLevels   = 1;
        ci.arrayLayers = 1;
        ci.samples     = vk::SampleCountFlagBits::e1;
        ci.tiling      = vk::ImageTiling::eOptimal;
        ci.format      = depthFormat;
        ci.usage       = vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled;
        depthImage = vk::raii::Image(device, ci);

        auto req = depthImage.getMemoryRequirements();
        vk::MemoryAllocateInfo ai{ req.size,
            FindMemoryType(req.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal) };
        depthMemory = vk::raii::DeviceMemory(device, ai);
        depthImage.bindMemory(*depthMemory, 0);

        vk::ImageViewCreateInfo vi{};
        vi.image            = *depthImage;
        vi.viewType         = vk::ImageViewType::e2D;
        vi.format           = depthFormat;
        vi.subresourceRange = { vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1 };
        depthView = vk::raii::ImageView(device, vi);
    }

    // Nearest sampler — the lighting/SSAO passes read exact texels, no filtering.
    vk::SamplerCreateInfo si{};
    si.magFilter    = vk::Filter::eNearest;
    si.minFilter    = vk::Filter::eNearest;
    si.addressModeU = vk::SamplerAddressMode::eClampToEdge;
    si.addressModeV = vk::SamplerAddressMode::eClampToEdge;
    si.addressModeW = vk::SamplerAddressMode::eClampToEdge;
    si.mipmapMode   = vk::SamplerMipmapMode::eNearest;
    sampler = vk::raii::Sampler(device, si);
}

void VulkanGBuffer::BeginRendering(vk::CommandBuffer cmd)
{
    std::array<vk::RenderingAttachmentInfo, COLOR_COUNT> colorAtts{};
    for (uint32_t i = 0; i < COLOR_COUNT; i++)
    {
        vk::AccessFlags        srcA = colorLayouts[i] == vk::ImageLayout::eShaderReadOnlyOptimal
                                      ? vk::AccessFlagBits::eShaderRead : vk::AccessFlagBits::eNone;
        vk::PipelineStageFlags srcS = colorLayouts[i] == vk::ImageLayout::eShaderReadOnlyOptimal
                                      ? vk::PipelineStageFlagBits::eFragmentShader
                                      : vk::PipelineStageFlagBits::eTopOfPipe;
        vk::ImageMemoryBarrier b{};
        b.oldLayout           = colorLayouts[i];
        b.newLayout           = vk::ImageLayout::eColorAttachmentOptimal;
        b.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
        b.dstQueueFamilyIndex = vk::QueueFamilyIgnored;
        b.image               = *colorImages[i];
        b.subresourceRange    = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 };
        b.srcAccessMask       = srcA;
        b.dstAccessMask       = vk::AccessFlagBits::eColorAttachmentWrite;
        cmd.pipelineBarrier(srcS, vk::PipelineStageFlagBits::eColorAttachmentOutput, {}, {}, {}, b);
        colorLayouts[i] = vk::ImageLayout::eColorAttachmentOptimal;

        colorAtts[i].imageView   = *colorViews[i];
        colorAtts[i].imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
        colorAtts[i].loadOp      = vk::AttachmentLoadOp::eClear;
        colorAtts[i].storeOp     = vk::AttachmentStoreOp::eStore;
        colorAtts[i].clearValue  = vk::ClearColorValue{ std::array<float,4>{ 0.0f, 0.0f, 0.0f, 0.0f } };
    }

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
                        vk::PipelineStageFlagBits::eEarlyFragmentTests, {}, {}, {}, depthBarrier);

    vk::RenderingAttachmentInfo depthAtt{};
    depthAtt.imageView   = *depthView;
    depthAtt.imageLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
    depthAtt.loadOp      = vk::AttachmentLoadOp::eClear;
    depthAtt.storeOp     = vk::AttachmentStoreOp::eStore;
    depthAtt.clearValue  = vk::ClearDepthStencilValue{ 1.0f, 0 };

    vk::RenderingInfo ri{};
    ri.renderArea           = vk::Rect2D{ vk::Offset2D{ 0, 0 }, extent };
    ri.layerCount           = 1;
    ri.colorAttachmentCount = COLOR_COUNT;
    ri.pColorAttachments    = colorAtts.data();
    ri.pDepthAttachment     = &depthAtt;
    cmd.beginRendering(ri);

    vk::Viewport vp{};
    vp.width    = static_cast<float>(extent.width);
    vp.height   = static_cast<float>(extent.height);
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    cmd.setViewport(0, vp);
    cmd.setScissor(0, vk::Rect2D{ {0,0}, extent });
}

void VulkanGBuffer::EndRendering(vk::CommandBuffer cmd)
{
    cmd.endRendering();
}

void VulkanGBuffer::TransitionToShaderRead(vk::CommandBuffer cmd)
{
    for (uint32_t i = 0; i < COLOR_COUNT; i++)
    {
        vk::ImageMemoryBarrier b{};
        b.oldLayout           = vk::ImageLayout::eColorAttachmentOptimal;
        b.newLayout           = vk::ImageLayout::eShaderReadOnlyOptimal;
        b.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
        b.dstQueueFamilyIndex = vk::QueueFamilyIgnored;
        b.image               = *colorImages[i];
        b.subresourceRange    = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 };
        b.srcAccessMask       = vk::AccessFlagBits::eColorAttachmentWrite;
        b.dstAccessMask       = vk::AccessFlagBits::eShaderRead;
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput,
                            vk::PipelineStageFlagBits::eFragmentShader, {}, {}, {}, b);
        colorLayouts[i] = vk::ImageLayout::eShaderReadOnlyOptimal;
    }

    vk::ImageMemoryBarrier db{};
    db.oldLayout           = vk::ImageLayout::eDepthStencilAttachmentOptimal;
    db.newLayout           = vk::ImageLayout::eShaderReadOnlyOptimal;
    db.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
    db.dstQueueFamilyIndex = vk::QueueFamilyIgnored;
    db.image               = *depthImage;
    db.subresourceRange    = { vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1 };
    db.srcAccessMask       = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
    db.dstAccessMask       = vk::AccessFlagBits::eShaderRead;
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eLateFragmentTests,
                        vk::PipelineStageFlagBits::eFragmentShader, {}, {}, {}, db);
}

void VulkanGBuffer::Recreate(uint32_t width, uint32_t height)
{
    vk::Format df = depthFormat;
    Destroy();
    Create(width, height, df);
}

void VulkanGBuffer::Destroy()
{
    sampler = nullptr;
    for (auto& v : colorViews)  v = nullptr;
    for (auto& m : colorMemory) m = nullptr;
    for (auto& i : colorImages) i = nullptr;
    depthView   = nullptr;
    depthMemory = nullptr;
    depthImage  = nullptr;
}

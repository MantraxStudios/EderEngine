#include "VulkanPointShadowMap.h"
#include "VulkanInstance.h"
#include <glm/gtc/matrix_transform.hpp>

vk::Format VulkanPointShadowMap::FindDepthFormat()
{
    for (vk::Format f : { vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint, vk::Format::eD24UnormS8Uint })
    {
        auto props = VulkanInstance::Get().GetPhysicalDevice().getFormatProperties(f);
        if (props.optimalTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment)
            return f;
    }
    throw std::runtime_error("VulkanPointShadowMap: no depth format");
}

uint32_t VulkanPointShadowMap::FindMemoryType(uint32_t filter, vk::MemoryPropertyFlags props)
{
    auto mem = VulkanInstance::Get().GetPhysicalDevice().getMemoryProperties();
    for (uint32_t i = 0; i < mem.memoryTypeCount; i++)
        if ((filter & (1 << i)) && (mem.memoryTypes[i].propertyFlags & props) == props)
            return i;
    throw std::runtime_error("VulkanPointShadowMap: no memory type");
}

void VulkanPointShadowMap::Create(uint32_t size)
{
    mapSize = size;
    auto& device = VulkanInstance::Get().GetDevice();
    format = FindDepthFormat();

    uint32_t totalLayers = MAX_SLOTS * FACE_COUNT;  // 4 * 6 = 24

    vk::ImageCreateInfo ci{};
    ci.flags       = vk::ImageCreateFlagBits::eCubeCompatible;
    ci.imageType   = vk::ImageType::e2D;
    ci.format      = format;
    ci.extent      = vk::Extent3D{ size, size, 1 };
    ci.mipLevels   = 1;
    ci.arrayLayers = totalLayers;
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

    // Per-face 2D views for rendering
    for (uint32_t i = 0; i < totalLayers; i++)
    {
        vk::ImageViewCreateInfo vi{};
        vi.image                           = *depthImage;
        vi.viewType                        = vk::ImageViewType::e2D;
        vi.format                          = format;
        vi.subresourceRange.aspectMask     = vk::ImageAspectFlagBits::eDepth;
        vi.subresourceRange.levelCount     = 1;
        vi.subresourceRange.baseArrayLayer = i;
        vi.subresourceRange.layerCount     = 1;
        faceViews[i]   = vk::raii::ImageView(device, vi);
        faceLayouts[i] = vk::ImageLayout::eUndefined;
    }

    // Cube-array view for sampling
    vk::ImageViewCreateInfo vi{};
    vi.image                       = *depthImage;
    vi.viewType                    = vk::ImageViewType::eCubeArray;
    vi.format                      = format;
    vi.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
    vi.subresourceRange.levelCount = 1;
    vi.subresourceRange.layerCount = totalLayers;
    cubeArrayView = vk::raii::ImageView(device, vi);

    vk::SamplerCreateInfo si{};
    si.magFilter    = vk::Filter::eNearest;
    si.minFilter    = vk::Filter::eNearest;
    si.addressModeU = vk::SamplerAddressMode::eClampToEdge;
    si.addressModeV = vk::SamplerAddressMode::eClampToEdge;
    si.addressModeW = vk::SamplerAddressMode::eClampToEdge;
    si.mipmapMode   = vk::SamplerMipmapMode::eNearest;
    sampler = vk::raii::Sampler(device, si);

    std::cout << "[Vulkan] PointShadowMap OK (" << MAX_SLOTS << " slots, " << size << "px)" << std::endl;
}

void VulkanPointShadowMap::BeginRendering(vk::CommandBuffer cmd, uint32_t slot, uint32_t face)
{
    uint32_t layer = slot * FACE_COUNT + face;
    auto& lay = faceLayouts[layer];

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
    barrier.subresourceRange    = { vk::ImageAspectFlagBits::eDepth, 0, 1, layer, 1 };
    barrier.srcAccessMask       = srcAccess;
    barrier.dstAccessMask       = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
    cmd.pipelineBarrier(srcStage, vk::PipelineStageFlagBits::eEarlyFragmentTests,
                        {}, {}, {}, barrier);

    vk::RenderingAttachmentInfo depthAtt{};
    depthAtt.imageView   = *faceViews[layer];
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

void VulkanPointShadowMap::EndRendering(vk::CommandBuffer cmd) { cmd.endRendering(); }

void VulkanPointShadowMap::TransitionToShaderRead(vk::CommandBuffer cmd)
{
    uint32_t totalLayers = MAX_SLOTS * FACE_COUNT;
    vk::ImageMemoryBarrier barrier{};
    barrier.oldLayout           = vk::ImageLayout::eDepthStencilAttachmentOptimal;
    barrier.newLayout           = vk::ImageLayout::eShaderReadOnlyOptimal;
    barrier.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
    barrier.dstQueueFamilyIndex = vk::QueueFamilyIgnored;
    barrier.image               = *depthImage;
    barrier.subresourceRange    = { vk::ImageAspectFlagBits::eDepth, 0, 1, 0, totalLayers };
    barrier.srcAccessMask       = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
    barrier.dstAccessMask       = vk::AccessFlagBits::eShaderRead;
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eLateFragmentTests,
                        vk::PipelineStageFlagBits::eFragmentShader,
                        {}, {}, {}, barrier);
    for (auto& l : faceLayouts) l = vk::ImageLayout::eShaderReadOnlyOptimal;
}

std::array<glm::mat4, 6> VulkanPointShadowMap::ComputeFaceMatrices(
    const glm::vec3& pos, float nearZ, float farZ)
{
    // 90° FOV cube-face projections with Vulkan Y-flip
    glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, nearZ, farZ);
    proj[1][1] *= -1.0f;

    // Vulkan cubemap face order: +X, -X, +Y, -Y, +Z, -Z
    static const glm::vec3 dirs[6] = {
        { 1, 0, 0}, {-1, 0, 0},
        { 0, 1, 0}, { 0,-1, 0},
        { 0, 0, 1}, { 0, 0,-1}
    };
    static const glm::vec3 ups[6] = {
        { 0,-1, 0}, { 0,-1, 0},
        { 0, 0, 1}, { 0, 0,-1},
        { 0,-1, 0}, { 0,-1, 0}
    };

    std::array<glm::mat4, 6> mats{};
    for (int f = 0; f < 6; f++)
        mats[f] = proj * glm::lookAt(pos, pos + dirs[f], ups[f]);
    return mats;
}

void VulkanPointShadowMap::Destroy()
{
    sampler       = nullptr;
    cubeArrayView = nullptr;
    for (auto& v : faceViews) v = nullptr;
    depthMemory = nullptr;
    depthImage  = nullptr;
}

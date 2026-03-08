#include "VulkanShadowMap.h"
#include "VulkanInstance.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

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
    ci.arrayLayers = NUM_CASCADES;
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

    // Per-cascade layer views (for rendering one layer at a time)
    for (uint32_t i = 0; i < NUM_CASCADES; i++)
    {
        vk::ImageViewCreateInfo vi{};
        vi.image                           = *depthImage;
        vi.viewType                        = vk::ImageViewType::e2D;
        vi.format                          = format;
        vi.subresourceRange.aspectMask     = vk::ImageAspectFlagBits::eDepth;
        vi.subresourceRange.levelCount     = 1;
        vi.subresourceRange.baseArrayLayer = i;
        vi.subresourceRange.layerCount     = 1;
        layerViews[i]  = vk::raii::ImageView(device, vi);
        layerLayouts[i] = vk::ImageLayout::eUndefined;
    }

    // Full 2D-array view for shader sampling
    {
        vk::ImageViewCreateInfo vi{};
        vi.image                       = *depthImage;
        vi.viewType                    = vk::ImageViewType::e2DArray;
        vi.format                      = format;
        vi.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
        vi.subresourceRange.levelCount = 1;
        vi.subresourceRange.layerCount = NUM_CASCADES;
        arrayView = vk::raii::ImageView(device, vi);
    }

    vk::SamplerCreateInfo si{};
    si.magFilter    = vk::Filter::eLinear;
    si.minFilter    = vk::Filter::eLinear;
    si.addressModeU = vk::SamplerAddressMode::eClampToBorder;
    si.addressModeV = vk::SamplerAddressMode::eClampToBorder;
    si.addressModeW = vk::SamplerAddressMode::eClampToBorder;
    si.borderColor  = vk::BorderColor::eFloatOpaqueWhite;
    si.mipmapMode   = vk::SamplerMipmapMode::eNearest;
    sampler = vk::raii::Sampler(device, si);
}

void VulkanShadowMap::BeginRendering(vk::CommandBuffer cmd, uint32_t cascadeIndex)
{
    auto& lay = layerLayouts[cascadeIndex];

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
    barrier.subresourceRange    = { vk::ImageAspectFlagBits::eDepth, 0, 1, cascadeIndex, 1 };
    barrier.srcAccessMask       = srcAccess;
    barrier.dstAccessMask       = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
    cmd.pipelineBarrier(srcStage, vk::PipelineStageFlagBits::eEarlyFragmentTests,
                        {}, {}, {}, barrier);

    vk::RenderingAttachmentInfo depthAtt{};
    depthAtt.imageView   = *layerViews[cascadeIndex];
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
    // Transition all cascade layers at once after all shadow passes are done
    vk::ImageMemoryBarrier barrier{};
    barrier.oldLayout           = vk::ImageLayout::eDepthStencilAttachmentOptimal;
    barrier.newLayout           = vk::ImageLayout::eShaderReadOnlyOptimal;
    barrier.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
    barrier.dstQueueFamilyIndex = vk::QueueFamilyIgnored;
    barrier.image               = *depthImage;
    barrier.subresourceRange    = { vk::ImageAspectFlagBits::eDepth, 0, 1, 0, NUM_CASCADES };
    barrier.srcAccessMask       = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
    barrier.dstAccessMask       = vk::AccessFlagBits::eShaderRead;
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eLateFragmentTests,
                        vk::PipelineStageFlagBits::eFragmentShader,
                        {}, {}, {}, barrier);
    for (auto& l : layerLayouts) l = vk::ImageLayout::eShaderReadOnlyOptimal;
}

void VulkanShadowMap::ComputeCascades(
    const glm::mat4& camView,
    const glm::mat4& camProj,
    const glm::vec3& lightDir,
    float nearPlane, float farPlane,
    glm::mat4 outMatrices[NUM_CASCADES],
    glm::vec4& outSplits) const
{
    // Practical split scheme (log + linear blend)
    float lambda = 0.85f;
    float splits[NUM_CASCADES + 1];
    splits[0] = nearPlane;
    for (int i = 1; i <= (int)NUM_CASCADES; i++)
    {
        float t   = float(i) / float(NUM_CASCADES);
        float log = nearPlane * std::pow(farPlane / nearPlane, t);
        float uni = nearPlane + (farPlane - nearPlane) * t;
        splits[i] = lambda * log + (1.0f - lambda) * uni;
    }
    outSplits = glm::vec4(splits[1], splits[2], splits[3], splits[4]);

    // Full frustum corners in world space via inverse(proj * view)
    glm::mat4 invVP = glm::inverse(camProj * camView);
    glm::vec3 frustumCorners[8];
    {
        int idx = 0;
        // z=0: near plane corners [0..3], z=1: far plane corners [4..7]
        for (int z = 0; z < 2; z++)
        for (int x = 0; x < 2; x++)
        for (int y = 0; y < 2; y++)
        {
            glm::vec4 pt = invVP * glm::vec4(
                x * 2.0f - 1.0f,
                y * 2.0f - 1.0f,
                static_cast<float>(z),
                1.0f);
            frustumCorners[idx++] = glm::vec3(pt) / pt.w;
        }
    }

    // Extract half-FOV tangents from the projection matrix (GLM perspective, Vulkan Y-flip).
    // camProj[1][1] = -1/tan(fovY/2) and camProj[0][0] = -camProj[1][1]/aspect.
    // These are constant per camera setup, so radii computed from them are stable.
    float tanHalfFovY = 1.0f / std::abs(camProj[1][1]);
    float tanHalfFovX = 1.0f / std::abs(camProj[0][0]);

    glm::vec3 up = (std::abs(lightDir.y) < 0.99f)
        ? glm::vec3(0.0f, 1.0f, 0.0f)
        : glm::vec3(1.0f, 0.0f, 0.0f);

    for (int c = 0; c < (int)NUM_CASCADES; c++)
    {
        float tNear = (splits[c]     - nearPlane) / (farPlane - nearPlane);
        float tFar  = (splits[c + 1] - nearPlane) / (farPlane - nearPlane);

        // Lerp between near/far frustum corners to get sub-frustum for this cascade
        glm::vec3 corners[8];
        for (int i = 0; i < 4; i++)
        {
            glm::vec3 ray   = frustumCorners[i + 4] - frustumCorners[i];
            corners[i]     = frustumCorners[i] + ray * tNear;
            corners[i + 4] = frustumCorners[i] + ray * tFar;
        }

        // Sphere center: average of sub-frustum corners (follows camera, correct)
        glm::vec3 center(0.0f);
        for (int i = 0; i < 8; i++) center += corners[i];
        center /= 8.0f;

        // Stable radius: derived purely from cascade depth range and camera FOV.
        // Does NOT depend on camera orientation, so it is constant between frames
        // and the texel grid never shifts when the camera rotates — eliminates shadow swimming.
        float farC     = splits[c + 1];
        float halfDepth = (splits[c + 1] - splits[c]) * 0.5f;
        float farHalfH  = farC * tanHalfFovY;
        float farHalfW  = farC * tanHalfFovX;
        float radius    = std::sqrt(farHalfW * farHalfW + farHalfH * farHalfH + halfDepth * halfDepth);
        radius = std::ceil(radius * 16.0f) / 16.0f;

        // Texel snapping: snap center to shadow texel grid to eliminate edge crawling
        float texelSize = (radius * 2.0f) / float(mapSize);

        // Build a temporary view along light direction to snap in light space
        glm::mat4 tmpView = glm::lookAt(glm::vec3(0.0f), lightDir, up);
        glm::vec4 cLS     = tmpView * glm::vec4(center, 1.0f);
        cLS.x = std::floor(cLS.x / texelSize) * texelSize;
        cLS.y = std::floor(cLS.y / texelSize) * texelSize;
        glm::vec3 centerSnapped = glm::vec3(glm::inverse(tmpView) * cLS);

        glm::mat4 lightView = glm::lookAt(
            centerSnapped - lightDir * (radius + 1.0f),
            centerSnapped, up);
        glm::mat4 lightProj = glm::ortho(
            -radius, radius, -radius, radius,
            0.0f, radius * 2.0f + 1.0f);
        lightProj[1][1] *= -1.0f;  // Vulkan Y-flip

        outMatrices[c] = lightProj * lightView;
    }
}

void VulkanShadowMap::Destroy()
{
    sampler   = nullptr;
    arrayView = nullptr;
    for (auto& v : layerViews) v = nullptr;
    depthMemory = nullptr;
    depthImage  = nullptr;
}

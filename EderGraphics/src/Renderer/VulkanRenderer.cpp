#include "VulkanRenderer.h"
#include "Renderer/Vulkan/VulkanInstance.h"
#include "Renderer/Vulkan/VulkanSwapchain.h"
#ifdef __ANDROID__
#include <android/log.h>
#define VKLOG(...) __android_log_print(ANDROID_LOG_DEBUG, "EderVKRend", __VA_ARGS__)
#else
#define VKLOG(...) do {} while(0)
#endif

VulkanRenderer& VulkanRenderer::Get()
{
    static VulkanRenderer instance;
    return instance;
}

void VulkanRenderer::Init()
{
    CreateCommandPool();
    CreateCommandBuffers();
    CreateSyncObjects();

    auto ext = VulkanSwapchain::Get().GetExtent();
    VKLOG("Init: creating depth buffer %ux%u", ext.width, ext.height);
    depthBuffer.Create(ext.width, ext.height);
    VKLOG("Init: depthBuffer created, format=%d view=%p",
        (int)depthBuffer.GetFormat(),
        (void*)(VkImageView)depthBuffer.GetImageView());

    // Load vkCmdBeginRendering / vkCmdEndRendering with KHR extension fallback.
    // On some Android devices the Vulkan 1.3 core name is not returned by
    // vkGetDeviceProcAddr even though the KHR extension alias is available.
    VkDevice rawDevice = *VulkanInstance::Get().GetDevice();
    pfnCmdBeginRendering = reinterpret_cast<PFN_vkCmdBeginRendering>(
        vkGetDeviceProcAddr(rawDevice, "vkCmdBeginRendering"));
    if (!pfnCmdBeginRendering)
        pfnCmdBeginRendering = reinterpret_cast<PFN_vkCmdBeginRendering>(
            vkGetDeviceProcAddr(rawDevice, "vkCmdBeginRenderingKHR"));
    VKLOG("Init: pfnCmdBeginRendering=%p", (void*)pfnCmdBeginRendering);

    pfnCmdEndRendering = reinterpret_cast<PFN_vkCmdEndRendering>(
        vkGetDeviceProcAddr(rawDevice, "vkCmdEndRendering"));
    if (!pfnCmdEndRendering)
        pfnCmdEndRendering = reinterpret_cast<PFN_vkCmdEndRendering>(
            vkGetDeviceProcAddr(rawDevice, "vkCmdEndRenderingKHR"));
    VKLOG("Init: pfnCmdEndRendering=%p", (void*)pfnCmdEndRendering);

    if (!pfnCmdBeginRendering || !pfnCmdEndRendering)
        throw std::runtime_error("vkCmdBeginRendering / vkCmdEndRendering not available!");

    std::cout << "[Vulkan] Renderer OK" << std::endl;
}

void VulkanRenderer::CreateCommandPool()
{
    vk::CommandPoolCreateInfo poolInfo{};
    poolInfo.flags            = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
    poolInfo.queueFamilyIndex = VulkanInstance::Get().GetGraphicsIndex();
    commandPool = vk::raii::CommandPool(VulkanInstance::Get().GetDevice(), poolInfo);
}

void VulkanRenderer::CreateCommandBuffers()
{
    vk::CommandBufferAllocateInfo allocInfo{};
    allocInfo.commandPool        = *commandPool;
    allocInfo.level              = vk::CommandBufferLevel::ePrimary;
    allocInfo.commandBufferCount = MAX_FRAMES_IN_FLIGHT;
    commandBuffers = VulkanInstance::Get().GetDevice().allocateCommandBuffers(allocInfo);
}

void VulkanRenderer::CreateSyncObjects()
{
    vk::SemaphoreCreateInfo semaphoreInfo{};
    vk::FenceCreateInfo     fenceInfo{};
    fenceInfo.flags = vk::FenceCreateFlagBits::eSignaled;

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        imageAvailable.emplace_back(VulkanInstance::Get().GetDevice(), semaphoreInfo);
        renderFinished.emplace_back(VulkanInstance::Get().GetDevice(), semaphoreInfo);
        inFlightFences.emplace_back(VulkanInstance::Get().GetDevice(), fenceInfo);
    }
}

void VulkanRenderer::BeginFrame()
{
    auto& device    = VulkanInstance::Get().GetDevice();
    auto& swapchain = VulkanSwapchain::Get();

    device.waitForFences(*inFlightFences[currentFrame], vk::True, UINT64_MAX);

    uint32_t idx = 0;
    try
    {
        auto [acquireResult, acquiredIdx] = swapchain.GetSwapchain().acquireNextImage(
            UINT64_MAX, *imageAvailable[currentFrame], nullptr);

        if (acquireResult == vk::Result::eSuboptimalKHR || framebufferResized)
        {
            framebufferResized = false;
            device.waitIdle();
            swapchain.Recreate(window);
            auto ext = swapchain.GetExtent();
            depthBuffer.Recreate(ext.width, ext.height);
            return;
        }

        idx = acquiredIdx;
    }
    catch (const vk::OutOfDateKHRError&)
    {
        device.waitIdle();
        swapchain.Recreate(window);
        auto ext = swapchain.GetExtent();
        depthBuffer.Recreate(ext.width, ext.height);
        return;
    }

    device.resetFences(*inFlightFences[currentFrame]);
    imageIndex = idx;

    auto& cmd = commandBuffers[currentFrame];
    cmd.reset();
    cmd.begin(vk::CommandBufferBeginInfo{});
    frameStarted = true;
}

void VulkanRenderer::BeginMainPass()
{
    auto& swapchain = VulkanSwapchain::Get();
    auto& cmd       = commandBuffers[currentFrame];

    VKLOG("BeginMainPass: imageIndex=%u imageCount=%zu depthView=%p",
        imageIndex,
        swapchain.GetImages().size(),
        (void*)(VkImageView)depthBuffer.GetImageView());

    vk::ImageMemoryBarrier colorBarrier{};
    colorBarrier.oldLayout           = vk::ImageLayout::eUndefined;
    colorBarrier.newLayout           = vk::ImageLayout::eColorAttachmentOptimal;
    colorBarrier.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
    colorBarrier.dstQueueFamilyIndex = vk::QueueFamilyIgnored;
    colorBarrier.image               = swapchain.GetImages()[imageIndex];
    colorBarrier.subresourceRange    = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 };
    colorBarrier.srcAccessMask       = vk::AccessFlagBits::eNone;
    colorBarrier.dstAccessMask       = vk::AccessFlagBits::eColorAttachmentWrite;
    VKLOG("BeginMainPass: calling pipelineBarrier");
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
                        vk::PipelineStageFlagBits::eColorAttachmentOutput,
                        {}, {}, {}, colorBarrier);

    vk::RenderingAttachmentInfo colorAttachment{};
    colorAttachment.imageView   = *swapchain.GetImageViews()[imageIndex];
    colorAttachment.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
    colorAttachment.loadOp      = vk::AttachmentLoadOp::eClear;
    colorAttachment.storeOp     = vk::AttachmentStoreOp::eStore;
    colorAttachment.clearValue  = vk::ClearColorValue{ std::array<float,4>{ 0.1f, 0.1f, 0.1f, 1.0f } };

    vk::RenderingAttachmentInfo depthAttachment{};
    depthAttachment.imageView   = depthBuffer.GetImageView();
    depthAttachment.imageLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
    depthAttachment.loadOp      = vk::AttachmentLoadOp::eClear;
    depthAttachment.storeOp     = vk::AttachmentStoreOp::eDontCare;
    depthAttachment.clearValue  = vk::ClearDepthStencilValue{ 1.0f, 0 };

    vk::RenderingInfo renderingInfo{};
    renderingInfo.renderArea.extent    = swapchain.GetExtent();
    renderingInfo.layerCount           = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments    = &colorAttachment;
    renderingInfo.pDepthAttachment     = &depthAttachment;

    VKLOG("BeginMainPass: calling beginRendering extent=%ux%u",
        swapchain.GetExtent().width, swapchain.GetExtent().height);
    pfnCmdBeginRendering(*cmd, reinterpret_cast<const VkRenderingInfo*>(&renderingInfo));

    VKLOG("BeginMainPass: setting viewport/scissor");
    vk::Viewport vp{};
    vp.width    = static_cast<float>(swapchain.GetExtent().width);
    vp.height   = static_cast<float>(swapchain.GetExtent().height);
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    cmd.setViewport(0, vp);
    cmd.setScissor(0, vk::Rect2D{ vk::Offset2D{ 0, 0 }, swapchain.GetExtent() });
    VKLOG("BeginMainPass: done");
}

void VulkanRenderer::EndFrame()
{
    if (!frameStarted) return;
    frameStarted = false;

    auto& swapchain = VulkanSwapchain::Get();
    auto& cmd       = commandBuffers[currentFrame];

    pfnCmdEndRendering(*cmd);

    vk::ImageMemoryBarrier barrier{};
    barrier.oldLayout           = vk::ImageLayout::eColorAttachmentOptimal;
    barrier.newLayout           = vk::ImageLayout::ePresentSrcKHR;
    barrier.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
    barrier.dstQueueFamilyIndex = vk::QueueFamilyIgnored;
    barrier.image               = swapchain.GetImages()[imageIndex];
    barrier.subresourceRange    = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 };
    barrier.srcAccessMask       = vk::AccessFlagBits::eColorAttachmentWrite;
    barrier.dstAccessMask       = vk::AccessFlagBits::eNone;

    cmd.pipelineBarrier(
        vk::PipelineStageFlagBits::eColorAttachmentOutput,
        vk::PipelineStageFlagBits::eBottomOfPipe,
        {}, {}, {}, barrier);

    cmd.end();

    vk::PipelineStageFlags waitStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;

    vk::SubmitInfo submitInfo{};
    submitInfo.waitSemaphoreCount   = 1;
    submitInfo.pWaitSemaphores      = &*imageAvailable[currentFrame];
    submitInfo.pWaitDstStageMask    = &waitStage;
    submitInfo.commandBufferCount   = 1;
    submitInfo.pCommandBuffers      = &*cmd;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores    = &*renderFinished[currentFrame];

    VulkanInstance::Get().GetGraphicsQueue().submit(submitInfo, *inFlightFences[currentFrame]);

    vk::PresentInfoKHR presentInfo{};
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores    = &*renderFinished[currentFrame];
    presentInfo.swapchainCount     = 1;
    presentInfo.pSwapchains        = &*swapchain.GetSwapchain();
    presentInfo.pImageIndices      = &imageIndex;

    try
    {
        auto presentResult = VulkanInstance::Get().GetPresentQueue().presentKHR(presentInfo);
        if (presentResult == vk::Result::eSuboptimalKHR)
        {
            VulkanInstance::Get().GetDevice().waitIdle();
            swapchain.Recreate(window);
            auto ext = swapchain.GetExtent();
            depthBuffer.Recreate(ext.width, ext.height);
        }
    }
    catch (const vk::OutOfDateKHRError&)
    {
        VulkanInstance::Get().GetDevice().waitIdle();
        swapchain.Recreate(window);
        auto ext = swapchain.GetExtent();
        depthBuffer.Recreate(ext.width, ext.height);
    }

    currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void VulkanRenderer::RecreateSwapchainResources(NativeWindow* w)
{
    window = w;
    auto& swapchain = VulkanSwapchain::Get();
    swapchain.Recreate(w);
    auto ext = swapchain.GetExtent();
    depthBuffer.Recreate(ext.width, ext.height);
}

void VulkanRenderer::Shutdown()
{
    VulkanInstance::Get().GetDevice().waitIdle();
    depthBuffer.Destroy();
}

#include "VulkanRenderer.h"
#include "Renderer/Vulkan/VulkanInstance.h"
#include "Renderer/Vulkan/VulkanSwapchain.h"

void VulkanRenderer::Init()
{
    CreateCommandPool();
    CreateCommandBuffers();
    CreateSyncObjects();
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
            return;
        }

        idx = acquiredIdx;
    }
    catch (const vk::OutOfDateKHRError&)
    {
        device.waitIdle();
        swapchain.Recreate(window);
        return;
    }

    device.resetFences(*inFlightFences[currentFrame]);
    imageIndex = idx;

    auto& cmd = commandBuffers[currentFrame];
    cmd.reset();

    vk::CommandBufferBeginInfo beginInfo{};
    cmd.begin(beginInfo);

    vk::ImageMemoryBarrier barrier{};
    barrier.oldLayout           = vk::ImageLayout::eUndefined;
    barrier.newLayout           = vk::ImageLayout::eColorAttachmentOptimal;
    barrier.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
    barrier.dstQueueFamilyIndex = vk::QueueFamilyIgnored;
    barrier.image               = swapchain.GetImages()[imageIndex];
    barrier.subresourceRange.aspectMask     = vk::ImageAspectFlagBits::eColor;
    barrier.subresourceRange.baseMipLevel   = 0;
    barrier.subresourceRange.levelCount     = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount     = 1;
    barrier.srcAccessMask = vk::AccessFlagBits::eNone;
    barrier.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;

    cmd.pipelineBarrier(
        vk::PipelineStageFlagBits::eTopOfPipe,
        vk::PipelineStageFlagBits::eColorAttachmentOutput,
        {}, {}, {}, barrier);

    vk::RenderingAttachmentInfo colorAttachment{};
    colorAttachment.imageView   = *swapchain.GetImageViews()[imageIndex];
    colorAttachment.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
    colorAttachment.loadOp      = vk::AttachmentLoadOp::eClear;
    colorAttachment.storeOp     = vk::AttachmentStoreOp::eStore;
    colorAttachment.clearValue  = vk::ClearColorValue{ std::array<float,4>{ 0.1f, 0.1f, 0.1f, 1.0f } };

    vk::RenderingInfo renderingInfo{};
    renderingInfo.renderArea.offset        = vk::Offset2D{ 0, 0 };
    renderingInfo.renderArea.extent        = swapchain.GetExtent();
    renderingInfo.layerCount               = 1;
    renderingInfo.colorAttachmentCount     = 1;
    renderingInfo.pColorAttachments        = &colorAttachment;

    cmd.beginRendering(renderingInfo);
    frameStarted = true;
}

void VulkanRenderer::EndFrame()
{
    if (!frameStarted) return;
    frameStarted = false;

    auto& swapchain = VulkanSwapchain::Get();
    auto& cmd       = commandBuffers[currentFrame];

    cmd.endRendering();

    vk::ImageMemoryBarrier barrier{};
    barrier.oldLayout           = vk::ImageLayout::eColorAttachmentOptimal;
    barrier.newLayout           = vk::ImageLayout::ePresentSrcKHR;
    barrier.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
    barrier.dstQueueFamilyIndex = vk::QueueFamilyIgnored;
    barrier.image               = swapchain.GetImages()[imageIndex];
    barrier.subresourceRange.aspectMask     = vk::ImageAspectFlagBits::eColor;
    barrier.subresourceRange.baseMipLevel   = 0;
    barrier.subresourceRange.levelCount     = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount     = 1;
    barrier.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
    barrier.dstAccessMask = vk::AccessFlagBits::eNone;

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
        }
    }
    catch (const vk::OutOfDateKHRError&)
    {
        VulkanInstance::Get().GetDevice().waitIdle();
        swapchain.Recreate(window);
    }

    currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void VulkanRenderer::Shutdown()
{
    VulkanInstance::Get().GetDevice().waitIdle();
}
#pragma once
#include "Renderer/Vulkan/ImportCore.h"
#include "Renderer/Vulkan/VulkanDepthBuffer.h"
#include "../Core/Renderer.h"
#include "../Core/DLLHeader.h"

class EDERGRAPHICS_API VulkanRenderer : public Renderer
{
public:
    static VulkanRenderer& Get();

    void Init()          override;
    void BeginFrame()    override;
    void BeginMainPass();
    void EndFrame()      override;
    void Shutdown()      override;

    void SetWindow(NativeWindow* w)    { window = w; }
    void RecreateSwapchainResources(NativeWindow* w);
    void SetFramebufferResized()     { framebufferResized = true; }
    bool IsFrameStarted() const      { return frameStarted; }
    vk::CommandBuffer GetCommandBuffer() { return *commandBuffers[currentFrame]; }
    vk::Format        GetDepthFormat()   { return depthBuffer.GetFormat(); }
    uint32_t          GetCurrentFrame()  { return currentFrame; }

private:
    VulkanRenderer() = default;
    ~VulkanRenderer() = default;
    VulkanRenderer(const VulkanRenderer&) = delete;
    VulkanRenderer& operator=(const VulkanRenderer&) = delete;

    void CreateCommandPool();
    void CreateCommandBuffers();
    void CreateSyncObjects();

    vk::raii::CommandPool                commandPool    = nullptr;
    std::vector<vk::raii::CommandBuffer> commandBuffers;
    std::vector<vk::raii::Semaphore>     imageAvailable;
    std::vector<vk::raii::Semaphore>     renderFinished;
    std::vector<vk::raii::Fence>         inFlightFences;

    // Raw function pointers with KHR fallback (needed on Android where
    // vkCmdBeginRendering / vkCmdEndRendering core-1.3 names may be null in
    // the device dispatch table while the KHR aliases are available).
    PFN_vkCmdBeginRendering pfnCmdBeginRendering = nullptr;
    PFN_vkCmdEndRendering   pfnCmdEndRendering   = nullptr;

    VulkanDepthBuffer   depthBuffer;
    NativeWindow*         window             = nullptr;
    bool                framebufferResized = false;
    bool                frameStarted       = false;
    uint32_t            currentFrame       = 0;
    uint32_t            imageIndex         = 0;

    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;
};
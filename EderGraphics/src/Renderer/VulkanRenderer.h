#pragma once
#include "Renderer/Vulkan/ImportCore.h"
#include "../Core/Renderer.h"

class VulkanRenderer : public Renderer
{
public:
    static VulkanRenderer& Get()
    {
        static VulkanRenderer instance;
        return instance;
    }

    void Init()       override;
    void BeginFrame() override;
    void EndFrame()   override;
    void Shutdown()   override;

    void SetWindow(GLFWwindow* w)    { window = w; }
    void SetFramebufferResized()     { framebufferResized = true; }
    vk::CommandBuffer GetCommandBuffer() { return *commandBuffers[currentFrame]; }

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

    GLFWwindow* window             = nullptr;
    bool        framebufferResized = false;
    bool        frameStarted       = false;
    uint32_t    currentFrame       = 0;
    uint32_t    imageIndex         = 0;

    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;
};
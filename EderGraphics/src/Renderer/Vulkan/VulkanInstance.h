#pragma once
#include "ImportCore.h"

class EDERGRAPHICS_API VulkanInstance
{
public:
    static VulkanInstance& Get()
    {
        static VulkanInstance instance;
        return instance;
    }

    vk::raii::Instance&      GetInstance()       { return instance; }
    vk::raii::Device&        GetDevice()         { return device; }
    vk::raii::Queue&         GetGraphicsQueue()  { return graphicsQueue; }
    vk::raii::Queue&         GetPresentQueue()   { return presentQueue; }
    vk::raii::SurfaceKHR&    GetSurface()        { return surface; }
    vk::raii::PhysicalDevice& GetPhysicalDevice(){ return physicalDevice; }
    uint32_t                 GetGraphicsIndex()  { return graphicsIndex; }
    uint32_t                 GetPresentIndex()   { return presentIndex; }

    void Init(GLFWwindow* window);

private:
    VulkanInstance() = default;
    ~VulkanInstance() = default;
    VulkanInstance(const VulkanInstance&) = delete;
    VulkanInstance& operator=(const VulkanInstance&) = delete;

    void CreateInstance(GLFWwindow* window);
    void PickPhysicalDevice();
    void CreateDeviceLogic();

    vk::raii::Context        context;
    vk::raii::Instance       instance       = nullptr;
    vk::raii::SurfaceKHR     surface        = nullptr;
    vk::raii::PhysicalDevice physicalDevice = nullptr;
    vk::raii::Device         device         = nullptr;
    vk::raii::Queue          graphicsQueue  = nullptr;
    vk::raii::Queue          presentQueue   = nullptr;

    uint32_t graphicsIndex = 0;
    uint32_t presentIndex  = 0;

    const std::vector<const char*> deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };
};
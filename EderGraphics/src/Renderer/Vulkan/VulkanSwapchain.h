#pragma once
#include "ImportCore.h"

struct SwapchainSupportDetails
{
    vk::SurfaceCapabilitiesKHR        capabilities;
    std::vector<vk::SurfaceFormatKHR> formats;
    std::vector<vk::PresentModeKHR>   presentModes;
};

class EDERGRAPHICS_API VulkanSwapchain
{
public:
    static VulkanSwapchain& Get()
    {
        static VulkanSwapchain instance;
        return instance;
    }

    void Init(GLFWwindow* window);
    void Recreate(GLFWwindow* window);
    void Cleanup();

    vk::raii::SwapchainKHR&        GetSwapchain()   { return swapchain; }
    std::vector<vk::Image>&        GetImages()      { return images; }
    std::vector<vk::raii::ImageView>& GetImageViews(){ return imageViews; }
    vk::Format                     GetFormat()      { return format; }
    vk::Extent2D                   GetExtent()      { return extent; }

private:
    VulkanSwapchain() = default;
    ~VulkanSwapchain() = default;
    VulkanSwapchain(const VulkanSwapchain&) = delete;
    VulkanSwapchain& operator=(const VulkanSwapchain&) = delete;

    SwapchainSupportDetails QuerySupport();
    vk::SurfaceFormatKHR    ChooseFormat(const std::vector<vk::SurfaceFormatKHR>& formats);
    vk::PresentModeKHR      ChoosePresentMode(const std::vector<vk::PresentModeKHR>& modes);
    vk::Extent2D            ChooseExtent(const vk::SurfaceCapabilitiesKHR& caps, GLFWwindow* window);

    vk::raii::SwapchainKHR           swapchain  = nullptr;
    std::vector<vk::Image>           images;
    std::vector<vk::raii::ImageView> imageViews;
    vk::Format                       format;
    vk::Extent2D                     extent;
};
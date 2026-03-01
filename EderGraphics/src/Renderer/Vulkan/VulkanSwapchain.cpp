#include "VulkanSwapchain.h"
#include "VulkanInstance.h"
#include <algorithm>
#include <limits>

VulkanSwapchain& VulkanSwapchain::Get()
{
    static VulkanSwapchain instance;
    return instance;
}

void VulkanSwapchain::Init(NativeWindow* window)
{
    auto& vi = VulkanInstance::Get();
    SwapchainSupportDetails support = QuerySupport();

    vk::SurfaceFormatKHR surfaceFormat = ChooseFormat(support.formats);
    vk::PresentModeKHR   presentMode   = ChoosePresentMode(support.presentModes);
    vk::Extent2D         swapExtent    = ChooseExtent(support.capabilities, window);

    uint32_t imageCount = support.capabilities.minImageCount + 1;
    if (support.capabilities.maxImageCount > 0 && imageCount > support.capabilities.maxImageCount)
        imageCount = support.capabilities.maxImageCount;

    vk::SwapchainCreateInfoKHR createInfo{};
    createInfo.surface          = *vi.GetSurface();
    createInfo.minImageCount    = imageCount;
    createInfo.imageFormat      = surfaceFormat.format;
    createInfo.imageColorSpace  = surfaceFormat.colorSpace;
    createInfo.imageExtent      = swapExtent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage       = vk::ImageUsageFlagBits::eColorAttachment;

    uint32_t indices[] = { vi.GetGraphicsIndex(), vi.GetPresentIndex() };
    if (vi.GetGraphicsIndex() != vi.GetPresentIndex())
    {
        createInfo.imageSharingMode      = vk::SharingMode::eConcurrent;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices   = indices;
    }
    else
    {
        createInfo.imageSharingMode = vk::SharingMode::eExclusive;
    }

    createInfo.preTransform   = support.capabilities.currentTransform;
    createInfo.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
    createInfo.presentMode    = presentMode;
    createInfo.clipped        = vk::True;

    swapchain = vk::raii::SwapchainKHR(vi.GetDevice(), createInfo);
    images    = swapchain.getImages();
    format    = surfaceFormat.format;
    extent    = swapExtent;

    imageViews.reserve(images.size());
    for (auto& image : images)
    {
        vk::ImageViewCreateInfo viewInfo{};
        viewInfo.image                           = image;
        viewInfo.viewType                        = vk::ImageViewType::e2D;
        viewInfo.format                          = format;
        viewInfo.components.r                    = vk::ComponentSwizzle::eIdentity;
        viewInfo.components.g                    = vk::ComponentSwizzle::eIdentity;
        viewInfo.components.b                    = vk::ComponentSwizzle::eIdentity;
        viewInfo.components.a                    = vk::ComponentSwizzle::eIdentity;
        viewInfo.subresourceRange.aspectMask     = vk::ImageAspectFlagBits::eColor;
        viewInfo.subresourceRange.baseMipLevel   = 0;
        viewInfo.subresourceRange.levelCount     = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount     = 1;

        imageViews.emplace_back(vi.GetDevice(), viewInfo);
    }

    std::cout << "[Vulkan] Swapchain OK (" << images.size() << " images, "
              << extent.width << "x" << extent.height << ")" << std::endl;
}

SwapchainSupportDetails VulkanSwapchain::QuerySupport()
{
    auto& vi = VulkanInstance::Get();
    SwapchainSupportDetails details;
    details.capabilities = vi.GetPhysicalDevice().getSurfaceCapabilitiesKHR(*vi.GetSurface());
    details.formats      = vi.GetPhysicalDevice().getSurfaceFormatsKHR(*vi.GetSurface());
    details.presentModes = vi.GetPhysicalDevice().getSurfacePresentModesKHR(*vi.GetSurface());
    return details;
}

vk::SurfaceFormatKHR VulkanSwapchain::ChooseFormat(const std::vector<vk::SurfaceFormatKHR>& formats)
{
    for (const auto& f : formats)
        if (f.format == vk::Format::eB8G8R8A8Srgb && f.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear)
            return f;
    return formats.front();
}

vk::PresentModeKHR VulkanSwapchain::ChoosePresentMode(const std::vector<vk::PresentModeKHR>& modes)
{
    for (const auto& m : modes)
        if (m == vk::PresentModeKHR::eMailbox)
            return m;
    return vk::PresentModeKHR::eFifo;
}

vk::Extent2D VulkanSwapchain::ChooseExtent(const vk::SurfaceCapabilitiesKHR& caps, NativeWindow* window)
{
    if (caps.currentExtent.width != std::numeric_limits<uint32_t>::max())
        return caps.currentExtent;

#if defined(_WIN32)
    int w, h;
    glfwGetFramebufferSize(window, &w, &h);
#elif defined(__ANDROID__)
    int w = ANativeWindow_getWidth(window);
    int h = ANativeWindow_getHeight(window);
#endif

    vk::Extent2D actual{ static_cast<uint32_t>(w), static_cast<uint32_t>(h) };
    actual.width  = std::clamp(actual.width,  caps.minImageExtent.width,  caps.maxImageExtent.width);
    actual.height = std::clamp(actual.height, caps.minImageExtent.height, caps.maxImageExtent.height);
    return actual;
}

void VulkanSwapchain::Cleanup()
{
    imageViews.clear();
    swapchain = nullptr;
}

void VulkanSwapchain::Recreate(NativeWindow* window)
{
#if defined(_WIN32)
    int w = 0, h = 0;
    glfwGetFramebufferSize(window, &w, &h);
    while (w == 0 || h == 0)
    {
        glfwGetFramebufferSize(window, &w, &h);
        glfwWaitEvents();
    }
#elif defined(__ANDROID__)
    int w = ANativeWindow_getWidth(window);
    int h = ANativeWindow_getHeight(window);
#endif

    VulkanInstance::Get().GetDevice().waitIdle();
    Cleanup();
    Init(window);
}
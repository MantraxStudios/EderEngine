#include "VulkanInstance.h"

VulkanInstance& VulkanInstance::Get()
{
    static VulkanInstance instance;
    return instance;
}

void VulkanInstance::Init(NativeWindow* window)
{
    CreateInstance(window);
    PickPhysicalDevice();
    CreateDeviceLogic();
}

void VulkanInstance::CreateInstance(NativeWindow* window)
{
    vk::ApplicationInfo appInfo{};
    appInfo.pApplicationName   = "EderGraphics";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName        = "EderEngine";
    appInfo.engineVersion      = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion         = VK_API_VERSION_1_0;

    vk::InstanceCreateInfo createInfo{};
    createInfo.pApplicationInfo = &appInfo;

#if defined(_WIN32)
    uint32_t     glfwExtCount = 0;
    const char** glfwExts     = glfwGetRequiredInstanceExtensions(&glfwExtCount);
    createInfo.enabledExtensionCount   = glfwExtCount;
    createInfo.ppEnabledExtensionNames = glfwExts;
#elif defined(__ANDROID__)
    static const char* androidExts[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_ANDROID_SURFACE_EXTENSION_NAME
    };
    createInfo.enabledExtensionCount   = 2;
    createInfo.ppEnabledExtensionNames = androidExts;
#endif

    instance = vk::raii::Instance(context, createInfo);

#if defined(_WIN32)
    VkSurfaceKHR _surface;
    if (glfwCreateWindowSurface(*instance, window, nullptr, &_surface) != VK_SUCCESS)
        throw std::runtime_error("Failed to create window surface!");
    surface = vk::raii::SurfaceKHR(instance, _surface);
#elif defined(__ANDROID__)
    vk::AndroidSurfaceCreateInfoKHR surfaceInfo{};
    surfaceInfo.window = window;
    surface = instance.createAndroidSurfaceKHR(surfaceInfo);
#endif

    std::cout << "[Vulkan] Instance + Surface OK" << std::endl;
}

void VulkanInstance::PickPhysicalDevice()
{
    auto physicalDevices = instance.enumeratePhysicalDevices();
    if (physicalDevices.empty())
        throw std::runtime_error("No Vulkan-compatible GPU found!");

    physicalDevice = std::move(physicalDevices.front());

    auto props = physicalDevice.getProperties();
    std::cout << "[Vulkan] GPU: " << props.deviceName << std::endl;
}

void VulkanInstance::CreateDeviceLogic()
{
    auto queueFamilyProperties = physicalDevice.getQueueFamilyProperties();

    // Buscar graphics queue
    auto it = std::find_if(queueFamilyProperties.begin(), queueFamilyProperties.end(),
        [](const vk::QueueFamilyProperties& qfp)
        {
            return (qfp.queueFlags & vk::QueueFlagBits::eGraphics) != static_cast<vk::QueueFlags>(0);
        });

    graphicsIndex = static_cast<uint32_t>(std::distance(queueFamilyProperties.begin(), it));

    // Buscar present queue
    presentIndex = physicalDevice.getSurfaceSupportKHR(graphicsIndex, *surface)
                   ? graphicsIndex
                   : static_cast<uint32_t>(queueFamilyProperties.size());

    if (presentIndex == queueFamilyProperties.size())
    {
        for (size_t i = 0; i < queueFamilyProperties.size(); i++)
        {
            if ((queueFamilyProperties[i].queueFlags & vk::QueueFlagBits::eGraphics) &&
                physicalDevice.getSurfaceSupportKHR(static_cast<uint32_t>(i), *surface))
            {
                graphicsIndex = static_cast<uint32_t>(i);
                presentIndex  = graphicsIndex;
                break;
            }
        }

        if (presentIndex == queueFamilyProperties.size())
        {
            for (size_t i = 0; i < queueFamilyProperties.size(); i++)
            {
                if (physicalDevice.getSurfaceSupportKHR(static_cast<uint32_t>(i), *surface))
                {
                    presentIndex = static_cast<uint32_t>(i);
                    break;
                }
            }
        }
    }

    if (graphicsIndex == queueFamilyProperties.size() || presentIndex == queueFamilyProperties.size())
        throw std::runtime_error("Could not find graphics or present queue!");

    // Features Vulkan 1.3
    vk::PhysicalDeviceVulkan13Features vulkan13Features{};
    vulkan13Features.dynamicRendering = vk::True;

    vk::PhysicalDeviceFeatures2 features{};
    features.features.samplerAnisotropy = vk::True;
    features.pNext = &vulkan13Features;

    float queuePriority = 1.0f;

    vk::DeviceQueueCreateInfo queueCreateInfo{};
    queueCreateInfo.queueFamilyIndex = graphicsIndex;
    queueCreateInfo.queueCount       = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    vk::DeviceCreateInfo deviceCreateInfo{};
    deviceCreateInfo.pNext                   = &features;
    deviceCreateInfo.queueCreateInfoCount    = 1;
    deviceCreateInfo.pQueueCreateInfos       = &queueCreateInfo;
    deviceCreateInfo.enabledExtensionCount   = static_cast<uint32_t>(deviceExtensions.size());
    deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions.data();

    device        = vk::raii::Device(physicalDevice, deviceCreateInfo);
    graphicsQueue = vk::raii::Queue(device, graphicsIndex, 0);
    presentQueue  = vk::raii::Queue(device, presentIndex, 0);

    std::cout << "[Vulkan] Device + Queues OK" << std::endl;
}
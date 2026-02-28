#pragma once

#include "../../Core/DLLHeader.h"

#include <vulkan/vulkan_raii.hpp>

#if defined(_WIN32)

    #define GLFW_INCLUDE_VULKAN
    #include <GLFW/glfw3.h>

#elif defined(__ANDROID__)

    #include <android/native_window.h>
    #include <android/native_activity.h>

#endif

#include <iostream>
#include <vector>
#include <stdexcept>
#include <algorithm>
#include <iterator>
#pragma once

#include "../../Core/DLLHeader.h"

#include <vulkan/vulkan_raii.hpp>

#if defined(_WIN32)

    #include <SDL3/SDL.h>
    #include <SDL3/SDL_vulkan.h>
    using NativeWindow = SDL_Window;

#elif defined(__ANDROID__)

    #include <android/native_window.h>
    #include <android/native_activity.h>
    using NativeWindow = ANativeWindow;

#endif

#include <iostream>
#include <vector>
#include <stdexcept>
#include <algorithm>
#include <iterator>
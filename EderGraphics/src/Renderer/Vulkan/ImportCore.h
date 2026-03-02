#pragma once

#include "../../Core/DLLHeader.h"

#include <vulkan/vulkan_raii.hpp>

// SDL3 is used on all platforms (Windows, Android, etc.)
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
using NativeWindow = SDL_Window;

#include <iostream>
#include <vector>
#include <stdexcept>
#include <algorithm>
#include <iterator>
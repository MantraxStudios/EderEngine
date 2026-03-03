#pragma once
#include <glm/glm.hpp>

struct VolumetricLightComponent
{
    bool      enabled     = true;
    int       numSteps    = 64;                    // ray-march step count (perf vs. quality)
    float     density     = 0.008f;                // scattering coefficient σ_s  (keep low: 0.005–0.05)
    float     absorption  = 0.0f;                  // absorption coefficient σ_a
    float     g           = 0.3f;                  // Henyey-Greenstein anisotropy (-1 .. 1)
    float     intensity   = 0.5f;                  // final output multiplier
    float     maxDistance = 40.0f;                 // max ray-march distance (world units)
    float     jitter      = 1.0f;                  // noise jitter to break banding (0 = off)
    glm::vec3 tint        = {1.0f, 0.95f, 0.85f};  // colour tint applied to scattered light
};

#pragma once
#include <glm/glm.hpp>

struct SunShaftsComponent
{
    float     intensity  = 1.0f;            // Overall brightness of shafts
    float     decay      = 0.97f;           // Per-step light falloff (0.9–0.99)
    float     weight     = 0.35f;           // Per-sample contribution
    float     exposure   = 0.20f;           // Final multiply on accumulated rays
    glm::vec3 tint       = {1.0f, 0.95f, 0.85f}; // Warm sun tint
    bool      enabled    = true;
};

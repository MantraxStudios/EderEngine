#pragma once
#include <glm/glm.hpp>

struct SunShaftsComponent
{
    float     density     = 1.0f;                  // Ray step density / reach
    float     bloomScale  = 1.0f;                  // Glare / bloom brightness (independent)
    float     decay       = 0.97f;                 // Per-step ray falloff  (0.9–0.99)
    float     weight      = 2.0f;                  // Ray brightness accumulation weight
    float     exposure    = 0.85f;                 // Overall exposure multiplier
    float     sunRadius   = 0.012f;                // Angular radius of sun disk (occlusion pass)
    glm::vec3 tint        = {1.0f, 0.95f, 0.85f};  // Warm sun tint
    bool      enabled     = true;
};

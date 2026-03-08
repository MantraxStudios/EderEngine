#pragma once
#include <glm/glm.hpp>

enum class LightType
{
    Directional,
    Point,
    Spot
};

struct LightComponent
{
    LightType  type      = LightType::Directional;
    glm::vec3  color     = { 1.0f, 1.0f, 1.0f };
    float      intensity = 1.0f;

    // Point / Spot
    float range          = 10.0f;

    // Spot only
    float innerConeAngle = 15.0f; // degrees
    float outerConeAngle = 30.0f; // degrees

    bool  castShadow       = false;
    float shadowDistance   = 100.0f;  // Directional: max shadow range (world units), independent of camera far

    // -- Volumetric Light (Directional only) --
    bool      volumetricEnabled = false;
    int       volNumSteps       = 64;            // ray-march step count (perf vs. quality)
    float     volDensity        = 0.008f;        // scattering coefficient σ_s  (keep low: 0.005–0.05)
    float     volAbsorption     = 0.0f;          // absorption coefficient σ_a
    float     volG              = 0.3f;          // Henyey-Greenstein anisotropy (-1 .. 1)
    float     volIntensity      = 0.5f;          // final output multiplier
    float     volMaxDistance    = 40.0f;         // max ray-march distance (world units)
    float     volJitter         = 1.0f;          // noise jitter to break banding (0 = off)
    glm::vec3 volTint           = {1.0f, 0.95f, 0.85f}; // colour tint applied to scattered light

    // -- Sun Shafts (Directional only) --
    bool      sunShaftsEnabled = false;
    float     shaftsDensity    = 1.0f;    // ray step density / reach
    float     shaftsBloomScale = 1.0f;    // glare / bloom brightness (independent)
    float     shaftsDecay      = 0.97f;   // per-step ray falloff (0.9–0.99)
    float     shaftsWeight     = 2.0f;    // ray brightness accumulation weight
    float     shaftsExposure   = 0.85f;   // overall exposure multiplier
    float     shaftsSunRadius  = 0.012f;  // angular radius of sun disk (occlusion pass)
    glm::vec3 shaftsTint       = {1.0f, 0.95f, 0.85f}; // warm sun tint
};

#pragma once
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>

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

    // Color temperature (Kelvin). When enabled, tints `color` by a black-body
    // approximation — 6500K is neutral, lower is warm, higher is cool.
    bool       useTemperature   = false;
    float      colorTemperature = 6500.0f;

    // Directional only: scales the scene's hemisphere ambient (environment light).
    float      ambientIntensity = 1.0f;

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

// Black-body colour approximation (Tanner Helland). Returns a normalised RGB
// tint so 6500K ≈ white; warmer below, cooler above.
inline glm::vec3 KelvinToRGB(float kelvin)
{
    float t = std::min(std::max(kelvin, 1000.0f), 40000.0f) / 100.0f;
    float r, g, b;

    if (t <= 66.0f) r = 255.0f;
    else            r = 329.698727446f * std::pow(t - 60.0f, -0.1332047592f);

    if (t <= 66.0f) g = 99.4708025861f * std::log(t) - 161.1195681661f;
    else            g = 288.1221695283f * std::pow(t - 60.0f, -0.0755148492f);

    if (t >= 66.0f)      b = 255.0f;
    else if (t <= 19.0f) b = 0.0f;
    else                 b = 138.5177312231f * std::log(t - 10.0f) - 305.0447927307f;

    glm::vec3 c = glm::vec3(r, g, b) / 255.0f;
    return glm::clamp(c, glm::vec3(0.0f), glm::vec3(1.0f));
}

// Light colour after applying the optional colour temperature.
inline glm::vec3 EffectiveLightColor(const LightComponent& l)
{
    return l.useTemperature ? l.color * KelvinToRGB(l.colorTemperature) : l.color;
}

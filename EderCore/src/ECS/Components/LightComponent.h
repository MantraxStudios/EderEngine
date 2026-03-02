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

    bool  castShadow     = false;
};

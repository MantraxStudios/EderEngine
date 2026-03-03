#pragma once
#include <glm/glm.hpp>

struct VolumetricFogComponent
{
    bool enabled = true;

    // -- Colour --
    glm::vec3 fogColor     = { 0.75f, 0.82f, 0.95f };  // base fog/haze colour
    glm::vec3 horizonColor = { 0.98f, 0.90f, 0.75f };  // warmer tint near horizon
    glm::vec3 sunScatterColor = { 1.0f, 0.85f, 0.60f };// sun forward-scatter glow

    // -- Density & shape --
    float density      = 0.012f;  // global density coefficient
    float heightFalloff= 0.08f;   // how fast fog fades with height  (larger = thinner band)
    float heightOffset = 0.0f;    // world-Y below which fog is at full density

    // -- Distance range --
    float fogStart     = 2.0f;    // metres — below this the fog is zero
    float fogEnd       = 200.0f;  // metres — clamps the maximum fog amount

    // -- Scatter --
    float scatterStrength = 0.6f; // mie forward-scatter toward the sun (0–1)
    float maxFogAmount    = 0.95f;// global opacity ceiling (prevents total whiteout)
};

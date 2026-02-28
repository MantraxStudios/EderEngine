#pragma once
#include <glm/glm.hpp>
#include <cstdint>

#define MAX_DIR_LIGHTS   4
#define MAX_POINT_LIGHTS 16
#define MAX_SPOT_LIGHTS  8

struct alignas(16) DirectionalLight
{
    glm::vec3 direction  = { 0.0f, -1.0f, 0.0f };
    float     intensity  = 1.0f;
    glm::vec3 color      = { 1.0f, 1.0f, 1.0f };
    float     _pad       = 0.0f;
};

struct alignas(16) PointLight
{
    glm::vec3 position   = { 0.0f, 0.0f, 0.0f };
    float     radius     = 10.0f;
    glm::vec3 color      = { 1.0f, 1.0f, 1.0f };
    float     intensity  = 1.0f;
};

struct alignas(16) SpotLight
{
    glm::vec3 position   = { 0.0f, 0.0f, 0.0f };
    float     innerCos   = 0.97f;
    glm::vec3 direction  = { 0.0f, -1.0f, 0.0f };
    float     outerCos   = 0.93f;
    glm::vec3 color      = { 1.0f, 1.0f, 1.0f };
    float     intensity  = 1.0f;
    float     radius     = 20.0f;
    float     _pad[3]    = {};
};

struct LightUBO
{
    DirectionalLight dirLights[MAX_DIR_LIGHTS];
    PointLight       pointLights[MAX_POINT_LIGHTS];
    SpotLight        spotLights[MAX_SPOT_LIGHTS];
    int32_t          numDirLights   = 0;
    int32_t          numPointLights = 0;
    int32_t          numSpotLights  = 0;
    float            _pad           = 0.0f;
    glm::vec3        cameraPos      = {};
    float            _pad2          = 0.0f;
    glm::vec3        cameraForward  = { 0.0f, 0.0f, -1.0f };
    float            _pad3          = 0.0f;
    glm::vec4        cascadeSplits  = glm::vec4(0.0f);
    glm::mat4        cascadeMatrices[4] = {};
};

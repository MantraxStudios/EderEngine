#pragma once
#include <glm/glm.hpp>
#include <cstdint>

#define MAX_DIR_LIGHTS    4
#define MAX_POINT_LIGHTS  16
#define MAX_SPOT_LIGHTS   8
#define MAX_SPOT_SHADOWS  4
#define MAX_POINT_SHADOWS 4

struct alignas(16) DirectionalLight
{
    glm::vec3 direction  = { 0.0f, -1.0f, 0.0f };
    float     intensity  = 1.0f;
    glm::vec3 color      = { 1.0f, 1.0f, 1.0f };
    float     _pad       = 0.0f;
};

// 48 bytes (3 vec4 rows)
struct alignas(16) PointLight
{
    glm::vec3 position   = { 0.0f, 0.0f, 0.0f };
    float     radius     = 10.0f;
    glm::vec3 color      = { 1.0f, 1.0f, 1.0f };
    float     intensity  = 1.0f;
    int32_t   shadowIdx  = -1;      // -1 = no shadow, 0..MAX_POINT_SHADOWS-1 = cubemap slot
    float     _pad[3]    = {};
};

// 64 bytes (4 vec4 rows) — same total size as before
struct alignas(16) SpotLight
{
    glm::vec3 position   = { 0.0f, 0.0f, 0.0f };
    float     innerCos   = 0.97f;
    glm::vec3 direction  = { 0.0f, -1.0f, 0.0f };
    float     outerCos   = 0.93f;
    glm::vec3 color      = { 1.0f, 1.0f, 1.0f };
    float     intensity  = 1.0f;
    float     radius     = 20.0f;
    int32_t   shadowIdx  = -1;      // -1 = no shadow, 0..MAX_SPOT_SHADOWS-1 = shadow map slot
    float     _pad[2]    = {};
};

struct LightUBO
{
    DirectionalLight dirLights[MAX_DIR_LIGHTS];             // 4 × 32 = 128
    PointLight       pointLights[MAX_POINT_LIGHTS];         // 16 × 48 = 768
    SpotLight        spotLights[MAX_SPOT_LIGHTS];           // 8 × 64 = 512
    int32_t          numDirLights   = 0;
    int32_t          numPointLights = 0;
    int32_t          numSpotLights  = 0;
    float            _pad           = 0.0f;
    glm::vec3        cameraPos      = {};
    float            _pad2          = 0.0f;
    glm::vec3        cameraForward  = { 0.0f, 0.0f, -1.0f };
    float            _pad3          = 0.0f;
    glm::vec4        cascadeSplits  = glm::vec4(0.0f);
    glm::mat4        cascadeMatrices[4]             = {};   // 256
    glm::mat4        spotMatrices[MAX_SPOT_SHADOWS] = {};   // 4 × 64 = 256
    glm::vec4        pointFarPlanes                 = {};   // far plane per point shadow slot
    // Sky-driven ambient: set each frame from the procedural skybox sun direction.
    glm::vec4        skyAmbient    = { 0.08f, 0.12f, 0.20f, 1.0f }; // hemisphere top
    glm::vec4        groundAmbient = { 0.05f, 0.04f, 0.03f, 1.0f }; // hemisphere bottom
};

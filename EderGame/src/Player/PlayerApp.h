#pragma once

// ─────────────────────────────────────────────────────────────────────────────
//  PlayerApp — standalone game runtime (no ImGui / Editor).
//  Reads all assets from a compiled .pak; renders with full Vulkan pipeline.
// ─────────────────────────────────────────────────────────────────────────────

#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>

#include "Core/Camera.h"
#include "Core/Scene.h"
#include "Core/LightBuffer.h"
#include "Core/Material.h"
#include "Renderer/Vulkan/VulkanPipeline.h"
#include "Renderer/Vulkan/VulkanTexture.h"
#include "Renderer/Vulkan/VulkanSkybox.h"
#include "Renderer/Vulkan/VulkanShadowMap.h"
#include "Renderer/Vulkan/VulkanShadowPipeline.h"
#include "Renderer/Vulkan/VulkanSpotShadowMap.h"
#include "Renderer/Vulkan/VulkanPointShadowMap.h"
#include "Renderer/Vulkan/VulkanPointShadowPipeline.h"
#include "Renderer/Vulkan/BoneSSBO.h"
#include "EderCore.h"

class PlayerApp
{
public:
    // ── Normal (PAK) mode ────────────────────────────────────────────────────
    // initialScene : relative asset path of the scene to load (e.g. "scenes/main.scene")
    // gameName     : displayed in the window title
    int Run(const std::string& initialScene,
            const std::string& gameName = "EderPlayer");

    // ── Preview mode (editor play mode) ─────────────────────────────────────
    // scenePath : absolute path to the .scene file to load (written by the editor).
    // Assets are already initialised with the raw workdir before this is called.
    int RunPreview(const std::string& scenePath);

private:
    // ── Lifecycle ────────────────────────────────────────────────────────────
    void Init(const std::string& windowTitle, const std::string& initialScene);
    void InitPreview(const std::string& scenePath);
    void Shutdown();

    // ── Per-frame ────────────────────────────────────────────────────────────
    void PollEvents();
    void ProcessInput(float dt);
    void UpdateLightBuffer();
    void SyncECSToScene();
    void UpdateAnimations(float dt);

    // ── Render passes ────────────────────────────────────────────────────────
    void RenderShadowPasses(vk::CommandBuffer cmd);
    void RenderMainPass    (vk::CommandBuffer cmd);

    // ── Init helpers ─────────────────────────────────────────────────────────
    void InitMaterials();

    // ═══════════════════════  Members  ══════════════════════════════════════

    SDL_Window* m_window      = nullptr;
    bool        m_running     = true;
    bool        m_previewMode = false;   // true when launched from editor play mode
    bool        m_lookActive  = false;
    float       m_mouseDX    = 0.0f;
    float       m_mouseDY    = 0.0f;

    Camera   m_camera;
    Scene    m_scene;
    Registry m_registry;

    VulkanTexture m_albedoTex;
    Material      m_floorMat;
    Material      m_glassMat;
    Material      m_glassMat2;
    Material      m_glassMat3;

    VulkanPipeline m_pipeline;
    VulkanSkybox   m_skybox;
    BoneSSBO       m_boneSSBO;

    VulkanShadowMap           m_shadowMap;
    VulkanShadowPipeline      m_shadowPipeline;
    VulkanSpotShadowMap       m_spotShadowMap;
    VulkanPointShadowMap      m_pointShadowMap;
    VulkanPointShadowPipeline m_pointShadowPipeline;
    LightBuffer               m_lights;

    // ── Light frame state ────────────────────────────────────────────────────
    glm::vec3 m_activeDirDir       = glm::normalize(glm::vec3(-1.0f, -1.0f, -0.4f));
    glm::vec3 m_activeDirColor     = glm::vec3(1.0f, 0.9f, 0.7f);
    float     m_activeDirIntensity = 1.0f;
    bool      m_hasDir             = false;

    bool      m_hasSpotShadow      = false;
    glm::vec3 m_activeSpotPos      = glm::vec3(0.0f);
    glm::vec3 m_activeSpotDir      = glm::vec3(0.0f, -1.0f, 0.0f);
    float     m_activeSpotOuterCos = 0.866f;
    float     m_activeSpotFar      = 50.0f;
    glm::mat4 m_activeSpotMatrix   = glm::mat4(1.0f);

    bool      m_hasPointShadow = false;
    glm::vec3 m_activePointPos = glm::vec3(0.0f);
    float     m_activePointFar = 50.0f;

    glm::mat4 m_cascadeMatrices[VulkanShadowMap::NUM_CASCADES] = {};
    glm::vec4 m_cascadeSplits  = {};

    // ── Mesh hot-swap tracking ────────────────────────────────────────────────
    std::unordered_map<uint32_t, uint64_t>    m_lastMeshGuid;
    std::unordered_map<uint32_t, uint64_t>    m_lastAnimMeshGuid;
    std::unordered_map<uint32_t, std::string> m_lastMaterialName;
    std::unordered_map<std::string, uint64_t> m_lastMatTexGuid;    std::unordered_map<uint32_t, std::vector<glm::mat4>> m_entityBoneMatrices;};

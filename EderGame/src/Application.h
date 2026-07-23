#pragma once

#include <SDL3/SDL.h>
#include <glm/glm.hpp>

#include "Editor/Editor.h"
#include "Core/Camera.h"
#include "Core/Scene.h"
#include "Core/LightBuffer.h"
#include "Core/Material.h"
#include "Renderer/Vulkan/VulkanPipeline.h"
#include "Renderer/Vulkan/VulkanTexture.h"
#include "Renderer/Vulkan/VulkanFramebuffer.h"
#include "Renderer/Vulkan/VulkanDebugOverlay.h"
#include "Renderer/Vulkan/VulkanSkybox.h"
#include "Renderer/Vulkan/VulkanShadowMap.h"
#include "Renderer/Vulkan/VulkanShadowPipeline.h"
#include "Renderer/Vulkan/VulkanSpotShadowMap.h"
#include "Renderer/Vulkan/VulkanPointShadowMap.h"
#include "Renderer/Vulkan/VulkanPointShadowPipeline.h"
#include "Renderer/Vulkan/VulkanGizmo.h"
#include "Renderer/Vulkan/VulkanSunShafts.h"
#include "Renderer/Vulkan/VulkanOcclusionPass.h"
#include "Renderer/Vulkan/VulkanVolumetricLight.h"
#include "Renderer/Vulkan/VulkanVolumetricFog.h"
#include "Renderer/Vulkan/VulkanPostProcessPass.h"
#include "Renderer/Vulkan/BoneSSBO.h"
#include "UI/UIRenderer.h"
#include "EderCore.h"
#include "Physics/PhysicsSystem.h"
#include <unordered_map>
#include <string>
#include <thread>
#include <memory>

// ────────────────────────────────────────────────────────────────────────────
// Application
// Owns the full engine lifecycle: window, renderer, ECS, editor and all
// render passes.  Call Run() from main(); it returns the process exit code.
// ────────────────────────────────────────────────────────────────────────────
class Application
{
public:
    Application()  = default;
    ~Application() = default;

    // Entry point — blocks until the user closes the window, returns exit code.
    int Run();

    // Set the project name shown in the editor title bar (call before Run()).
    void SetProjectName(const std::string& name) { m_projectName = name; }

private:
    // ── Lifecycle ────────────────────────────────────────────────────────────
    void Init();
    void Shutdown();

    // ── Per-frame pipeline ───────────────────────────────────────────────────
    void PollEvents();
    void HandleSceneViewResize();
    void ProcessInput(float dt);
    void UpdateLightBuffer();
    void SyncECSToScene();
    void UpdateAnimations(float dt);

    // ── Render passes ────────────────────────────────────────────────────────
    void RenderShadowPasses(vk::CommandBuffer cmd);
    void RenderSceneView   (vk::CommandBuffer cmd);
    void RenderPostProcess (vk::CommandBuffer cmd);
    void RenderMainPass    (vk::CommandBuffer cmd);

    // ── Init helpers ─────────────────────────────────────────────────────────
    void InitMaterials();
    void InitPostProcess();
    void WireEditorCallbacks();
    // Loads each PlayerStart's prefab as child entities (editor preview).
    // Runs only in edit mode; idempotent (tracks loaded state per entity+path).
    void UpdatePlayerStartPreviews();

    // ── Scene operations ─────────────────────────────────────────────────
    void NewScene();
    void SaveScene();
    void SaveSceneAs(const std::string& name);
    void LoadScene (const std::string& absPath);

    // ── Build / Play ──────────────────────────────────────────────────────
    /// Pack all registered assets + game.conf into a .pak at outPakPath.
    /// Runs synchronously; progress is forwarded to the Editor log.
    void BuildPak(const std::string& outPakPath,
                  const std::string& initialScene,
                  const std::string& gameName);

    // ── Play mode ─────────────────────────────────────────────────────────
    /// Embedded target → snapshot scene, init physics+scripting in this process.
    /// Standalone target → launch EderPlayer.exe as a separate window.
    void StartPlayMode();

    /// Stop + restore pre-play scene state (both modes).
    void StopPlayMode();

    /// Called every frame in Standalone mode; detects EderPlayer process exit.
    void UpdatePlayerWindowPos();
    // ── Utility ──────────────────────────────────────────────────────────────
    float SceneViewAspect() const;

    // ═════════════════════════  Members  ════════════════════════════════════

    // ── Window / input ───────────────────────────────────────────────────────
    SDL_Window* m_window      = nullptr;
    bool        m_running     = true;
    std::string m_projectName = "EderEngine";
    bool        m_lookActive = false;
    float       m_mouseDX    = 0.0f;
    float       m_mouseDY    = 0.0f;

    // ── Core ─────────────────────────────────────────────────────────────────
    Camera    m_camera;
    Editor    m_editor;
    Scene     m_scene;
    Registry  m_registry;
    // Points to m_registry normally; redirected to the prefab registry while in prefab-edit mode.
    Registry* m_activeReg = nullptr;

    // ── Geometry / materials ─────────────────────────────────────────────────
    VulkanTexture m_albedoTex;
    Material      m_floorMat;
    Material      m_glassMat;
    Material      m_glassMat2;
    Material      m_glassMat3;

    // ── Main pipeline ────────────────────────────────────────────────────────
    VulkanPipeline     m_pipeline;
    VulkanDebugOverlay m_debugOverlay;
    VulkanSkybox       m_skybox;
    BoneSSBO           m_boneSSBO;          // identity fallback / initial bind
    std::unordered_map<uint32_t, std::unique_ptr<BoneSSBO>> m_entityBoneSSBO; // one per skinned entity
    VulkanFramebuffer  m_debugFb;          // primary scene-view framebuffer
    UIRenderer         m_uiRenderer;

    // ── Shadow system ────────────────────────────────────────────────────────
    VulkanShadowMap           m_shadowMap;
    VulkanShadowPipeline      m_shadowPipeline;
    VulkanSpotShadowMap       m_spotShadowMap;
    VulkanPointShadowMap      m_pointShadowMap;
    VulkanPointShadowPipeline m_pointShadowPipeline;
    LightBuffer               m_lights;

    // ── Post-process chain ───────────────────────────────────────────────────
    VulkanGizmo           m_gizmo;
    VulkanVolumetricLight m_volumetricLight;
    VulkanVolumetricFog   m_volumetricFog;
    VulkanOcclusionPass   m_occlusionPass;
    VulkanSunShafts       m_sunShafts;

    // ── Per-frame directional-light state (rebuilt from ECS) ─────────────────
    glm::vec3 m_activeDirDir          = glm::normalize(glm::vec3(-1.0f, -1.0f, -0.4f));
    glm::vec3 m_activeDirColor        = glm::vec3(1.0f, 0.9f, 0.7f);
    float     m_activeDirIntensity    = 1.0f;
    float     m_activeDirShadowDist   = 100.0f;
    bool      m_hasDir                = false;

    // ── Per-frame spot-shadow state ───────────────────────────────────────────
    bool      m_hasSpotShadow      = false;
    glm::vec3 m_activeSpotPos      = glm::vec3(0.0f);
    glm::vec3 m_activeSpotDir      = glm::vec3(0.0f, -1.0f, 0.0f);
    float     m_activeSpotOuterCos = 0.866f;   // cos(30°)
    float     m_activeSpotFar      = 50.0f;
    glm::mat4 m_activeSpotMatrix   = glm::mat4(1.0f);

    // ── Per-frame point-shadow state ──────────────────────────────────────────
    bool      m_hasPointShadow = false;
    glm::vec3 m_activePointPos = glm::vec3(0.0f);
    float     m_activePointFar = 50.0f;

    // ── Cascade shadow data ───────────────────────────────────────────────────
    glm::mat4 m_cascadeMatrices[VulkanShadowMap::NUM_CASCADES] = {};
    glm::vec4 m_cascadeSplits  = {};
    // Per-cascade world-space cull sphere (xyz center, w radius).
    glm::vec4 m_cascadeCullSpheres[VulkanShadowMap::NUM_CASCADES] = {};

    // ── Static directional-shadow cache ───────────────────────────────────────
    // When neither the cascade matrices nor any shadow caster changed (and there
    // are no animated casters), the 4 cascade passes are skipped and last frame's
    // depth map is reused.
    //
    // DISABLED by default: the shadow map is a SINGLE image shared across
    // MAX_FRAMES_IN_FLIGHT (2) frames, with no cross-frame synchronisation.
    // Writing it every frame is self-consistent (each frame re-establishes the
    // content it reads via an intra-frame barrier). Skipping the write makes a
    // frame read content written by a still-in-flight previous frame → a
    // read/write race that shows up as shimmering shadows when the camera moves.
    // To enable this safely the directional shadow map must be double-buffered
    // (one image per frame in flight) or gated by a fence/semaphore.
    bool      m_shadowCacheEnabled = false;
    bool      m_dirShadowValid     = false;   // a valid map was rendered at least once
    uint64_t  m_prevCasterHash     = 0;
    glm::mat4 m_prevCascadeMatrices[VulkanShadowMap::NUM_CASCADES] = {};
    bool DirShadowNeedsRedraw();              // updates cache state, returns true if a redraw is needed

    // ── Custom post-process graph ─────────────────────────────────────────────
    Krayon::PostProcessGraph                           m_ppGraph;
    std::vector<std::unique_ptr<VulkanPostProcessPass>> m_ppPasses;
    bool                                               m_ppDirty = false;

    void RebuildPostProcessPasses();

    // ── Post-process running output (advances each active pass) ──────────────
    VulkanFramebuffer* m_postFb = nullptr;

    // ── Mesh hot-reload tracking (keyed by entity id, value = last loaded GUID) ─
    std::unordered_map<uint32_t, uint64_t>                 m_lastMeshGuid;
    std::unordered_map<uint32_t, uint64_t>                 m_lastAnimMeshGuid;
    std::unordered_map<uint32_t, std::string>              m_lastMaterialName;
    std::unordered_map<uint32_t, std::vector<std::string>> m_lastSubMeshMaterials;
    // Texture hot-swap tracking: matName → last-loaded albedoTexGuid
    std::unordered_map<std::string, uint64_t> m_lastMatTexGuid;
    // Per-entity bone matrices (populated by UpdateAnimations, consumed by DrawSkinned callback)
    std::unordered_map<uint32_t, std::vector<glm::mat4>> m_entityBoneMatrices;

    // ── PlayerStart preview tracking: entity → last loaded prefab path ────────
    std::unordered_map<uint32_t, std::string> m_playerStartLoaded;

    // ── Scene persistence state ───────────────────────────────────────────────
    std::string m_currentScenePath;
    std::string m_currentSceneName = "Untitled";

    // ── Background build thread ──────────────────────────────────────────────
    std::thread m_buildThread;

    // ── Pending OS drag-drop files (batched between DROP_BEGIN/COMPLETE) ─────
    std::vector<std::string> m_pendingDropFiles;

    // ── Play mode state ──────────────────────────────────────────────────────
    // Embedded (inline):  physics+scripting tick in this process; no child window.
    bool        m_playingInline  = false;
    std::string m_tempScenePath;         // snapshot written on Play, deleted on Stop
    // Standalone: external EderPlayer.exe process (void* avoids windows.h in header)
    void* m_playerProcess  = nullptr;   // HANDLE
    void* m_playerHWND     = nullptr;   // HWND (unused — kept for process-exit poll)
};


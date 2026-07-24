#include "Application.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <imgui/imgui.h>
#include <glm/gtc/constants.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/euler_angles.hpp>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <array>
#include <unordered_map>
#include <cstdio>
#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#  include <shellapi.h>
#endif

#include "Core/MaterialLayout.h"
#include "Core/MaterialManager.h"
#include "Core/MeshManager.h"
#include "Core/TextureManager.h"
#include "ECS/Components/VolumetricFogComponent.h"
#include "ECS/Components/AnimationComponent.h"
#include "ECS/Components/AudioSourceComponent.h"
#include "ECS/Components/CameraComponent.h"
#include "ECS/Components/ColliderComponent.h"
#include "ECS/Components/CharacterControllerComponent.h"
#include "ECS/Components/HierarchyComponent.h"
#include "ECS/Components/LightComponent.h"
#include "ECS/Components/MeshRendererComponent.h"
#include "ECS/Components/RigidbodyComponent.h"
#include "ECS/Components/ScriptComponent.h"
#include "ECS/Components/TagComponent.h"
#include "ECS/Components/TransformComponent.h"
#include "ECS/Systems/TransformSystem.h"
#include "Renderer/VulkanRenderer.h"
#include "Renderer/Vulkan/VulkanInstance.h"
#include "Renderer/Vulkan/VulkanSwapchain.h"
#include "Physics/PhysicsSystem.h"
#include "Scripting/LuaScriptSystem.h"
#include "Audio/AudioSystem.h"
#include "UI/UISystem.h"
#include <IO/AssetManager.h>
#include <IO/SceneSerializer.h>
#include <IO/DebugDraw.h>
#include "ECS/Components/PlayerStartComponent.h"
#include "ECS/Components/PlayerComponent.h"

int Application::Run()
{
    try { Init(); }
    catch (const std::exception& e)
    {
        std::cerr << "[ERROR] Initialization failed: " << e.what() << "\n";
        return -1;
    }

    uint64_t prevTime = SDL_GetPerformanceCounter();
    const uint64_t perfFreq = SDL_GetPerformanceFrequency();
    static constexpr float PHYSICS_DT        = 1.0f / 60.0f;
    static constexpr float MAX_DT            = 0.1f;
    static constexpr int   MAX_PHYSICS_STEPS = 5;
    float physAccum = 0.0f;

    while (m_running)
    {
        const uint64_t currTime = SDL_GetPerformanceCounter();
        float dt = static_cast<float>(currTime - prevTime) / static_cast<float>(perfFreq);
        dt = std::min(dt, MAX_DT);
        prevTime = currTime;

        PollEvents();
        LuaScriptSystem::Get().BeginFrame();

        if (m_playingInline && m_editor.GetPlayState() == PlayState::Playing)
        {
            {
                if (LuaScriptSystem::Get().ConsumePendingQuit())
                    StopPlayMode();   // Game.quit() stops inline play

                std::string next = LuaScriptSystem::Get().ConsumePendingScene();
                if (!next.empty())
                {
                    LuaScriptSystem::Get().Shutdown();
                    PhysicsSystem::Get().Shutdown();
                    AudioSystem::Get().Shutdown();
                    m_registry.Clear();
                    m_scene.GetObjects().clear();
                    const auto bytes = Krayon::AssetManager::Get().GetBytes(next);
                    if (!bytes.empty())
                        Krayon::SceneSerializer::LoadFromBytes(bytes, m_registry);
                    else
                        Krayon::SceneSerializer::Load(next, m_registry);
                    PhysicsSystem::Get().Init();
                    LuaScriptSystem::Get().Init();
                    AudioSystem::Get().Init();
                    physAccum = 0.0f;
                }
            }

            physAccum += dt;

            int steps = 0;
            while (physAccum >= PHYSICS_DT && steps < MAX_PHYSICS_STEPS)
            {
                LuaScriptSystem::Get().Update(m_registry, PHYSICS_DT);
                PhysicsSystem::Get().SyncActors(m_registry);
                PhysicsSystem::Get().SyncControllers(m_registry);
                PhysicsSystem::Get().Step(PHYSICS_DT);
                PhysicsSystem::Get().WriteBack(m_registry);
                PhysicsSystem::Get().WriteBackControllers(m_registry);
                PhysicsSystem::Get().DispatchEvents(m_registry);
                physAccum -= PHYSICS_DT;
                ++steps;
            }

            // Smooth rendering between fixed steps: blend body poses by the
            // leftover accumulator fraction so motion doesn't stutter when the
            // render rate isn't a multiple of the 60 Hz sim.
            PhysicsSystem::Get().WriteBackInterpolated(m_registry, physAccum / PHYSICS_DT);

            {
                glm::vec3 fwd = m_camera.GetForward() * -1.0f;;
                glm::vec3 up  = m_camera.GetUp();
                AudioSystem::Get().SetListenerTransform(m_camera.fpsPos, fwd, up);
            }

            AudioSystem::Get().Update(m_registry, dt);

            if (steps >= MAX_PHYSICS_STEPS)
                physAccum = 0.0f;
        }

        HandleSceneViewResize();
        // Editor camera is disabled while the game is playing so it doesn't
        // interfere with script input (relative mouse mode, mouse delta, etc.)
        if (!m_playingInline || m_editor.GetPlayState() != PlayState::Playing)
            ProcessInput(dt);

        m_editor.BeginFrame();
        if (m_lookActive)
        {
            ImGui::GetIO().WantCaptureKeyboard = false;
            ImGui::GetIO().WantCaptureMouse    = false;
        }

        VulkanRenderer::Get().BeginFrame();
        if (!VulkanRenderer::Get().IsFrameStarted())
        {
            m_editor.EndFrame();
            auto& sc  = VulkanSwapchain::Get();
            uint32_t fw = sc.GetExtent().width  / 2;
            uint32_t fh = sc.GetExtent().height / 2;
            if (fw != m_debugFb.GetExtent().width || fh != m_debugFb.GetExtent().height)
            {
                m_debugFb.Recreate(fw, fh);
                m_sunShafts.Resize(fw, fh);
                m_occlusionPass.Resize(fw, fh);
                m_volumetricLight.Resize(fw, fh);
                m_volumetricFog.Resize(fw, fh);
            }
            continue;
        }

        // Switch active registry when entering / leaving prefab-edit mode
        {
            Registry* nextReg = m_editor.IsPrefabMode()
                ? &m_editor.GetPrefabRegistry() : &m_registry;
            if (nextReg != m_activeReg)
            {
                m_activeReg = nextReg;
                // Clear scene objects so stale meshes from the previous mode don't linger
                m_scene.Clear();
                m_lastMeshGuid.clear();
                m_lastAnimMeshGuid.clear();
                m_lastMaterialName.clear();
                m_lastSubMeshMaterials.clear();
            }
        }

        // Keep PlayerStart prefab children in sync while not playing
        if (!m_playingInline && !m_editor.IsPrefabMode())
            UpdatePlayerStartPreviews();

        UpdateLightBuffer();

        auto cmd = VulkanRenderer::Get().GetCommandBuffer();

        SyncECSToScene();
        UpdateAnimations(dt);

        {
            ImVec2 svPos = m_editor.GetSceneViewContentPos();
            UISystem::Get().SetViewportOffset(svPos.x, svPos.y);
        }
        UISystem::Get().Update(dt);

        if (m_playerProcess)
            UpdatePlayerWindowPos();

        m_deferred = m_editor.IsDeferred();
        RenderShadowPasses(cmd);
        if (m_deferred) RenderSceneViewDeferred(cmd);
        else            RenderSceneView(cmd);
        RenderPostProcess(cmd);
        RenderMainPass(cmd);

        m_editor.Draw(m_camera, *m_activeReg, dt);
        m_editor.Render(cmd);
        VulkanRenderer::Get().EndFrame();
        Krayon::DebugDraw::Get().Tick(dt);
    }

    Shutdown();
    return 0;
}

void Application::Init()
{
    m_activeReg = &m_registry;
    SDL_Init(SDL_INIT_VIDEO);

    m_window = SDL_CreateWindow(
        ("EderEngine — " + m_projectName).c_str(), 800, 600,
        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    if (!m_window)
        throw std::runtime_error("SDL_CreateWindow failed");

#ifdef _WIN32
    // Enable OS-level drag-and-drop onto the window
    {
        HWND hwnd = (HWND)SDL_GetPointerProperty(
            SDL_GetWindowProperties(m_window),
            SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
        if (hwnd) DragAcceptFiles(hwnd, TRUE);
    }
#endif

    m_camera.fpsMode = true;
    m_camera.fpsPos  = { 0.0f, 2.0f, 12.0f };
    m_camera.SetOrientation(0.0f, 0.0f);
    SDL_SetWindowRelativeMouseMode(m_window, false);

    VulkanInstance::Get().Init(m_window);
    VulkanSwapchain::Get().Init(m_window);
    VulkanRenderer::Get().Init();
    VulkanRenderer::Get().SetWindow(m_window);
    m_editor.Init(m_window);

    // The forward pipeline renders into the HDR scene framebuffer (RGBA16F),
    // not the swapchain — the final tonemap pass handles the LDR conversion.
    m_pipeline.Create(
        "shaders/triangle.vert.spv",
        "shaders/triangle.frag.spv",
        vk::Format::eR16G16B16A16Sfloat,
        VulkanRenderer::Get().GetDepthFormat());

    // Deferred geometry pipeline — same vertex shader / material + bone layouts as
    // the forward pipeline, but writes the G-buffer (MRT) via gbuffer.frag.
    {
        const auto& gbFormats = VulkanGBuffer::ColorFormats();
        m_gbufferPipeline.Create(
            "shaders/triangle.vert.spv",
            "shaders/gbuffer.frag.spv",
            VulkanSwapchain::Get().GetFormat(),
            VulkanRenderer::Get().GetDepthFormat(),
            VulkanGBuffer::COLOR_COUNT, gbFormats.data());
    }

    InitMaterials();

    m_shadowMap.Create(2048);
    m_shadowPipeline.Create(m_shadowMap.GetFormat(), *m_pipeline.GetDescriptorSetLayout());
    m_spotShadowMap.Create(1024);
    m_pointShadowMap.Create(512);
    m_pointShadowPipeline.Create(m_pointShadowMap.GetFormat());

    m_lights.Build(m_pipeline);
    // Comparison samplers → hardware PCF (sampler*Shadow in triangle/volumetric.frag).
    m_lights.BindShadowMap     (m_shadowMap.GetArrayView(),          m_shadowMap.GetCompareSampler());
    m_lights.BindSpotShadowMap (m_spotShadowMap.GetArrayView(),      m_spotShadowMap.GetCompareSampler());
    m_lights.BindPointShadowMap(m_pointShadowMap.GetCubeArrayView(), m_pointShadowMap.GetCompareSampler());

    InitPostProcess();

    m_boneSSBO.Create(m_pipeline);
    m_editor.SetSceneViewFramebuffer(&m_debugFb);

    UISystem::Get().Init();
    UISystem::Get().SetWindow(m_window);
    m_uiRenderer.Create(vk::Format::eR16G16B16A16Sfloat, VulkanRenderer::Get().GetDepthFormat());

    PhysicsSystem::Get().Init();
    LuaScriptSystem::Get().Init();
    AudioSystem::Get().Init();
    WireEditorCallbacks();
}

void Application::InitMaterials()
{
    MaterialLayout layout;
    layout.AddVec4 ("albedo")
          .AddFloat("roughness")
          .AddFloat("metallic")
          .AddFloat("emissiveIntensity")
          .AddFloat("alphaThreshold")
                                 .AddFloat("hasNormalMap")
                                 .AddFloat("hasRoughMap")
                                 .AddFloat("hasEmissiveMap")
                                 .AddFloat("normalStrength");

    MaterialManager::Get().Add("default", layout, m_pipeline);
    m_floorMat.Build(layout, m_pipeline);
    m_glassMat.Build(layout, m_pipeline);
    m_glassMat2.Build(layout, m_pipeline);
    m_glassMat3.Build(layout, m_pipeline);

    auto& def = MaterialManager::Get().GetDefault();
    def.SetVec4 ("albedo",            glm::vec4(1.0f, 0.92f, 0.78f, 1.0f));
    def.SetFloat("roughness",         0.55f);
    def.SetFloat("metallic",          0.0f);
    def.SetFloat("emissiveIntensity", 0.0f);

    m_floorMat.SetVec4 ("albedo",            glm::vec4(0.55f, 0.58f, 0.62f, 1.0f));
    m_floorMat.SetFloat("roughness",         0.85f);
    m_floorMat.SetFloat("metallic",          0.0f);
    m_floorMat.SetFloat("emissiveIntensity", 0.0f);

    m_glassMat.SetVec4 ("albedo",            glm::vec4(0.40f, 0.70f, 1.0f,  0.35f));
    m_glassMat.SetFloat("roughness",         0.05f);
    m_glassMat.SetFloat("metallic",          0.0f);
    m_glassMat.SetFloat("emissiveIntensity", 0.0f);
    m_glassMat.opacity = 0.35f;

    m_glassMat2.SetVec4 ("albedo",            glm::vec4(0.30f, 1.0f,  0.45f, 0.40f));
    m_glassMat2.SetFloat("roughness",         0.05f);
    m_glassMat2.SetFloat("metallic",          0.0f);
    m_glassMat2.SetFloat("emissiveIntensity", 0.0f);
    m_glassMat2.opacity = 0.40f;

    m_glassMat3.SetVec4 ("albedo",            glm::vec4(1.0f,  0.65f, 0.10f, 0.45f));
    m_glassMat3.SetFloat("roughness",         0.05f);
    m_glassMat3.SetFloat("metallic",          0.0f);
    m_glassMat3.SetFloat("emissiveIntensity", 0.0f);
    m_glassMat3.opacity = 0.45f;

    try { m_albedoTex.Load("assets/bush01.png"); }
    catch (const std::exception& e)
    {
        std::cerr << "[WARNING] " << e.what() << " — using default texture\n";
        m_albedoTex.CreateDefault();
    }

    def.BindTexture(0, m_albedoTex);
    m_floorMat.BindTexture(0, m_albedoTex);
    m_glassMat.BindTexture(0, m_albedoTex);
    m_glassMat2.BindTexture(0, m_albedoTex);
    m_glassMat3.BindTexture(0, m_albedoTex);
}

void Application::InitPostProcess()
{
    auto& sc      = VulkanSwapchain::Get();
    auto& rd      = VulkanRenderer::Get();
    uint32_t w    = sc.GetExtent().width  / 2;
    uint32_t h    = sc.GetExtent().height / 2;
    auto depthFmt = rd.GetDepthFormat();

    // Linear HDR scene target — tonemapped once at the end of the chain.
    m_debugFb.Create(w, h, vk::Format::eR16G16B16A16Sfloat, depthFmt);
    m_debugOverlay.Create(sc.GetFormat(), depthFmt);
    m_skybox.Create(m_debugFb.GetColorFormat(), depthFmt);
    m_gizmo.Create(m_debugFb.GetColorFormat(), depthFmt);
    m_occlusionPass.Create(w, h);
    m_sunShafts.Create(m_debugFb.GetColorFormat(), w, h);
    m_volumetricLight.Create(m_debugFb.GetColorFormat(), w, h,
                             *m_pipeline.GetLightDescriptorSetLayout());
    m_volumetricFog.Create(m_debugFb.GetColorFormat(), w, h,
                           *m_pipeline.GetLightDescriptorSetLayout());

    // Deferred targets (rendered only when m_deferred is on).
    m_gbuffer.Create(w, h, depthFmt);
    m_ssao.Create(w, h);
    m_deferredLighting.Create(m_debugFb.GetColorFormat(), depthFmt,
                              *m_pipeline.GetLightDescriptorSetLayout());

    // Final display transform: HDR chain → LDR image for the editor viewport.
    m_tonemapPass.Create(vk::Format::eB8G8R8A8Unorm, w, h, "shaders/tonemap.frag.spv");
}

void Application::RebuildPostProcessPasses()
{
    VulkanInstance::Get().GetDevice().waitIdle();

    m_ppPasses.clear();
    m_ppPasses.reserve(m_ppGraph.effects.size());

    uint32_t w = m_debugFb.GetExtent().width;
    uint32_t h = m_debugFb.GetExtent().height;

    for (const auto& fx : m_ppGraph.effects)
    {
        auto pass = std::make_unique<VulkanPostProcessPass>();
        try {
            pass->Create(m_debugFb.GetColorFormat(), w, h, fx.fragShaderPath);
            m_ppPasses.push_back(std::move(pass));
        }
        catch (const std::exception& e) {
            m_editor.AppendBuildLog(std::string("[PostProcess] Failed to create '")
                + fx.name + "': " + e.what());
            m_ppPasses.push_back(nullptr);  
        }
    }

    m_ppDirty = false;
}

void Application::WireEditorCallbacks()
{
    m_editor.SetNewSceneCallback([this]() { NewScene(); });

    m_editor.SetPostProcessGraph(&m_ppGraph, [this]() { m_ppDirty = true; });

    // SSAO + exposure live-edit from the Post Process panel.
    {
        PostProcessPanel::SSAOControls c;
        c.enabled   = &m_ssaoEnabled;
        c.radius    = &m_ssaoRadius;
        c.bias      = &m_ssaoBias;
        c.intensity = &m_ssaoIntensity;
        c.power     = &m_ssaoPower;
        c.exposure  = &m_tonemapExposure;
        m_editor.SetSSAOControls(c);
    }

    m_editor.SetSaveSceneCallback([this]()
    {
        if (m_currentScenePath.empty())
            SaveSceneAs(m_currentSceneName);
        else
            SaveScene();
    });

    m_editor.SetSaveAsCallback   ([this](const std::string& name) { SaveSceneAs(name); });
    m_editor.SetOpenSceneCallback([this](const std::string& path) { LoadScene(path); });

    m_editor.SetBuildPakCallback([this](const std::string& outPak,
                                        const std::string& initialScene,
                                        const std::string& gameName)
    {
        BuildPak(outPak, initialScene, gameName);
    });

    m_editor.SetPlayCallback([this]() { StartPlayMode(); });
    m_editor.SetStopCallback([this]() { StopPlayMode(); });
    m_editor.SetCurrentSceneName(m_currentSceneName);

    m_editor.SetMeshSubmeshCountQuery([](uint64_t guid) -> uint32_t {
        if (guid == 0) return 0;
        const auto* meta = Krayon::AssetManager::Get().FindByGuid(guid);
        if (!meta || !MeshManager::Get().Has(meta->path)) return 0;
        try { return MeshManager::Get().Load(meta->path).GetSubmeshCount(); }
        catch (...) { return 0; }
    });

    m_editor.SetMeshSubmeshNameQuery([](uint64_t guid, uint32_t index) -> std::string {
        if (guid == 0) return {};
        const auto* meta = Krayon::AssetManager::Get().FindByGuid(guid);
        if (!meta || !MeshManager::Get().Has(meta->path)) return {};
        try {
            auto& mesh = MeshManager::Get().Load(meta->path);
            if (index >= mesh.GetSubmeshCount()) return {};
            return mesh.GetSubmeshInfo(index).name;
        }
        catch (...) { return {}; }
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// UpdatePlayerStartPreviews
// In editor mode: for each PlayerStartComponent with a prefabPath, ensure its
// prefab is loaded as child entities so the actor is visible in the viewport.
// Idempotent: tracks loaded path per entity and only reloads when the path changes.
// ─────────────────────────────────────────────────────────────────────────────
void Application::UpdatePlayerStartPreviews()
{
    namespace fs = std::filesystem;
    const std::string wd = Krayon::AssetManager::Get().GetWorkDir();
    if (wd.empty()) return;

    // Helper: deep-clone prefabReg subtree into m_registry, parented under dstParent
    std::function<Entity(Registry&, Entity, Entity)> cloneNode =
        [&](Registry& src, Entity se, Entity dstParent) -> Entity
    {
        Entity dst = m_registry.Create();
        if (src.Has<TagComponent>(se))          m_registry.Add<TagComponent>(dst)          = src.Get<TagComponent>(se);
        if (src.Has<TransformComponent>(se))    m_registry.Add<TransformComponent>(dst)    = src.Get<TransformComponent>(se);
        if (src.Has<MeshRendererComponent>(se)) m_registry.Add<MeshRendererComponent>(dst) = src.Get<MeshRendererComponent>(se);
        if (src.Has<LightComponent>(se))        m_registry.Add<LightComponent>(dst)        = src.Get<LightComponent>(se);
        if (src.Has<AnimationComponent>(se))    m_registry.Add<AnimationComponent>(dst)    = src.Get<AnimationComponent>(se);
        // Wire hierarchy
        if (dstParent != NULL_ENTITY)
        {
            auto& hc = m_registry.Has<HierarchyComponent>(dst)
                ? m_registry.Get<HierarchyComponent>(dst)
                : m_registry.Add<HierarchyComponent>(dst);
            hc.parent = dstParent;
            auto& ph = m_registry.Has<HierarchyComponent>(dstParent)
                ? m_registry.Get<HierarchyComponent>(dstParent)
                : m_registry.Add<HierarchyComponent>(dstParent);
            ph.children.push_back(dst);
        }
        if (src.Has<HierarchyComponent>(se))
            for (Entity child : src.Get<HierarchyComponent>(se).children)
                cloneNode(src, child, dst);
        return dst;
    };

    m_registry.Each<PlayerStartComponent>([&](Entity e, PlayerStartComponent& ps)
    {
        if (ps.prefabPath.empty()) return;

        auto it = m_playerStartLoaded.find(e);
        // Already loaded the same path — nothing to do
        if (it != m_playerStartLoaded.end() && it->second == ps.prefabPath) return;

        // Path changed or first load: destroy old preview children
        if (it != m_playerStartLoaded.end())
        {
            if (m_registry.Has<HierarchyComponent>(e))
            {
                std::vector<Entity> toDestroy = m_registry.Get<HierarchyComponent>(e).children;
                for (Entity child : toDestroy)
                    m_registry.Destroy(child);
                m_registry.Get<HierarchyComponent>(e).children.clear();
            }
        }

        // Load the prefab into a temp registry
        Registry prefabReg;
        bool loaded = false;
        auto bytes = Krayon::AssetManager::Get().GetBytes(ps.prefabPath);
        if (!bytes.empty())
            loaded = Krayon::SceneSerializer::LoadPrefabFromBytes(bytes, prefabReg) != NULL_ENTITY;
        else
        {
            fs::path absPath = fs::path(wd) / ps.prefabPath;
            if (fs::exists(absPath))
                loaded = Krayon::SceneSerializer::LoadPrefab(absPath.string(), prefabReg) != NULL_ENTITY;
        }

        if (!loaded) return;

        // Find root entity in prefab (no parent in HierarchyComponent)
        Entity prefabRoot = NULL_ENTITY;
        prefabReg.Each<TransformComponent>([&](Entity pe, TransformComponent&) {
            if (prefabRoot != NULL_ENTITY) return;
            if (!prefabReg.Has<HierarchyComponent>(pe) ||
                prefabReg.Get<HierarchyComponent>(pe).parent == NULL_ENTITY)
                prefabRoot = pe;
        });

        if (prefabRoot == NULL_ENTITY) return;

        // Clone prefab into scene as children of the PlayerStart entity
        // (only visual components: no physics/scripts in editor preview)
        cloneNode(prefabReg, prefabRoot, e);

        m_playerStartLoaded[e] = ps.prefabPath;
    });
}

void Application::StartPlayMode()
{
    const bool wantEmbedded = (m_editor.GetPlayTarget() == PlayTarget::Embedded);

    if (wantEmbedded)
    {
        namespace fs = std::filesystem;

        const std::string workdirAbs = fs::absolute(
            Krayon::AssetManager::Get().GetWorkDir()).string();
        m_tempScenePath = (fs::path(workdirAbs) / "__playsnapshot__.scene").string();
        Krayon::SceneSerializer::Save(m_tempScenePath, m_registry, "__playsnapshot__");

        PhysicsSystem::Get().Init();
        LuaScriptSystem::Get().Init();
        AudioSystem::Get().Init();
        m_playingInline = true;

        // ── Mark player: the preview children of PlayerStart become the player ──
        // (UpdatePlayerStartPreviews already loaded the prefab as children in edit mode)
        m_registry.Each<PlayerStartComponent>([&](Entity e, PlayerStartComponent& ps) {
            if (ps.prefabPath.empty()) return;
            if (!m_registry.Has<HierarchyComponent>(e)) return;
            auto& hc = m_registry.Get<HierarchyComponent>(e);
            if (hc.children.empty()) return;
            Entity playerRoot = hc.children[0];
            if (!m_registry.Has<PlayerComponent>(playerRoot))
                m_registry.Add<PlayerComponent>(playerRoot);
        });

        // Release editor camera lock so it doesn't interfere with game input
        if (m_lookActive)
        {
            m_lookActive = false;
            SDL_SetWindowRelativeMouseMode(m_window, false);
            m_mouseDX = m_mouseDY = 0.0f;
        }

        m_editor.AppendBuildLog("[Play] Running inline in editor.");
        return;
    }

#ifdef _WIN32
    {
        namespace fs = std::filesystem;

        if (!m_currentScenePath.empty()) SaveScene();

        const std::string workdirAbs = fs::absolute(
            Krayon::AssetManager::Get().GetWorkDir()).string();

        std::string scenePath = m_currentScenePath;
        if (scenePath.empty())
        {
            m_tempScenePath = (fs::path(workdirAbs) / "__preview__.scene").string();
            Krayon::SceneSerializer::Save(m_tempScenePath, m_registry, "__preview__");
            scenePath = m_tempScenePath;
        }
        else
        {
            m_tempScenePath.clear();
        }

        char exeBuf[MAX_PATH] = {};
        GetModuleFileNameA(nullptr, exeBuf, MAX_PATH);
        const fs::path playerExe = fs::path(exeBuf).parent_path() / "EderPlayer.exe";

        if (!fs::exists(playerExe))
        {
            m_editor.AppendBuildLog("[Play] ERROR: EderPlayer.exe not found at: " +
                                    playerExe.string());
            m_editor.ForceStop();
            return;
        }

        const std::string cmdLine = "\"" + playerExe.string() + "\""
            + " --preview"
            + " --workdir \"" + workdirAbs + "\""
            + " --scene \""  + scenePath  + "\"";

        STARTUPINFOA si = {};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi = {};

        std::vector<char> cmdBuf(cmdLine.begin(), cmdLine.end());
        cmdBuf.push_back('\0');

        if (!CreateProcessA(nullptr, cmdBuf.data(),
                            nullptr, nullptr, FALSE, 0,
                            nullptr, nullptr, &si, &pi))
        {
            m_editor.AppendBuildLog("[Play] ERROR: Failed to launch EderPlayer.exe "
                                    "(GetLastError=" + std::to_string(GetLastError()) + ").");
            m_editor.ForceStop();
            return;
        }

        m_playerProcess = reinterpret_cast<void*>(pi.hProcess);
        CloseHandle(pi.hThread);

        m_editor.AppendBuildLog("[Play] EderPlayer launched standalone (PID "
                                + std::to_string(pi.dwProcessId) + ").");
    }
#else
    m_editor.AppendBuildLog("[Play] Standalone mode is only supported on Windows.");
    m_editor.ForceStop();
#endif
}

void Application::StopPlayMode()
{
    if (m_playingInline)
    {
        UISystem::Get().DestroyAll();
        PhysicsSystem::Get().Shutdown();
        LuaScriptSystem::Get().Shutdown();
        AudioSystem::Get().Shutdown();
        m_playingInline = false;

        if (!m_tempScenePath.empty())
        {
            VulkanInstance::Get().GetDevice().waitIdle();
            m_scene.Clear();
            m_registry.Clear();
            m_lastMeshGuid.clear();
            m_lastAnimMeshGuid.clear();
            m_lastMaterialName.clear();
            m_lastMatTexGuid.clear();
            m_lastSubMeshMaterials.clear();

            std::string name;
            Krayon::SceneSerializer::Load(m_tempScenePath, m_registry, &name);
            std::filesystem::remove(m_tempScenePath);
            m_tempScenePath.clear();
        }

        m_editor.AppendBuildLog("[Stop] Game stopped.");
        return;
    }

#ifdef _WIN32
    if (m_playerProcess)
    {
        TerminateProcess(reinterpret_cast<HANDLE>(m_playerProcess), 0);
        CloseHandle(reinterpret_cast<HANDLE>(m_playerProcess));
        m_playerProcess = nullptr;
    }
    if (!m_tempScenePath.empty())
    {
        std::filesystem::remove(m_tempScenePath);
        m_tempScenePath.clear();
    }
    m_editor.AppendBuildLog("[Stop] EderPlayer stopped.");
#endif
}

void Application::UpdatePlayerWindowPos()
{
#ifdef _WIN32
    HANDLE hProcess = reinterpret_cast<HANDLE>(m_playerProcess);
    if (!hProcess) return;

    if (WaitForSingleObject(hProcess, 0) == WAIT_OBJECT_0)
    {
        CloseHandle(hProcess);
        m_playerProcess = nullptr;
        m_editor.ForceStop();
        if (!m_tempScenePath.empty())
        {
            std::filesystem::remove(m_tempScenePath);
            m_tempScenePath.clear();
        }
        m_editor.AppendBuildLog("[Play] EderPlayer exited.");
    }
#endif
}

void Application::NewScene()
{
    VulkanInstance::Get().GetDevice().waitIdle();
    m_scene.Clear();
    m_registry.Clear();
    m_lastMeshGuid.clear();
    m_lastAnimMeshGuid.clear();
    m_lastMaterialName.clear();
    m_lastMatTexGuid.clear();
    m_lastSubMeshMaterials.clear();
    m_playerStartLoaded.clear();

    m_currentScenePath = "";
    m_currentSceneName = "Untitled";
    m_editor.SetCurrentSceneName(m_currentSceneName);
}

void Application::SaveScene()
{
    if (m_currentScenePath.empty()) { SaveSceneAs(m_currentSceneName); return; }
    Krayon::SceneSerializer::Save(m_currentScenePath, m_registry, m_currentSceneName, &m_ppGraph);
}

void Application::SaveSceneAs(const std::string& name)
{
    namespace fs = std::filesystem;
    auto& AM = Krayon::AssetManager::Get();
    if (AM.GetWorkDir().empty()) return;

    const fs::path scenesDir = fs::path(AM.GetWorkDir()) / "scenes";
    std::error_code ec;
    fs::create_directories(scenesDir, ec);

    std::string stem = name;
    fs::path    absFile;
    int         suffix = 0;
    do {
        absFile = fs::weakly_canonical(scenesDir / (stem + ".scene"));
        if (!fs::exists(absFile)) break;
        // Compare canonicalized paths so relative vs absolute and slash style
        // differences don't cause a spurious _1 suffix on the current scene.
        std::error_code ce;
        auto curCanon = fs::weakly_canonical(fs::path(m_currentScenePath), ce);
        if (!ce && absFile == curCanon) break;
        stem = name + "_" + std::to_string(++suffix);
    } while (true);

    if (Krayon::SceneSerializer::Save(absFile.string(), m_registry, stem, &m_ppGraph))
    {
        m_currentScenePath = absFile.string();
        m_currentSceneName = stem;
        AM.RegisterSceneFile(m_currentScenePath, stem);
        m_editor.SetCurrentSceneName(m_currentSceneName);
    }
}

void Application::LoadScene(const std::string& absPath)
{
    namespace fs = std::filesystem;
    if (!fs::exists(absPath)) return;

    VulkanInstance::Get().GetDevice().waitIdle();
    m_scene.Clear();
    m_registry.Clear();
    m_lastMeshGuid.clear();
    m_lastAnimMeshGuid.clear();
    m_lastMaterialName.clear();
    m_lastMatTexGuid.clear();
    m_lastSubMeshMaterials.clear();
    m_playerStartLoaded.clear();

    std::string loadedName;
    m_ppGraph = {};
    if (Krayon::SceneSerializer::Load(absPath, m_registry, &loadedName, &m_ppGraph))
    {
        m_currentScenePath = fs::weakly_canonical(absPath).string();
        m_currentSceneName = loadedName.empty()
            ? fs::path(absPath).stem().string()
            : loadedName;
        m_editor.SetCurrentSceneName(m_currentSceneName);
        m_ppDirty = true;
    }
}

void Application::BuildPak(const std::string&,
                            const std::string& initialScene,
                            const std::string& gameName)
{
    namespace fs = std::filesystem;

    auto& AM        = Krayon::AssetManager::Get();
    const auto& all = AM.GetAll();

    const fs::path buildDir  = fs::current_path();
    const fs::path amContent = fs::path(AM.GetWorkDir()).is_absolute()
                               ? fs::path(AM.GetWorkDir())
                               : buildDir / AM.GetWorkDir();
    const std::string gname  = gameName.empty() ? "EderGame" : gameName;
    const fs::path distDir   = amContent.parent_path() / gname;
    const std::string outPakPath = (distDir / "Game.pak").string();

    AM.Scan();

    std::unordered_map<std::string, std::string> fileMap;
    fileMap.reserve(all.size());
    const std::string workDir = AM.GetWorkDir();

    for (const auto& [guid, meta] : all)
    {
        if (meta.type == Krayon::AssetType::Unknown) continue;
        if (meta.type == Krayon::AssetType::PAK)     continue;

        const std::string absPath = workDir + "/" + meta.path;
        if (fs::exists(absPath))
            fileMap[meta.path] = absPath;
    }

    Krayon::GameConfig config;
    config.gameName     = gameName.empty() ? "EderGame" : gameName;
    config.initialScene = initialScene;

    const std::string cfgText = config.Serialize();
    std::unordered_map<std::string, std::vector<uint8_t>> memMap;
    memMap["game.conf"] = std::vector<uint8_t>(cfgText.begin(), cfgText.end());

    {
        std::ostringstream manifest;
        for (const auto& [guid, meta] : all)
        {
            if (meta.type == Krayon::AssetType::Unknown) continue;
            if (meta.type == Krayon::AssetType::PAK)     continue;
            manifest << std::hex << guid << std::dec
                     << '\t' << Krayon::AssetTypeToString(meta.type)
                     << '\t' << meta.name
                     << '\t' << meta.path
                     << '\n';
        }
        const std::string ms = manifest.str();
        memMap["assets.manifest"] = std::vector<uint8_t>(ms.begin(), ms.end());
    }

    const fs::path outDir = fs::path(outPakPath).parent_path();
    if (!outDir.empty())
    {
        std::error_code ec;
        fs::create_directories(outDir, ec);
    }

    {
        std::error_code ec;
        if (fs::exists(outPakPath, ec))
        {
            fs::remove(outPakPath, ec);
            m_editor.AppendBuildLog("[Build] Removed old Game.pak");
        }
    }

    m_editor.AppendBuildLog("[Build] Starting — " + std::to_string(fileMap.size()) + " assets + game.conf");
    m_editor.AppendBuildLog("[Build] Output: " + outPakPath);
    m_editor.AppendBuildLog("[Build] Initial scene: " + (initialScene.empty() ? "(none)" : initialScene));

    try
    {
        Krayon::KRCompiler::Build(
            outPakPath,
            fileMap,
            memMap,
            [this](int done, int total, const std::string& name)
            {
                if (!name.empty())
                {
                    const std::string line = "  [" + std::to_string(done + 1) + "/" +
                                             std::to_string(total) + "]  " + name;
                    m_editor.AppendBuildLog(line);
                }
            });

        m_editor.AppendBuildLog("[Build] Done!  " + outPakPath);

        m_editor.AppendBuildLog("[Compile] Building EderPlayer.exe...");
        m_editor.SetBuildRunning(true);

        if (m_buildThread.joinable())
            m_buildThread.join();

        const std::string capturedOutPak = outPakPath;

        m_buildThread = std::thread([this, capturedOutPak]() mutable
        {
            namespace fs = std::filesystem;

            const std::string cmd = "cmake --build . --target EderPlayer 2>&1";
            FILE* pipe = _popen(cmd.c_str(), "r");
            if (pipe)
            {
                char buf[512];
                while (fgets(buf, sizeof(buf), pipe))
                {
                    std::string line(buf);
                    while (!line.empty() &&
                           (line.back() == '\n' || line.back() == '\r'))
                        line.pop_back();
                    if (!line.empty())
                        m_editor.AppendBuildLog(line);
                }
                const int ret = _pclose(pipe);
                if (ret != 0)
                {
                    m_editor.AppendBuildLog(
                        "[Compile] FAILED (exit=" + std::to_string(ret) + ")");
                    m_editor.SetBuildRunning(false);
                    return;
                }
            }
            else
            {
                m_editor.AppendBuildLog("[Compile] ERROR: could not run cmake");
                m_editor.SetBuildRunning(false);
                return;
            }
            m_editor.AppendBuildLog("[Compile] EderPlayer built OK.");

            const fs::path bldDir = fs::current_path();
            const fs::path pakSrc = fs::path(capturedOutPak).is_absolute()
                                    ? fs::path(capturedOutPak)
                                    : bldDir / capturedOutPak;
            const fs::path outDir = pakSrc.parent_path();
            std::error_code ec;
            fs::create_directories(outDir, ec);

            auto copyFile = [&](const fs::path& src, const std::string& dstName)
            {
                fs::copy_file(src, outDir / dstName,
                              fs::copy_options::overwrite_existing, ec);
                if (ec)
                    m_editor.AppendBuildLog(
                        "[Package] WARN: could not copy " + src.filename().string());
                else
                    m_editor.AppendBuildLog("[Package] " + dstName);
            };

            copyFile(bldDir / "EderPlayer.exe",         "EderPlayer.exe");
            copyFile(bldDir / "EderGraphics.dll",        "EderGraphics.dll");
            copyFile(bldDir / "SDL3.dll",                "SDL3.dll");
            copyFile(bldDir / "assimp-vc143-mt.dll",     "assimp-vc143-mt.dll");
            copyFile(bldDir / "lua54.dll",               "lua54.dll");
            copyFile(bldDir / "fmod.dll",                "fmod.dll");

            m_editor.AppendBuildLog("[Package] Done -> " + outDir.string());
            m_editor.SetBuildRunning(false);
        });
        m_buildThread.detach();
    }
    catch (const std::exception& e)
    {
        m_editor.AppendBuildLog(std::string("[Build] ERROR: ") + e.what());
    }
}

void Application::Shutdown()
{
    if (m_buildThread.joinable())
        m_buildThread.join();

    VulkanInstance::Get().GetDevice().waitIdle();

    m_editor.Shutdown();
    m_ppPasses.clear();
    m_gizmo.Destroy();
    m_boneSSBO.Destroy();
    m_occlusionPass.Destroy();
    m_volumetricLight.Destroy();
    m_volumetricFog.Destroy();
    m_sunShafts.Destroy();
    m_tonemapPass.Destroy();
    m_deferredLighting.Destroy();
    m_ssao.Destroy();
    m_gbuffer.Destroy();
    m_gbufferPipeline.Destroy();
    m_debugOverlay.Destroy();
    m_skybox.Destroy();
    m_debugFb.Destroy();
    m_pointShadowPipeline.Destroy();
    m_pointShadowMap.Destroy();
    m_spotShadowMap.Destroy();
    m_shadowPipeline.Destroy();
    m_shadowMap.Destroy();
    m_lights.Destroy();
    m_scene.Destroy();
    MaterialManager::Get().Destroy();
    MeshManager::Get().Destroy();
    m_floorMat.Destroy();
    m_albedoTex.Destroy();
    m_pipeline.Destroy();

    PhysicsSystem::Get().Shutdown();
    LuaScriptSystem::Get().Shutdown();
    AudioSystem::Get().Shutdown();
    UISystem::Get().Shutdown();
    m_uiRenderer.Destroy();
    SDL_DestroyWindow(m_window);
    SDL_Quit();
}

void Application::PollEvents()
{
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        m_editor.ProcessEvent(event);
        UISystem::Get().HandleEvent(event);

        switch (event.type)
        {
        case SDL_EVENT_QUIT:
            m_running = false;
            break;
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            VulkanRenderer::Get().SetFramebufferResized();
            break;
        case SDL_EVENT_MOUSE_MOTION:
            if (m_lookActive)
            {
                m_mouseDX += event.motion.xrel;
                m_mouseDY += event.motion.yrel;
            }
            break;
        case SDL_EVENT_DROP_BEGIN:
            std::cout << "[Drop] BEGIN\n";
            m_pendingDropFiles.clear();
            break;
        case SDL_EVENT_DROP_FILE:
            std::cout << "[Drop] FILE: " << (event.drop.data ? event.drop.data : "<null>") << "\n";
            if (event.drop.data)
                m_pendingDropFiles.push_back(event.drop.data);
            break;
        case SDL_EVENT_DROP_COMPLETE:
            std::cout << "[Drop] COMPLETE, files=" << m_pendingDropFiles.size() << "\n";
            if (!m_pendingDropFiles.empty())
            {
                m_editor.HandleFileDropBatch(m_pendingDropFiles);
                m_pendingDropFiles.clear();
            }
            break;
        default:
            break;
        }
    }
}

void Application::ProcessInput(float dt)
{
    float mx, my;
    bool  rmb = (SDL_GetMouseState(&mx, &my) & SDL_BUTTON_RMASK) != 0;

    if (rmb && !m_lookActive)
    {
        m_lookActive = true;
        SDL_SetWindowRelativeMouseMode(m_window, true);
        SDL_RaiseWindow(m_window);
        m_mouseDX = m_mouseDY = 0.0f;
    }
    else if (!rmb && m_lookActive)
    {
        m_lookActive = false;
        SDL_SetWindowRelativeMouseMode(m_window, false);
        m_mouseDX = m_mouseDY = 0.0f;
    }

    if (!m_lookActive)
    {
        m_mouseDX = m_mouseDY = 0.0f;
        return;
    }

    m_camera.FPSLook(m_mouseDX, m_mouseDY);
    m_mouseDX = m_mouseDY = 0.0f;

    const bool*     keys  = SDL_GetKeyboardState(nullptr);
    constexpr float speed = 8.0f;
    glm::vec3 fwd   = m_camera.GetForward();
    glm::vec3 right = m_camera.GetRight();
    glm::vec3 fwdXZ = glm::normalize(glm::vec3(fwd.x, 0.0f, fwd.z));

    if (keys[SDL_SCANCODE_W])     m_camera.fpsPos += fwdXZ  * speed * dt;
    if (keys[SDL_SCANCODE_S])     m_camera.fpsPos -= fwdXZ  * speed * dt;
    if (keys[SDL_SCANCODE_A])     m_camera.fpsPos -= right   * speed * dt;
    if (keys[SDL_SCANCODE_D])     m_camera.fpsPos += right   * speed * dt;
    if (keys[SDL_SCANCODE_SPACE]) m_camera.fpsPos.y += speed * dt;
    if (keys[SDL_SCANCODE_LCTRL]) m_camera.fpsPos.y -= speed * dt;
}

void Application::HandleSceneViewResize()
{
    uint32_t svW = 0, svH = 0;
    m_editor.GetSceneViewSize(svW, svH);

    if (svW <= 4 || svH <= 4) return;
    if (svW == m_debugFb.GetExtent().width && svH == m_debugFb.GetExtent().height) return;

    VulkanInstance::Get().GetDevice().waitIdle();
    m_editor.ReleaseSceneViewFramebuffer();
    m_debugFb.Recreate(svW, svH);
    m_sunShafts.Resize(svW, svH);
    m_occlusionPass.Resize(svW, svH);
    m_volumetricLight.Resize(svW, svH);
    m_volumetricFog.Resize(svW, svH);
    m_gbuffer.Recreate(svW, svH);
    m_ssao.Resize(svW, svH);
    m_deferredLighting.ResetBindings();
    m_tonemapPass.Resize(svW, svH);
    for (auto& pass : m_ppPasses)
        pass->Resize(svW, svH);
}

void Application::UpdateLightBuffer()
{
    Registry& reg = *m_activeReg;
    float dirAmbientScale = 1.0f;   // set from the directional light's ambientIntensity
    auto& sc     = VulkanSwapchain::Get();
    float aspect = static_cast<float>(sc.GetExtent().width) /
                   static_cast<float>(sc.GetExtent().height);

    m_hasDir             = false;
    m_hasSpotShadow      = false;
    m_hasPointShadow     = false;
    m_activeDirDir       = glm::normalize(glm::vec3(-1.0f, -1.0f, -0.4f));
    m_activeDirColor     = glm::vec3(1.0f, 0.9f, 0.7f);
    m_activeDirIntensity = 1.0f;

    m_lights.ClearLights();

    // Collect all lights, then sort point/spot by distance to camera so the
    // closest ones fill the GPU buffer first when the limit is exceeded.
    glm::vec3 camPos = m_camera.GetPosition();

    struct LightEntry { Entity e; float distSq; };
    std::vector<LightEntry> pointEntries, spotEntries;

    reg.Each<LightComponent>([&](Entity e, LightComponent& l)
    {
        if (!reg.Has<TransformComponent>(e)) return;

        if (l.type == LightType::Directional)
        {
            glm::mat4 mat = TransformSystem::GetWorldMatrix(e, reg);
            glm::vec3 dir = glm::normalize(glm::vec3(mat * glm::vec4(0, -1, 0, 0)));

            glm::vec3 effColor = EffectiveLightColor(l);
            if (!m_hasDir)
            {
                m_activeDirDir          = dir;
                m_activeDirColor        = effColor;
                m_activeDirIntensity    = l.intensity;
                m_activeDirShadowDist   = l.shadowDistance;
                dirAmbientScale         = l.ambientIntensity;
                m_hasDir                = true;
            }

            float sunHorizon = glm::clamp(-dir.y * 5.0f + 1.0f, 0.0f, 1.0f);
            DirectionalLight dl{};
            dl.direction = dir;
            dl.color     = effColor;
            dl.intensity = l.intensity * sunHorizon;
            m_lights.AddDirectional(dl);
        }
        else if (l.type == LightType::Point)
        {
            glm::vec3 pos = glm::vec3(TransformSystem::GetWorldMatrix(e, reg)[3]);
            glm::vec3 d   = pos - camPos;
            pointEntries.push_back({ e, glm::dot(d, d) });
        }
        else if (l.type == LightType::Spot)
        {
            glm::mat4 mat = TransformSystem::GetWorldMatrix(e, reg);
            glm::vec3 pos = glm::vec3(mat[3]);
            glm::vec3 d   = pos - camPos;
            spotEntries.push_back({ e, glm::dot(d, d) });
        }
    });

    // Sort closest-first so nearby lights always occupy the limited GPU slots.
    std::sort(pointEntries.begin(), pointEntries.end(),
              [](const LightEntry& a, const LightEntry& b){ return a.distSq < b.distSq; });
    std::sort(spotEntries.begin(), spotEntries.end(),
              [](const LightEntry& a, const LightEntry& b){ return a.distSq < b.distSq; });

    int pointSlot = 0;
    for (auto& entry : pointEntries)
    {
        Entity e        = entry.e;
        auto&  l        = reg.Get<LightComponent>(e);
        PointLight pl{};
        pl.position  = glm::vec3(TransformSystem::GetWorldMatrix(e, reg)[3]);
        pl.color     = EffectiveLightColor(l);
        pl.intensity = l.intensity;
        pl.radius    = l.range;

        if (l.castShadow && pointSlot < 1)
        {
            pl.shadowIdx     = 0;
            m_hasPointShadow = true;
            m_activePointPos = pl.position;
            m_activePointFar = l.range;
            m_lights.SetPointFarPlane(0, l.range);
            ++pointSlot;
        }
        else { pl.shadowIdx = -1; }

        m_lights.AddPoint(pl);
    }

    int spotSlot = 0;
    for (auto& entry : spotEntries)
    {
        Entity e   = entry.e;
        auto&  l   = reg.Get<LightComponent>(e);
        glm::mat4 mat = TransformSystem::GetWorldMatrix(e, reg);
        glm::vec3 dir = glm::normalize(glm::vec3(mat * glm::vec4(0, -1, 0, 0)));

        SpotLight sl{};
        sl.position  = glm::vec3(mat[3]);
        sl.direction = dir;
        sl.innerCos  = std::cos(glm::radians(l.innerConeAngle));
        sl.outerCos  = std::cos(glm::radians(l.outerConeAngle));
        sl.color     = EffectiveLightColor(l);
        sl.intensity = l.intensity;
        sl.radius    = l.range;

        if (l.castShadow && spotSlot < 1)
        {
            sl.shadowIdx         = 0;
            m_hasSpotShadow      = true;
            m_activeSpotPos      = sl.position;
            m_activeSpotDir      = dir;
            m_activeSpotOuterCos = sl.outerCos;
            m_activeSpotFar      = l.range;
            m_activeSpotMatrix   = VulkanSpotShadowMap::ComputeMatrix(
                m_activeSpotPos, m_activeSpotDir, m_activeSpotOuterCos, 0.3f, m_activeSpotFar);
            m_lights.SetSpotMatrix(0, m_activeSpotMatrix);
            ++spotSlot;
        }
        else { sl.shadowIdx = -1; }

        m_lights.AddSpot(sl);
    }

    m_shadowMap.ComputeCascades(
        m_camera.GetView(),
        m_activeDirDir, m_camera.nearPlane, m_activeDirShadowDist,
        m_cascadeMatrices, m_cascadeSplits, m_cascadeCullSpheres);

    m_lights.SetCascadeData(m_cascadeMatrices, m_cascadeSplits);
    m_lights.SetCameraForward(m_camera.GetForward());
    m_lights.SetNearPlane(m_camera.nearPlane);
    m_lights.SetSkyAmbient(glm::vec3(0.20f, 0.24f, 0.30f) * dirAmbientScale,
                           glm::vec3(0.08f, 0.07f, 0.06f) * dirAmbientScale);
    m_lights.Update(m_camera.GetPosition());
}

// Bind a material's PBR maps (normal / roughness-metallic / emissive) and set the
// shader "hasXxxMap" flags. Texture binds are cached per material+guid so we never
// rewrite a descriptor that an in-flight frame might still be using.
static void ApplyPBRMaps(Material& mat, const std::string& key, const Krayon::MaterialAsset& a)
{
    static std::unordered_map<std::string, std::array<uint64_t, 3>> cache;
    auto& c = cache[key];
    auto bind = [&](uint32_t slot, uint64_t guid, int idx, const char* flag, bool srgb)
    {
        mat.SetFloat(flag, guid != 0 ? 1.0f : 0.0f);
        if (guid == 0 || c[idx] == guid) return;
        const Krayon::AssetMeta* tm = Krayon::AssetManager::Get().FindByGuid(guid);
        if (!tm) return;
        try {
            VulkanTexture& t = TextureManager::Get().Load(tm->path, srgb);
            mat.BindTexture(slot, t);
            c[idx] = guid;
        } catch (const std::exception&) {}
    };
    // Normal and roughness/metallic are DATA maps → linear; emissive is colour → sRGB.
    bind(1, a.normalTexGuid,    0, "hasNormalMap",   false);
    bind(2, a.roughnessTexGuid, 1, "hasRoughMap",    false);
    bind(3, a.emissiveTexGuid,  2, "hasEmissiveMap", true);
    mat.SetFloat("normalStrength", a.normalStrength);
}

void Application::SyncECSToScene()
{
    Registry& reg = *m_activeReg;
    auto& objs = m_scene.GetObjects();
    objs.erase(std::remove_if(objs.begin(), objs.end(),
        [&](const SceneObject& o) {
            bool remove = o.entityId != 0 && !reg.Has<MeshRendererComponent>(o.entityId);
            if (remove) {
                m_lastMeshGuid        .erase(o.entityId);
                m_lastAnimMeshGuid    .erase(o.entityId);
                m_lastMaterialName    .erase(o.entityId);
                m_lastSubMeshMaterials.erase(o.entityId);
            }
            return remove;
        }), objs.end());

    reg.Each<MeshRendererComponent>([&](Entity e, MeshRendererComponent& mr)
    {
        std::string loadPath;
        if (mr.meshGuid != 0)
        {
            const auto* meta = Krayon::AssetManager::Get().FindByGuid(mr.meshGuid);
            if (meta) { loadPath = meta->path; mr.meshPath = loadPath; }
        }
        if (loadPath.empty()) loadPath = mr.meshPath;
        if (loadPath.empty()) return;

        SceneObject* existingObj = nullptr;
        for (auto& o : m_scene.GetObjects())
            if (o.entityId == e) { existingObj = &o; break; }

        const uint64_t trackGuid = mr.meshGuid ? mr.meshGuid
            : Krayon::AssetManager::Get().GetGuid(loadPath);
        auto it = m_lastMeshGuid.find(e);
        const bool meshChanged = (it == m_lastMeshGuid.end() || it->second != trackGuid);

        if (mr.materialGuid != 0)
        {
            Krayon::MaterialAsset matAsset;
            if (Krayon::AssetManager::Get().ReadMaterialAsset(mr.materialGuid, matAsset))
            {
                if (!matAsset.name.empty())
                {
                    const std::string matKey = "__mat_" + std::to_string(mr.materialGuid);
                    if (!MaterialManager::Get().Has(matKey))
                    {
                        MaterialLayout matLayout;
                        matLayout.AddVec4 ("albedo")
                                 .AddFloat("roughness")
                                 .AddFloat("metallic")
                                 .AddFloat("emissiveIntensity")
                                 .AddFloat("alphaThreshold")
                                 .AddFloat("hasNormalMap")
                                 .AddFloat("hasRoughMap")
                                 .AddFloat("hasEmissiveMap")
                                 .AddFloat("normalStrength");
                        MaterialManager::Get().Add(matKey, matLayout, m_pipeline);
                    }
                    Material& rMat = MaterialManager::Get().Get(matKey);
                    rMat.SetVec4 ("albedo",
                        glm::vec4(matAsset.albedo[0], matAsset.albedo[1],
                                  matAsset.albedo[2], matAsset.albedo[3]));
                    rMat.SetFloat("roughness", matAsset.roughness);
                    rMat.SetFloat("metallic",  matAsset.metallic);
                    const float ei = std::max({ matAsset.emissive[0],
                                                matAsset.emissive[1],
                                                matAsset.emissive[2] });
                    rMat.SetFloat("emissiveIntensity", ei);
                    rMat.SetFloat("alphaThreshold",    0.0f);

                    if (matAsset.albedoTexGuid != 0)
                    {
                        auto& lastTex = m_lastMatTexGuid[matKey];
                        if (lastTex != matAsset.albedoTexGuid)
                        {
                            const Krayon::AssetMeta* texMeta =
                                Krayon::AssetManager::Get().FindByGuid(matAsset.albedoTexGuid);
                            if (texMeta)
                            {
                                try
                                {
                                    VulkanTexture& tex = TextureManager::Get().Load(texMeta->path);
                                    rMat.BindTexture(0, tex);
                                    lastTex = matAsset.albedoTexGuid;
                                }
                                catch (const std::exception&) {}
                            }
                        }
                    }

                    ApplyPBRMaps(rMat, matKey, matAsset);
                    mr.materialName = matKey;
                }
            }
        }

        
        for (size_t si = 0; si < mr.subMeshMaterialGuids.size(); si++)
        {
            uint64_t smGuid = mr.subMeshMaterialGuids[si];
            if (smGuid == 0) continue;

            const auto* smMeta = Krayon::AssetManager::Get().FindByGuid(smGuid);
            if (!smMeta) continue;

            std::string resolvedName;

            Krayon::MaterialAsset smAsset;
            if (Krayon::AssetManager::Get().ReadMaterialAsset(smGuid, smAsset) && !smAsset.name.empty())
            {
                
                resolvedName = "__mat_" + std::to_string(smGuid);
                if (!MaterialManager::Get().Has(resolvedName))
                {
                    MaterialLayout smLayout;
                    smLayout.AddVec4 ("albedo")
                            .AddFloat("roughness")
                            .AddFloat("metallic")
                            .AddFloat("emissiveIntensity")
                            .AddFloat("alphaThreshold")
                                 .AddFloat("hasNormalMap")
                                 .AddFloat("hasRoughMap")
                                 .AddFloat("hasEmissiveMap")
                                 .AddFloat("normalStrength");
                    MaterialManager::Get().Add(resolvedName, smLayout, m_pipeline);
                }
                Material& smMat = MaterialManager::Get().Get(resolvedName);
                smMat.SetVec4 ("albedo", glm::vec4(smAsset.albedo[0], smAsset.albedo[1],
                                                   smAsset.albedo[2], smAsset.albedo[3]));
                smMat.SetFloat("roughness",         smAsset.roughness);
                smMat.SetFloat("metallic",          smAsset.metallic);
                smMat.SetFloat("emissiveIntensity",
                    std::max({ smAsset.emissive[0], smAsset.emissive[1], smAsset.emissive[2] }));
                smMat.SetFloat("alphaThreshold", 0.0f);
                if (smAsset.albedoTexGuid != 0)
                {
                    auto& lastTex = m_lastMatTexGuid[resolvedName];
                    if (lastTex != smAsset.albedoTexGuid)
                    {
                        const Krayon::AssetMeta* tm =
                            Krayon::AssetManager::Get().FindByGuid(smAsset.albedoTexGuid);
                        if (tm)
                        {
                            try {
                                VulkanTexture& tex = TextureManager::Get().Load(tm->path);
                                smMat.BindTexture(0, tex);
                                lastTex = smAsset.albedoTexGuid;
                            } catch (const std::exception&) {}
                        }
                    }
                }
                ApplyPBRMaps(smMat, resolvedName, smAsset);
            }
            else if (smMeta->type == Krayon::AssetType::Texture)
            {
                
                
                
                resolvedName = "auto_" + smMeta->name;
                if (!MaterialManager::Get().Has(resolvedName))
                {
                    MaterialLayout smLayout;
                    smLayout.AddVec4 ("albedo")
                            .AddFloat("roughness")
                            .AddFloat("metallic")
                            .AddFloat("emissiveIntensity")
                            .AddFloat("alphaThreshold")
                                 .AddFloat("hasNormalMap")
                                 .AddFloat("hasRoughMap")
                                 .AddFloat("hasEmissiveMap")
                                 .AddFloat("normalStrength");
                    MaterialManager::Get().Add(resolvedName, smLayout, m_pipeline);
                    Material& smMat = MaterialManager::Get().Get(resolvedName);
                    smMat.SetVec4 ("albedo", glm::vec4(1.0f));
                    smMat.SetFloat("roughness",          0.5f);
                    smMat.SetFloat("metallic",           0.0f);
                    smMat.SetFloat("emissiveIntensity",  0.0f);
                    smMat.SetFloat("alphaThreshold",     0.0f);
                }
                Material& smMat = MaterialManager::Get().Get(resolvedName);
                auto& lastTex = m_lastMatTexGuid[resolvedName];
                if (lastTex != smGuid)
                {
                    try {
                        VulkanTexture& tex = TextureManager::Get().Load(smMeta->path);
                        smMat.BindTexture(0, tex);
                        lastTex = smGuid;
                    } catch (const std::exception&) {}
                }
            }
            else { continue; }  

            
            if (si < mr.subMeshMaterialNames.size())
                mr.subMeshMaterialNames[si] = resolvedName;
            else
            {
                mr.subMeshMaterialNames.resize(si + 1);
                mr.subMeshMaterialNames[si] = resolvedName;
            }
        }

        const std::string& curMatName = mr.materialName;
        auto matIt = m_lastMaterialName.find(e);
        const bool matChanged = existingObj &&
            (matIt == m_lastMaterialName.end() || matIt->second != curMatName);
        if (matChanged)
        {
            existingObj->material  = &MaterialManager::Get().Get(curMatName);
            m_lastMaterialName[e]  = curMatName;
        }

        if (existingObj && !meshChanged) return;

        Material& mat = MaterialManager::Get().Get(mr.materialName);
        VulkanMesh* meshPtr = nullptr;
        try { meshPtr = &MeshManager::Get().Load(loadPath); }
        catch (const std::exception&) { return; }
        VulkanMesh& mesh = *meshPtr;
        m_lastMeshGuid[e]     = trackGuid;
        m_lastMaterialName[e] = mr.materialName;

        if (existingObj)
        {
            existingObj->mesh            = &mesh;
            existingObj->material        = &mat;
            existingObj->subMeshMaterials.clear();  
            m_lastSubMeshMaterials.erase(e);        
            // ...existing code...
            return;
        }

        SceneObject& obj = m_scene.Add(mesh, mat);
        obj.entityId = e;
        m_lastMaterialName[e] = mr.materialName;
        // ...existing code...

        if (reg.Has<TransformComponent>(e))
            obj.worldMatrix = TransformSystem::GetWorldMatrix(e, reg);
    });


    reg.Each<MeshRendererComponent>([&](Entity e, MeshRendererComponent& mr)
    {
        if (mr.subMeshMaterialNames.empty()) return;

        SceneObject* obj = nullptr;
        for (auto& o : m_scene.GetObjects())
            if (o.entityId == e) { obj = &o; break; }
        if (!obj || !obj->mesh) return;

        const auto& names = mr.subMeshMaterialNames;
        auto& last = m_lastSubMeshMaterials[e];
        if (last == names) return;
        last = names;

        uint32_t smCount = obj->mesh->GetSubmeshCount();
        obj->subMeshMaterials.resize(smCount, nullptr);
        for (uint32_t si = 0; si < smCount; si++)
        {
            const std::string& name = (si < (uint32_t)names.size()) ? names[si] : "";
            if (!name.empty() && MaterialManager::Get().Has(name))
                obj->subMeshMaterials[si] = &MaterialManager::Get().Get(name);
            else
                obj->subMeshMaterials[si] = nullptr;
        }
    });

    // ── Sync per-submesh local transforms (and entity-backed sub-meshes) ─────
    reg.Each<MeshRendererComponent>([&](Entity e, MeshRendererComponent& mr)
    {
        bool hasEntityIds  = !mr.subMeshEntityIds.empty();
        bool hasTransforms = !mr.subMeshTransforms.empty();
        if (!hasEntityIds && !hasTransforms) return;

        SceneObject* obj = nullptr;
        for (auto& o : m_scene.GetObjects())
            if (o.entityId == e) { obj = &o; break; }
        if (!obj || !obj->mesh) return;

        uint32_t smCount = obj->mesh->GetSubmeshCount();
        obj->subMeshLocalTransforms.resize(smCount, glm::mat4(1.0f));

        // Compute parent world matrix fresh so entity-backed offsets are frame-accurate.
        glm::mat4 parentWorld = reg.Has<TransformComponent>(e)
            ? TransformSystem::GetWorldMatrix(e, reg) : glm::mat4(1.0f);
        glm::mat4 parentInv = glm::inverse(parentWorld);

        for (uint32_t si = 0; si < smCount; si++)
        {
            // Entity-backed sub-mesh: use that entity's world transform directly.
            if (si < (uint32_t)mr.subMeshEntityIds.size() && mr.subMeshEntityIds[si] != 0
                && reg.Has<TransformComponent>(mr.subMeshEntityIds[si]))
            {
                glm::mat4 entWorld = TransformSystem::GetWorldMatrix(mr.subMeshEntityIds[si], reg);
                obj->subMeshLocalTransforms[si] = parentInv * entWorld;
                continue;
            }
            // Fallback: manual SubMeshTransform offset.
            if (si < (uint32_t)mr.subMeshTransforms.size())
            {
                const auto& t = mr.subMeshTransforms[si];
                glm::mat4 T = glm::translate(glm::mat4(1.0f), t.position);
                glm::mat4 R = glm::eulerAngleYXZ(
                    glm::radians(t.rotEulerDeg.y),
                    glm::radians(t.rotEulerDeg.x),
                    glm::radians(t.rotEulerDeg.z));
                glm::mat4 S = glm::scale(glm::mat4(1.0f), t.scale);
                obj->subMeshLocalTransforms[si] = T * R * S;
            }
        }
    });

    for (auto& obj : m_scene.GetObjects())
    {
        if (obj.entityId == 0 || !reg.Has<TransformComponent>(obj.entityId)) continue;

        obj.isSkinned   = reg.Has<AnimationComponent>(obj.entityId);
        obj.worldMatrix = TransformSystem::GetWorldMatrix(obj.entityId, reg);
    }

    for (auto& obj : m_scene.GetObjects())
    {
        if (!obj.material) continue;
        float threshold = (obj.material->alphaMode == Material::AlphaMode::AlphaTest)
                          ? obj.material->alphaCutoff : 0.0f;
        obj.material->SetFloat("alphaThreshold", threshold);
    }

    if (m_playingInline)
    {
        reg.Each<CameraComponent>([&](Entity e, CameraComponent& cam)
        {
            if (!cam.isActive) return;
            if (!reg.Has<TransformComponent>(e)) return;
            glm::mat4 world = TransformSystem::GetWorldMatrix(e, reg);
            glm::vec3 pos   = glm::vec3(world[3]);
            float sz        = glm::length(glm::vec3(world[2]));
            glm::vec3 fwd   = (sz > 0.f) ? (-glm::vec3(world[2]) / sz) : glm::vec3(0, 0, -1);
            m_camera.fpsMode   = true;
            m_camera.fpsPos    = pos;
            m_camera.fov       = cam.fov;
            m_camera.nearPlane = cam.nearPlane;
            m_camera.farPlane  = cam.farPlane;
            m_camera.SetOrientation(std::atan2(-fwd.x, -fwd.z),
                                    std::asin(glm::clamp(fwd.y, -1.0f, 1.0f)));
        });
    }
}

void Application::UpdateAnimations(float dt)
{
    Registry& reg = *m_activeReg;
    // Remove SSBOs for entities that no longer have an AnimationComponent.
    // waitIdle ensures the GPU is done with the buffer before freeing it.
    {
        std::vector<uint32_t> toErase;
        for (auto& [eid, _] : m_entityBoneSSBO)
            if (!reg.Has<AnimationComponent>(eid))
                toErase.push_back(eid);
        if (!toErase.empty())
        {
            VulkanInstance::Get().GetDevice().waitIdle();
            for (auto eid : toErase)
            {
                m_entityBoneSSBO[eid]->Destroy();
                m_entityBoneSSBO.erase(eid);
            }
        }
    }

    static const std::vector<glm::mat4> s_identity(MAX_BONES, glm::mat4(1.0f));

    reg.Each<AnimationComponent>([&](Entity e, AnimationComponent& anim)
    {
        // Reset per-entity bone buffer to identity before writing this entity's bones
        std::vector<glm::mat4> boneMatrices(s_identity);
        if (!reg.Has<MeshRendererComponent>(e)) return;
        const auto& mr = reg.Get<MeshRendererComponent>(e);

        std::string loadPath;
        if (mr.meshGuid != 0)
        {
            const auto* meta = Krayon::AssetManager::Get().FindByGuid(mr.meshGuid);
            if (meta) loadPath = meta->path;
        }
        if (loadPath.empty()) loadPath = mr.meshPath;
        if (loadPath.empty() || !MeshManager::Get().Has(loadPath)) return;

        const uint64_t trackGuid = mr.meshGuid ? mr.meshGuid
            : Krayon::AssetManager::Get().GetGuid(loadPath);
        auto it = m_lastAnimMeshGuid.find(e);
        if (it == m_lastAnimMeshGuid.end() || it->second != trackGuid)
        {
            anim.currentTime = 0.0f;
            anim.activeIndex = -1;
            anim.prevIndex   = -1;
            anim.prevTime    = 0.0f;
            anim.blendTime   = 0.0f;
            anim.playing     = true;
            m_lastAnimMeshGuid[e] = trackGuid;
        }

        VulkanMesh& mesh = MeshManager::Get().Load(loadPath);
        if (mesh.GetBoneCount() == 0 || mesh.GetAnimationCount() == 0) return;

        int clipIdx = glm::clamp(anim.animIndex, 0,
            static_cast<int>(mesh.GetAnimationCount()) - 1);

        if (anim.activeIndex != clipIdx)
        {
            anim.prevIndex   = (anim.activeIndex >= 0) ? anim.activeIndex : clipIdx;
            anim.prevTime    = anim.currentTime;
            anim.currentTime = 0.0f;
            anim.activeIndex = clipIdx;
            anim.blendTime   = 0.0f;
            anim.playing     = true;
        }

        if (!anim.playing) return;

        const float step           = dt * anim.speed;
        const float activeDuration = mesh.GetAnimationDuration(
            static_cast<uint32_t>(anim.activeIndex));

        anim.currentTime += step;
        if (!anim.loop && anim.currentTime >= activeDuration)
        {
            anim.currentTime = activeDuration;
            anim.playing     = false;
        }

        if (anim.prevIndex >= 0 && anim.blendTime < anim.blendDuration)
        {
            anim.prevTime  += step;
            anim.blendTime += step;
        }

        std::vector<glm::mat4> boneMats;
        mesh.ComputeBoneTransforms(static_cast<uint32_t>(anim.activeIndex),
                                   anim.currentTime, boneMats);

        const float blendFactor = (anim.prevIndex >= 0 && anim.blendDuration > 0.0f)
            ? glm::clamp(anim.blendTime / anim.blendDuration, 0.0f, 1.0f)
            : 1.0f;

        if (blendFactor < 1.0f)
        {
            std::vector<glm::mat4> prevMats;
            mesh.ComputeBoneTransforms(static_cast<uint32_t>(anim.prevIndex),
                                       anim.prevTime, prevMats);

            const uint32_t boneCount = static_cast<uint32_t>(
                std::min(boneMats.size(), prevMats.size()));
            for (uint32_t i = 0; i < boneCount; ++i)
                boneMats[i] = prevMats[i] + blendFactor * (boneMats[i] - prevMats[i]);
        }
        else
        {
            anim.prevIndex = -1;
            anim.prevTime  = 0.0f;
        }

        for (uint32_t i = 0; i < static_cast<uint32_t>(boneMats.size()) && i < MAX_BONES; ++i)
            boneMatrices[i] = boneMats[i];

        m_entityBoneMatrices[e] = boneMatrices;

        // Create a per-entity BoneSSBO on first use and upload this frame's matrices
        auto& ssbo = m_entityBoneSSBO[e];
        if (!ssbo)
        {
            ssbo = std::make_unique<BoneSSBO>();
            ssbo->Create(m_pipeline);
        }
        ssbo->Upload(boneMatrices);
    });
}

bool Application::DirShadowNeedsRedraw()
{
    if (!m_shadowCacheEnabled) return true;

    // FNV-1a hash over all opaque non-skinned casters (mesh pointer + world
    // matrix). Any skinned caster is treated as animated and forces a redraw.
    uint64_t h = 1469598103934665603ull;
    auto fnv = [&h](const void* data, size_t n)
    {
        const uint8_t* p = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ull; }
    };

    bool animated = false;
    for (auto& o : m_scene.GetObjects())
    {
        if (!o.mesh) continue;
        if (o.isSkinned) { animated = true; break; }
        VulkanMesh* mp = o.mesh;
        fnv(&mp, sizeof(mp));
        fnv(&o.worldMatrix, sizeof(o.worldMatrix));
    }

    bool matricesChanged = false;
    for (uint32_t c = 0; c < VulkanShadowMap::NUM_CASCADES; ++c)
        if (m_cascadeMatrices[c] != m_prevCascadeMatrices[c]) { matricesChanged = true; break; }

    bool redraw = animated || matricesChanged || (h != m_prevCasterHash) || !m_dirShadowValid;

    // Remember this frame's state for the next comparison.
    m_prevCasterHash = h;
    for (uint32_t c = 0; c < VulkanShadowMap::NUM_CASCADES; ++c)
        m_prevCascadeMatrices[c] = m_cascadeMatrices[c];
    m_dirShadowValid = true;
    return redraw;
}

void Application::RenderShadowPasses(vk::CommandBuffer cmd)
{
    auto bindBonesFnShadow = [this, &cmd](uint32_t entityId)
    {
        auto it = m_entityBoneSSBO.find(entityId);
        if (it != m_entityBoneSSBO.end() && it->second)
            it->second->BindToSet(cmd, *m_shadowPipeline.GetLayout(), 0);
        else
            m_boneSSBO.BindToSet(cmd, *m_shadowPipeline.GetLayout(), 0);
    };

    // Skip the 4 directional cascade passes entirely when nothing that affects
    // them changed — last frame's depth map is still in ShaderReadOnly layout
    // and remains valid for sampling.
    if (DirShadowNeedsRedraw())
    {
        for (uint32_t c = 0; c < VulkanShadowMap::NUM_CASCADES; ++c)
        {
            m_shadowMap.BeginRendering(cmd, c);
            m_shadowPipeline.Bind(cmd);
            m_boneSSBO.BindToSet(cmd, *m_shadowPipeline.GetLayout(), 0);
            m_scene.DrawShadow(cmd, m_shadowPipeline, m_cascadeMatrices[c], &m_cascadeCullSpheres[c], &m_boneSSBO);
            m_scene.DrawSkinnedShadow(cmd, m_shadowPipeline, m_cascadeMatrices[c], bindBonesFnShadow);
            m_shadowMap.EndRendering(cmd);
        }
        m_shadowMap.TransitionToShaderRead(cmd);
    }

    if (m_hasSpotShadow)
    {
        m_spotShadowMap.BeginRendering(cmd, 0);
        m_shadowPipeline.Bind(cmd);
        m_boneSSBO.BindToSet(cmd, *m_shadowPipeline.GetLayout(), 0);
        m_scene.DrawShadow(cmd, m_shadowPipeline, m_activeSpotMatrix, nullptr, &m_boneSSBO);
        m_scene.DrawSkinnedShadow(cmd, m_shadowPipeline, m_activeSpotMatrix, bindBonesFnShadow);
        m_spotShadowMap.EndRendering(cmd);
    }
    m_spotShadowMap.TransitionToShaderRead(cmd);

    if (m_hasPointShadow)
    {
        auto faceMats = VulkanPointShadowMap::ComputeFaceMatrices(
            m_activePointPos, 0.1f, m_activePointFar);

        auto bindBonesFnPoint = [this, &cmd](uint32_t entityId)
        {
            auto it = m_entityBoneSSBO.find(entityId);
            if (it != m_entityBoneSSBO.end() && it->second)
                it->second->BindToSet(cmd, m_pointShadowPipeline.GetLayout(), 0);
            else
                m_boneSSBO.BindToSet(cmd, m_pointShadowPipeline.GetLayout(), 0);
        };

        for (uint32_t face = 0; face < 6; ++face)
        {
            m_pointShadowMap.BeginRendering(cmd, 0, face);
            m_pointShadowPipeline.Bind(cmd);
            m_boneSSBO.BindToSet(cmd, m_pointShadowPipeline.GetLayout(), 0);
            m_scene.DrawShadowPoint(cmd, m_pointShadowPipeline,
                faceMats[face], m_activePointPos, m_activePointFar);
            m_scene.DrawSkinnedShadowPoint(cmd, m_pointShadowPipeline,
                faceMats[face], m_activePointPos, m_activePointFar, bindBonesFnPoint);
            m_pointShadowMap.EndRendering(cmd);
        }
    }
    m_pointShadowMap.TransitionToShaderRead(cmd, 0);
}

void Application::RenderSceneView(vk::CommandBuffer cmd)
{
    const float aspect = SceneViewAspect();

    m_debugFb.BeginRendering(cmd);

    m_pipeline.Bind(cmd);
    m_boneSSBO.Bind(cmd, *m_pipeline.GetLayout());
    m_scene.Draw(cmd, m_pipeline, m_camera, aspect, m_lights);

    m_pipeline.Bind(cmd);
    m_scene.DrawSkinned(cmd, m_pipeline, m_camera, aspect, m_lights,
        [this, &cmd](uint32_t entityId)
        {
            auto it = m_entityBoneSSBO.find(entityId);
            if (it != m_entityBoneSSBO.end() && it->second)
                it->second->Bind(cmd, *m_pipeline.GetLayout());
            else
                m_boneSSBO.Bind(cmd, *m_pipeline.GetLayout()); // fallback: identity
        });

    m_skybox.Draw(cmd, m_camera.GetView(), m_camera.GetProjection(aspect), -m_activeDirDir);

    m_pipeline.BindTransparent(cmd);
    m_scene.DrawTransparent(cmd, m_pipeline, m_camera, aspect, m_lights);

    glm::mat4 vp = m_camera.GetProjection(aspect) * m_camera.GetView();

    Entity selected = m_editor.GetSelected();

    const GizmoVisibility gv = m_editor.GetGizmoVisibility();
    if (gv != GizmoVisibility::None)
        m_gizmo.Draw(cmd, m_registry, vp, selected,
                     gv == GizmoVisibility::SelectedOnly);

    m_uiRenderer.Draw(cmd, m_debugFb.GetExtent().width, m_debugFb.GetExtent().height);

    m_debugFb.EndRendering(cmd);
    m_debugFb.TransitionToShaderRead(cmd);
}

// Deferred scene view: geometry → G-buffer, SSAO, then a full-screen lighting
// composite into m_debugFb (re-emitting depth) so the rest of the pipeline
// (volumetrics / sun shafts / post-process / editor) is reused unchanged.
// v1 limitations: no procedural skybox (flat sky) and transparents are skipped.
void Application::RenderSceneViewDeferred(vk::CommandBuffer cmd)
{
    const float aspect = SceneViewAspect();
    glm::mat4 view  = m_camera.GetView();
    glm::mat4 proj  = m_camera.GetProjection(aspect);
    glm::mat4 invVP = glm::inverse(proj * view);

    // 1) Geometry pass → G-buffer (opaque + skinned).
    m_gbuffer.BeginRendering(cmd);
    m_gbufferPipeline.Bind(cmd);
    m_boneSSBO.Bind(cmd, *m_gbufferPipeline.GetLayout());
    m_scene.Draw(cmd, m_gbufferPipeline, m_camera, aspect, m_lights);
    m_gbufferPipeline.Bind(cmd);
    m_scene.DrawSkinned(cmd, m_gbufferPipeline, m_camera, aspect, m_lights,
        [this, &cmd](uint32_t entityId)
        {
            auto it = m_entityBoneSSBO.find(entityId);
            if (it != m_entityBoneSSBO.end() && it->second)
                it->second->Bind(cmd, *m_gbufferPipeline.GetLayout());
            else
                m_boneSSBO.Bind(cmd, *m_gbufferPipeline.GetLayout());
        });
    m_gbuffer.EndRendering(cmd);
    m_gbuffer.TransitionToShaderRead(cmd);

    // 2) SSAO from the G-buffer.
    float radius    = m_ssaoEnabled ? m_ssaoRadius    : 0.0f;
    float intensity = m_ssaoEnabled ? m_ssaoIntensity : 0.0f;
    m_ssao.Draw(cmd, m_gbuffer.GetNormalView(), m_gbuffer.GetDepthView(), m_gbuffer.GetSampler(),
                proj, view, radius, m_ssaoBias, intensity, m_ssaoPower);
    m_ssao.GetOutput().TransitionToShaderRead(cmd);

    // 3) Deferred lighting composite into m_debugFb. Draw the procedural skybox
    //    first so sky pixels (which the composite discards) show through.
    m_debugFb.BeginRendering(cmd);
    m_skybox.Draw(cmd, view, proj, -m_activeDirDir);
    m_deferredLighting.Record(cmd,
        m_gbuffer.GetAlbedoView(), m_gbuffer.GetNormalView(), m_gbuffer.GetMaterialView(),
        m_gbuffer.GetDepthView(), m_gbuffer.GetSampler(),
        m_ssao.GetOutput().GetColorView(), m_ssao.GetOutput().GetSampler(),
        invVP, m_lights.GetDescriptorSet());

    // Gizmos + UI overlay on top (depth-tested against the re-emitted depth).
    glm::mat4 vp = proj * view;
    Entity selected = m_editor.GetSelected();
    const GizmoVisibility gv = m_editor.GetGizmoVisibility();
    if (gv != GizmoVisibility::None)
        m_gizmo.Draw(cmd, m_registry, vp, selected, gv == GizmoVisibility::SelectedOnly);
    m_uiRenderer.Draw(cmd, m_debugFb.GetExtent().width, m_debugFb.GetExtent().height);

    m_debugFb.EndRendering(cmd);
    m_debugFb.TransitionToShaderRead(cmd);
}

void Application::RenderPostProcess(vk::CommandBuffer cmd)
{
    const float aspect = SceneViewAspect();
    m_postFb = &m_debugFb;

    {
        LightComponent* volComp = nullptr;
        m_registry.Each<LightComponent>([&](Entity e, LightComponent& l) {
            if (l.volumetricEnabled && !volComp) volComp = &l;
        });

        if (volComp)
        {
            glm::vec3 sunWorldDir = m_hasDir
                ? glm::normalize(-m_activeDirDir)
                : glm::vec3(0.0f, 1.0f, 0.0f);

            float sunAbove = m_hasDir
                ? glm::clamp(sunWorldDir.y * 5.0f + 1.0f, 0.0f, 1.0f)
                : 0.0f;

            glm::mat4 view  = m_camera.GetView();
            glm::mat4 proj  = m_camera.GetProjection(aspect);
            glm::mat4 invVP = glm::inverse(proj * view);

            m_volumetricLight.Draw(cmd,
                m_debugFb.GetColorView(),  m_debugFb.GetSampler(),
                m_debugFb.GetDepthView(),  m_debugFb.GetSampler(),
                m_shadowMap.GetArrayView(), m_shadowMap.GetCompareSampler(),
                invVP, m_cascadeMatrices, m_cascadeSplits,
                sunWorldDir,
                m_hasDir ? m_activeDirColor     : glm::vec3(0.0f),
                m_hasDir ? m_activeDirIntensity : 0.0f,
                m_camera.GetPosition(),
                m_lights.GetDescriptorSet(),
                volComp->volNumSteps,
                volComp->volDensity,
                volComp->volAbsorption,
                volComp->volG,
                volComp->volIntensity * sunAbove,
                volComp->volMaxDistance,
                volComp->volJitter,
                volComp->volTint);

            m_volumetricLight.GetOutput().TransitionToShaderRead(cmd);
            m_postFb = &m_volumetricLight.GetOutput();
        }
    }

    {
        VolumetricFogComponent* fogComp = nullptr;
        m_registry.Each<VolumetricFogComponent>([&](Entity e, VolumetricFogComponent& f) {
            if (f.enabled && !fogComp) fogComp = &f;
        });

        if (fogComp)
        {
            glm::mat4 view  = m_camera.GetView();
            glm::mat4 proj  = m_camera.GetProjection(aspect);
            glm::mat4 invVP = glm::inverse(proj * view);

            glm::vec3 sunTowardDir = m_hasDir
                ? glm::normalize(-m_activeDirDir)
                : glm::vec3(0.0f, 1.0f, 0.0f);
            float sunIntensity = m_hasDir ? m_activeDirIntensity : 0.0f;

            m_volumetricFog.Draw(cmd,
                m_postFb->GetColorView(), m_postFb->GetSampler(),
                m_debugFb.GetDepthView(), m_debugFb.GetSampler(),
                invVP, m_camera.GetPosition(),
                fogComp->fogColor,        fogComp->density,
                fogComp->horizonColor,    fogComp->heightFalloff,
                fogComp->sunScatterColor, fogComp->scatterStrength,
                sunTowardDir,             sunIntensity,
                fogComp->heightOffset,
                fogComp->maxFogAmount,
                fogComp->fogStart,
                fogComp->fogEnd,
                m_lights.GetDescriptorSet());

            m_volumetricFog.GetOutput().TransitionToShaderRead(cmd);
            m_postFb = &m_volumetricFog.GetOutput();
        }
    }

    {
        LightComponent* shaftsComp = nullptr;
        m_registry.Each<LightComponent>([&](Entity e, LightComponent& l) {
            if (l.sunShaftsEnabled && l.type == LightType::Directional && !shaftsComp)
                shaftsComp = &l;
        });

        if (shaftsComp && m_activeDirDir != glm::vec3(0.0f))
        {
            glm::mat4 vp          = m_camera.GetProjection(aspect) * m_camera.GetView();
            glm::vec3 sunWorldDir = glm::normalize(-m_activeDirDir);
            glm::vec4 sunClip     = vp * glm::vec4(sunWorldDir * 1000.0f, 1.0f);

            bool sunInFront = (sunClip.w > 0.0f) &&
                              (glm::dot(m_camera.GetForward(), sunWorldDir) > 0.0f);
            glm::vec2 sunUV = sunInFront
                ? glm::vec2(sunClip.x / sunClip.w, sunClip.y / sunClip.w) * 0.5f + 0.5f
                : glm::vec2(-10.0f);

            float sunHeight = glm::normalize(-m_activeDirDir).y;

            m_occlusionPass.Draw(cmd,
                m_debugFb.GetDepthView(), m_debugFb.GetSampler(),
                sunUV, shaftsComp->shaftsSunRadius, aspect);

            m_sunShafts.Draw(cmd,
                m_postFb->GetColorView(),  m_postFb->GetSampler(),
                m_occlusionPass.GetView(), m_debugFb.GetDepthView(),
                sunUV,
                shaftsComp->shaftsDensity,    shaftsComp->shaftsBloomScale,
                shaftsComp->shaftsDecay,      shaftsComp->shaftsWeight,
                shaftsComp->shaftsExposure,   shaftsComp->shaftsTint,
                sunHeight, aspect);

            m_sunShafts.GetOutput().TransitionToShaderRead(cmd);
            m_postFb = &m_sunShafts.GetOutput();
        }
    }

    
    if (m_ppDirty) RebuildPostProcessPasses();

    for (size_t i = 0; i < m_ppPasses.size(); ++i)
    {
        if (!m_ppGraph.effects[i].enabled) continue;
        if (!m_ppPasses[i]) continue;

        m_ppPasses[i]->Draw(cmd,
            m_postFb->GetColorView(),    m_postFb->GetSampler(),
            m_debugFb.GetDepthView(),    m_debugFb.GetSampler(),
            m_ppGraph.effects[i].params);
        m_ppPasses[i]->GetOutput().TransitionToShaderRead(cmd);
        m_postFb = &m_ppPasses[i]->GetOutput();
    }

    // Final display transform (exposure → ACES → gamma) — the only tonemap in
    // the frame. Everything before this point is linear HDR.
    {
        float tp[16] = {};
        tp[0] = m_tonemapExposure;
        m_tonemapPass.Draw(cmd,
            m_postFb->GetColorView(), m_postFb->GetSampler(),
            m_debugFb.GetDepthView(), m_debugFb.GetSampler(), tp);
        m_tonemapPass.GetOutput().TransitionToShaderRead(cmd);
        m_postFb = &m_tonemapPass.GetOutput();
    }

    m_editor.SetSceneViewFramebuffer(m_postFb);
}

void Application::RenderMainPass(vk::CommandBuffer cmd)
{
    VulkanRenderer::Get().BeginMainPass();
    m_debugOverlay.Draw(cmd, m_debugFb, m_shadowMap);
}

float Application::SceneViewAspect() const
{
    return static_cast<float>(m_debugFb.GetExtent().width) /
           static_cast<float>(m_debugFb.GetExtent().height);
}
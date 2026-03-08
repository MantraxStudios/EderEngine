#include "PlayerApp.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <glm/gtc/constants.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/euler_angles.hpp>
#include <algorithm>
#include <filesystem>
#include <iostream>

#include "Core/MaterialLayout.h"
#include "Core/MaterialManager.h"
#include "Core/MeshManager.h"
#include "Core/TextureManager.h"
#include "ECS/Components/VolumetricFogComponent.h"
#include "ECS/Components/AnimationComponent.h"
#include "ECS/Components/CameraComponent.h"
#include "ECS/Systems/TransformSystem.h"
#include "Renderer/VulkanRenderer.h"
#include "Renderer/Vulkan/VulkanInstance.h"
#include "Renderer/Vulkan/VulkanSwapchain.h"
#include "Renderer/Vulkan/VulkanSpotShadowMap.h"
#include <IO/AssetManager.h>
#include <IO/SceneSerializer.h>
#include "Physics/PhysicsSystem.h"
#include "Scripting/LuaScriptSystem.h"
#include "Audio/AudioSystem.h"
#include "UI/UISystem.h"

int PlayerApp::Run(const std::string& initialScene, const std::string& gameName)
{
    const std::string title = gameName.empty() ? "EderPlayer" : gameName;
    try { Init(title, initialScene); }
    catch (const std::exception& e)
    {
        std::cerr << "[EderPlayer] Init failed: " << e.what() << "\n";
        return -1;
    }

    uint64_t prevTime = SDL_GetTicks();
    static constexpr float PHYSICS_DT = 1.0f / 60.0f;
    static constexpr float MAX_DT     = 0.05f;
    float physAccum = 0.0f;

    while (m_running)
    {
        const uint64_t currTime = SDL_GetTicks();
        const float    dt       = std::min(
            static_cast<float>(currTime - prevTime) / 1000.0f, MAX_DT);
        prevTime = currTime;
        physAccum += dt;

        {
            std::string next = LuaScriptSystem::Get().ConsumePendingScene();
            if (!next.empty())
            {
                UISystem::Get().DestroyAll();
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
                UISystem::Get().Init();
                PhysicsSystem::Get().Init();
                LuaScriptSystem::Get().Init();
                AudioSystem::Get().Init();
                physAccum = 0.0f;
            }
        }

        PollEvents();
        ProcessInput(dt);

        VulkanRenderer::Get().BeginFrame();
        if (!VulkanRenderer::Get().IsFrameStarted())
            continue;

        UpdateLightBuffer();

        auto cmd = VulkanRenderer::Get().GetCommandBuffer();
        SyncECSToScene();
        UpdateAnimations(dt);

        if (physAccum >= PHYSICS_DT)
        {
            physAccum -= PHYSICS_DT;
            PhysicsSystem::Get().SyncActors(m_registry);
            PhysicsSystem::Get().SyncControllers(m_registry);
            PhysicsSystem::Get().Step(PHYSICS_DT);
            PhysicsSystem::Get().WriteBack(m_registry);
            PhysicsSystem::Get().WriteBackControllers(m_registry);
            PhysicsSystem::Get().DispatchEvents(m_registry);
            LuaScriptSystem::Get().Update(m_registry, PHYSICS_DT);
        }

        UISystem::Get().Update(dt);

        {
            glm::vec3 fwd = m_camera.GetForward() * -1.0f;
            glm::vec3 up  = m_camera.GetUp();
            AudioSystem::Get().SetListenerTransform(m_camera.fpsPos, fwd, up);
        }

        AudioSystem::Get().Update(m_registry, dt);

        RenderShadowPasses(cmd);
        RenderSceneView(cmd);
        RenderPostProcess(cmd);
        RenderMainPass(cmd);

        VulkanRenderer::Get().EndFrame();
    }

    Shutdown();
    return 0;
}

void PlayerApp::Init(const std::string& windowTitle, const std::string& initialScene)
{
    SDL_Init(SDL_INIT_VIDEO);

    m_window = SDL_CreateWindow(windowTitle.c_str(), 1280, 720,
        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    if (!m_window)
        throw std::runtime_error("SDL_CreateWindow failed");

    m_camera.fpsMode = true;
    m_camera.fpsPos  = { 0.0f, 2.0f, 12.0f };
    m_camera.SetOrientation(0.0f, 0.0f);
    SDL_SetWindowRelativeMouseMode(m_window, false);

    VulkanInstance::Get().Init(m_window);
    VulkanSwapchain::Get().Init(m_window);
    VulkanRenderer::Get().Init();
    VulkanRenderer::Get().SetWindow(m_window);

    m_pipeline.Create(
        "shaders/triangle.vert.spv",
        "shaders/triangle.frag.spv",
        VulkanSwapchain::Get().GetFormat(),
        VulkanRenderer::Get().GetDepthFormat());

    InitMaterials();

    m_shadowMap.Create(1024);
    m_shadowPipeline.Create(m_shadowMap.GetFormat());
    m_spotShadowMap.Create(1024);
    m_pointShadowMap.Create(512);
    m_pointShadowPipeline.Create(m_pointShadowMap.GetFormat());

    m_lights.Build(m_pipeline);
    m_lights.BindShadowMap     (m_shadowMap.GetArrayView(),          m_shadowMap.GetSampler());
    m_lights.BindSpotShadowMap (m_spotShadowMap.GetArrayView(),      m_spotShadowMap.GetSampler());
    m_lights.BindPointShadowMap(m_pointShadowMap.GetCubeArrayView(), m_pointShadowMap.GetSampler());

    m_skybox.Create(VulkanSwapchain::Get().GetFormat(),
                    VulkanRenderer::Get().GetDepthFormat());
    m_boneSSBO.Create(m_pipeline);
    InitPostProcess();

    if (!initialScene.empty())
    {
        auto& AM = Krayon::AssetManager::Get();
        const auto bytes = AM.GetBytes(initialScene);
        if (bytes.empty())
            std::cerr << "[EderPlayer] Warning: scene not found in PAK: " << initialScene << "\n";
        else
        {
            std::cout << "[EderPlayer] Loading scene: " << initialScene << "\n";
            Krayon::SceneSerializer::LoadFromBytes(bytes, m_registry, nullptr, &m_ppGraph);
            m_ppDirty = true;
        }
    }

    UISystem::Get().Init();
    UISystem::Get().SetWindow(m_window);
    m_uiRenderer.Create(VulkanSwapchain::Get().GetFormat(),
                        VulkanRenderer::Get().GetDepthFormat());
    PhysicsSystem::Get().Init();
    LuaScriptSystem::Get().Init();
    AudioSystem::Get().Init();
}

int PlayerApp::RunPreview(const std::string& scenePath, bool borderless)
{
    m_previewMode = true;
    m_borderless  = borderless;
    try { InitPreview(scenePath); }
    catch (const std::exception& e)
    {
        std::cerr << "[EderPlayer] Preview init failed: " << e.what() << "\n";
        return -1;
    }

    uint64_t prevTime = SDL_GetTicks();
    static constexpr float PHYSICS_DT = 1.0f / 60.0f;
    static constexpr float MAX_DT     = 0.05f;
    float physAccum = 0.0f;

    while (m_running)
    {
        const uint64_t currTime = SDL_GetTicks();
        const float    dt       = std::min(
            static_cast<float>(currTime - prevTime) / 1000.0f, MAX_DT);
        prevTime = currTime;
        physAccum += dt;

        {
            std::string next = LuaScriptSystem::Get().ConsumePendingScene();
            if (!next.empty())
            {
                UISystem::Get().DestroyAll();
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
                UISystem::Get().Init();
                PhysicsSystem::Get().Init();
                LuaScriptSystem::Get().Init();
                AudioSystem::Get().Init();
                physAccum = 0.0f;
            }
        }

        PollEvents();
        ProcessInput(dt);

        VulkanRenderer::Get().BeginFrame();
        if (!VulkanRenderer::Get().IsFrameStarted())
            continue;

        UpdateLightBuffer();
        auto cmd = VulkanRenderer::Get().GetCommandBuffer();
        SyncECSToScene();
        UpdateAnimations(dt);

        if (physAccum >= PHYSICS_DT)
        {
            physAccum -= PHYSICS_DT;
            PhysicsSystem::Get().SyncActors(m_registry);
            PhysicsSystem::Get().SyncControllers(m_registry);
            PhysicsSystem::Get().Step(PHYSICS_DT);
            PhysicsSystem::Get().WriteBack(m_registry);
            PhysicsSystem::Get().WriteBackControllers(m_registry);
            PhysicsSystem::Get().DispatchEvents(m_registry);
            LuaScriptSystem::Get().Update(m_registry, PHYSICS_DT);
        }

        UISystem::Get().Update(dt);

        {
            glm::vec3 fwd = m_camera.GetForward() * -1.0f;
            glm::vec3 up  = m_camera.GetUp();
            AudioSystem::Get().SetListenerTransform(m_camera.fpsPos, fwd, up);
        }

        AudioSystem::Get().Update(m_registry, dt);

        RenderShadowPasses(cmd);
        RenderSceneView(cmd);
        RenderPostProcess(cmd);
        RenderMainPass(cmd);

        VulkanRenderer::Get().EndFrame();
    }

    Shutdown();
    return 0;
}

void PlayerApp::InitPreview(const std::string& scenePath)
{
    SDL_Init(SDL_INIT_VIDEO);

    const SDL_WindowFlags wflags = m_borderless
        ? (SDL_WINDOW_VULKAN | SDL_WINDOW_BORDERLESS)
        : (SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    m_window = SDL_CreateWindow("EderPreview", 1280, 720, wflags);
    if (!m_window)
        throw std::runtime_error("SDL_CreateWindow (preview) failed");

    m_camera.fpsMode = true;
    m_camera.fpsPos  = { 0.0f, 2.0f, 12.0f };
    m_camera.SetOrientation(0.0f, 0.0f);
    SDL_SetWindowRelativeMouseMode(m_window, false);

    VulkanInstance::Get().Init(m_window);
    VulkanSwapchain::Get().Init(m_window);
    VulkanRenderer::Get().Init();
    VulkanRenderer::Get().SetWindow(m_window);

    m_pipeline.Create(
        "shaders/triangle.vert.spv",
        "shaders/triangle.frag.spv",
        VulkanSwapchain::Get().GetFormat(),
        VulkanRenderer::Get().GetDepthFormat());

    InitMaterials();

    m_shadowMap.Create(1024);
    m_shadowPipeline.Create(m_shadowMap.GetFormat());
    m_spotShadowMap.Create(1024);
    m_pointShadowMap.Create(512);
    m_pointShadowPipeline.Create(m_pointShadowMap.GetFormat());

    m_lights.Build(m_pipeline);
    m_lights.BindShadowMap     (m_shadowMap.GetArrayView(),          m_shadowMap.GetSampler());
    m_lights.BindSpotShadowMap (m_spotShadowMap.GetArrayView(),      m_spotShadowMap.GetSampler());
    m_lights.BindPointShadowMap(m_pointShadowMap.GetCubeArrayView(), m_pointShadowMap.GetSampler());

    m_skybox.Create(VulkanSwapchain::Get().GetFormat(),
                    VulkanRenderer::Get().GetDepthFormat());
    m_boneSSBO.Create(m_pipeline);
    InitPostProcess();

    if (!scenePath.empty())
    {
        std::string loadedName;
        if (!Krayon::SceneSerializer::Load(scenePath, m_registry, &loadedName, &m_ppGraph))
            std::cerr << "[EderPlayer] Warning: failed to load preview scene: " << scenePath << "\n";
        else
        {
            std::cout << "[EderPlayer] Preview scene loaded: " << loadedName << "\n";
            m_ppDirty = true;
        }
    }

    UISystem::Get().Init();
    UISystem::Get().SetWindow(m_window);
    m_uiRenderer.Create(VulkanSwapchain::Get().GetFormat(),
                        VulkanRenderer::Get().GetDepthFormat());
    PhysicsSystem::Get().Init();
    LuaScriptSystem::Get().Init();
    AudioSystem::Get().Init();
}

void PlayerApp::InitMaterials()
{
    MaterialLayout layout;
    layout.AddVec4 ("albedo")
          .AddFloat("roughness")
          .AddFloat("metallic")
          .AddFloat("emissiveIntensity")
          .AddFloat("alphaThreshold");

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

    try   { m_albedoTex.Load("assets/bush01.png"); }
    catch (...) { m_albedoTex.CreateDefault(); }

    def.BindTexture(0, m_albedoTex);
    m_floorMat.BindTexture(0, m_albedoTex);
    m_glassMat.BindTexture(0, m_albedoTex);
    m_glassMat2.BindTexture(0, m_albedoTex);
    m_glassMat3.BindTexture(0, m_albedoTex);
}

void PlayerApp::Shutdown()
{
    VulkanInstance::Get().GetDevice().waitIdle();

    m_blit.Destroy();
    m_ppPasses.clear();
    m_occlusionPass.Destroy();
    m_sunShafts.Destroy();
    m_volumetricLight.Destroy();
    m_volumetricFog.Destroy();
    m_sceneFb.Destroy();

    m_boneSSBO.Destroy();
    m_skybox.Destroy();
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
    m_glassMat.Destroy();
    m_glassMat2.Destroy();
    m_glassMat3.Destroy();
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

void PlayerApp::PollEvents()
{
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
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
        case SDL_EVENT_KEY_DOWN:
            if (event.key.scancode == SDL_SCANCODE_ESCAPE && !UISystem::Get().HasFocusedInput())
                m_running = false;
            break;
        default:
            break;
        }
    }
}

void PlayerApp::ProcessInput(float /*dt*/)
{
    // Camera is controlled entirely through scripting (CameraComponent).
    // No built-in FPS camera in the player runtime.
}

void PlayerApp::UpdateLightBuffer()
{
    auto& sc     = VulkanSwapchain::Get();
    float aspect = static_cast<float>(sc.GetExtent().width) /
                   static_cast<float>(sc.GetExtent().height);

    m_hasDir           = false;
    m_hasSpotShadow    = false;
    m_hasPointShadow   = false;
    m_activeDirDir     = glm::normalize(glm::vec3(-1.0f, -1.0f, -0.4f));
    m_activeDirColor   = glm::vec3(1.0f, 0.9f, 0.7f);
    m_activeDirIntensity = 1.0f;

    m_lights.ClearLights();

    int spotSlot = 0, pointSlot = 0;
    m_registry.Each<LightComponent>([&](Entity e, LightComponent& l)
    {
        if (!m_registry.Has<TransformComponent>(e)) return;

        if (l.type == LightType::Directional)
        {
            glm::mat4 mat = TransformSystem::GetWorldMatrix(e, m_registry);
            glm::vec3 dir = glm::normalize(glm::vec3(mat * glm::vec4(0, -1, 0, 0)));

            if (!m_hasDir)
            {
                m_activeDirDir          = dir;
                m_activeDirColor        = l.color;
                m_activeDirIntensity    = l.intensity;
                m_activeDirShadowDist   = l.shadowDistance;
                m_hasDir                = true;
            }

            float sunHorizon = glm::clamp(-dir.y * 5.0f + 1.0f, 0.0f, 1.0f);
            DirectionalLight dl{};
            dl.direction = dir;
            dl.color     = l.color;
            dl.intensity = l.intensity * sunHorizon;
            m_lights.AddDirectional(dl);
        }
        else if (l.type == LightType::Point)
        {
            PointLight pl{};
            pl.position  = glm::vec3(TransformSystem::GetWorldMatrix(e, m_registry)[3]);
            pl.color     = l.color;
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
        else if (l.type == LightType::Spot)
        {
            glm::mat4 mat = TransformSystem::GetWorldMatrix(e, m_registry);
            glm::vec3 dir = glm::normalize(glm::vec3(mat * glm::vec4(0, -1, 0, 0)));

            SpotLight sl{};
            sl.position  = glm::vec3(mat[3]);
            sl.direction = dir;
            sl.innerCos  = std::cos(glm::radians(l.innerConeAngle));
            sl.outerCos  = std::cos(glm::radians(l.outerConeAngle));
            sl.color     = l.color;
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
    });

    m_shadowMap.ComputeCascades(
        m_camera.GetView(), m_camera.GetProjection(aspect),
        m_activeDirDir, m_camera.nearPlane, m_activeDirShadowDist,
        m_cascadeMatrices, m_cascadeSplits);

    m_lights.SetCascadeData    (m_cascadeMatrices, m_cascadeSplits);
    m_lights.SetCameraForward  (m_camera.GetForward());
    m_lights.SetNearPlane      (m_camera.nearPlane);
    m_lights.SetSkyAmbient     (glm::vec3(0.15f, 0.18f, 0.22f), glm::vec3(0.05f, 0.04f, 0.03f));
    m_lights.Update            (m_camera.GetPosition());
}

void PlayerApp::SyncECSToScene()
{
    auto& objs = m_scene.GetObjects();
    objs.erase(std::remove_if(objs.begin(), objs.end(),
        [&](const SceneObject& o) {
            bool remove = o.entityId != 0 && !m_registry.Has<MeshRendererComponent>(o.entityId);
            if (remove) {
                m_lastMeshGuid        .erase(o.entityId);
                m_lastAnimMeshGuid    .erase(o.entityId);
                m_lastMaterialName    .erase(o.entityId);
                m_lastSubMeshMaterials.erase(o.entityId);
            }
            return remove;
        }), objs.end());

    m_registry.Each<MeshRendererComponent>([&](Entity e, MeshRendererComponent& mr)
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
                                 .AddFloat("alphaThreshold");
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
                    mr.materialName = matKey;
                }
            }
        }

        // ── Per-submesh materials ─────────────────────────────────
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
                            .AddFloat("alphaThreshold");
                    MaterialManager::Get().Add(resolvedName, smLayout, m_pipeline);
                }
                Material& smMat = MaterialManager::Get().Get(resolvedName);
                smMat.SetVec4 ("albedo", glm::vec4(smAsset.albedo[0], smAsset.albedo[1],
                                                   smAsset.albedo[2], smAsset.albedo[3]));
                smMat.SetFloat("roughness",        smAsset.roughness);
                smMat.SetFloat("metallic",         smAsset.metallic);
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
                        if (tm) try {
                            VulkanTexture& tex = TextureManager::Get().Load(tm->path);
                            smMat.BindTexture(0, tex);
                            lastTex = smAsset.albedoTexGuid;
                        } catch (const std::exception&) {}
                    }
                }
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
                            .AddFloat("alphaThreshold");
                    MaterialManager::Get().Add(resolvedName, smLayout, m_pipeline);
                    Material& smMat = MaterialManager::Get().Get(resolvedName);
                    smMat.SetVec4 ("albedo", glm::vec4(1.0f));
                    smMat.SetFloat("roughness",         0.5f);
                    smMat.SetFloat("metallic",          0.0f);
                    smMat.SetFloat("emissiveIntensity", 0.0f);
                    smMat.SetFloat("alphaThreshold",    0.0f);
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
            return;
        }

        SceneObject& obj = m_scene.Add(mesh, mat);
        obj.entityId = e;
        m_lastMaterialName[e] = mr.materialName;

    });

    // ── Sync subMeshMaterials to SceneObjects ─────────────────────────────────
    m_registry.Each<MeshRendererComponent>([&](Entity e, MeshRendererComponent& mr)
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

    // ── Sync per-submesh local transforms ────────────────────────────────────
    m_registry.Each<MeshRendererComponent>([&](Entity e, MeshRendererComponent& mr)
    {
        if (mr.subMeshTransforms.empty()) return;
        SceneObject* obj = nullptr;
        for (auto& o : m_scene.GetObjects())
            if (o.entityId == e) { obj = &o; break; }
        if (!obj || !obj->mesh) return;

        uint32_t smCount = obj->mesh->GetSubmeshCount();
        obj->subMeshLocalTransforms.resize(smCount, glm::mat4(1.0f));
        for (uint32_t si = 0; si < smCount && si < (uint32_t)mr.subMeshTransforms.size(); si++)
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
    });

    for (auto& obj : m_scene.GetObjects())
    {
        if (obj.entityId == 0 || !m_registry.Has<TransformComponent>(obj.entityId)) continue;

        obj.isSkinned   = m_registry.Has<AnimationComponent>(obj.entityId);
        obj.worldMatrix = TransformSystem::GetWorldMatrix(obj.entityId, m_registry);
    }

    for (auto& obj : m_scene.GetObjects())
    {
        if (!obj.material) continue;
        float threshold = (obj.material->alphaMode == Material::AlphaMode::AlphaTest)
                          ? obj.material->alphaCutoff : 0.0f;
        obj.material->SetFloat("alphaThreshold", threshold);
    }

    m_registry.Each<CameraComponent>([&](Entity e, CameraComponent& cam)
    {
        if (!cam.isActive) return;
        if (!m_registry.Has<TransformComponent>(e)) return;
        glm::mat4 world = TransformSystem::GetWorldMatrix(e, m_registry);
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

void PlayerApp::UpdateAnimations(float dt)
{
    // Remove SSBOs for entities that no longer have an AnimationComponent.
    // waitIdle ensures the GPU is done with the buffer before freeing it.
    {
        std::vector<uint32_t> toErase;
        for (auto& [eid, _] : m_entityBoneSSBO)
            if (!m_registry.Has<AnimationComponent>(eid))
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

    m_registry.Each<AnimationComponent>([&](Entity e, AnimationComponent& anim)
    {
        std::vector<glm::mat4> boneMatrices(s_identity);
        if (!m_registry.Has<MeshRendererComponent>(e)) return;
        const auto& mr = m_registry.Get<MeshRendererComponent>(e);

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

        auto& ssbo = m_entityBoneSSBO[e];
        if (!ssbo)
        {
            ssbo = std::make_unique<BoneSSBO>();
            ssbo->Create(m_pipeline);
        }
        ssbo->Upload(boneMatrices);
    });
}

void PlayerApp::RenderShadowPasses(vk::CommandBuffer cmd)
{
    for (uint32_t c = 0; c < VulkanShadowMap::NUM_CASCADES; ++c)
    {
        m_shadowMap.BeginRendering(cmd, c);
        m_shadowPipeline.Bind(cmd);
        m_boneSSBO.BindToSet(cmd, *m_shadowPipeline.GetLayout(), 0);
        m_scene.DrawShadow(cmd, m_shadowPipeline, m_cascadeMatrices[c]);
        m_shadowMap.EndRendering(cmd);
    }
    m_shadowMap.TransitionToShaderRead(cmd);

    if (m_hasSpotShadow)
    {
        m_spotShadowMap.BeginRendering(cmd, 0);
        m_shadowPipeline.Bind(cmd);
        m_boneSSBO.BindToSet(cmd, *m_shadowPipeline.GetLayout(), 0);
        m_scene.DrawShadow(cmd, m_shadowPipeline, m_activeSpotMatrix);
        m_spotShadowMap.EndRendering(cmd);
    }
    m_spotShadowMap.TransitionToShaderRead(cmd);

    if (m_hasPointShadow)
    {
        auto faceMats = VulkanPointShadowMap::ComputeFaceMatrices(
            m_activePointPos, 0.1f, m_activePointFar);
        for (uint32_t face = 0; face < 6; ++face)
        {
            m_pointShadowMap.BeginRendering(cmd, 0, face);
            m_pointShadowPipeline.Bind(cmd);
            m_boneSSBO.BindToSet(cmd, m_pointShadowPipeline.GetLayout(), 0);
            m_scene.DrawShadowPoint(cmd, m_pointShadowPipeline,
                faceMats[face], m_activePointPos, m_activePointFar);
            m_pointShadowMap.EndRendering(cmd);
        }
    }
    m_pointShadowMap.TransitionToShaderRead(cmd, 0);
}

void PlayerApp::InitPostProcess()
{
    auto& sc      = VulkanSwapchain::Get();
    auto& rd      = VulkanRenderer::Get();
    uint32_t w    = sc.GetExtent().width;
    uint32_t h    = sc.GetExtent().height;
    auto depthFmt = rd.GetDepthFormat();
    auto colorFmt = vk::Format::eB8G8R8A8Unorm;

    m_sceneFb.Create(w, h, colorFmt, depthFmt);
    m_occlusionPass.Create(w, h);
    m_sunShafts.Create(colorFmt, w, h);
    m_volumetricLight.Create(colorFmt, w, h,
                             *m_pipeline.GetLightDescriptorSetLayout());
    m_volumetricFog.Create(colorFmt, w, h,
                           *m_pipeline.GetLightDescriptorSetLayout());
    m_blit.Create(sc.GetFormat(), depthFmt);
}

void PlayerApp::RebuildPostProcessPasses()
{
    VulkanInstance::Get().GetDevice().waitIdle();

    m_ppPasses.clear();
    m_ppPasses.reserve(m_ppGraph.effects.size());

    uint32_t w = m_sceneFb.GetExtent().width;
    uint32_t h = m_sceneFb.GetExtent().height;

    for (const auto& fx : m_ppGraph.effects)
    {
        auto pass = std::make_unique<VulkanPostProcessPass>();
        try {
            pass->Create(m_sceneFb.GetColorFormat(), w, h, fx.fragShaderPath);
            m_ppPasses.push_back(std::move(pass));
        }
        catch (const std::exception& e) {
            std::cerr << "[PostProcess] Failed to create '" << fx.name << "': " << e.what() << "\n";
            m_ppPasses.push_back(nullptr);
        }
    }

    m_ppDirty = false;
}

void PlayerApp::RenderSceneView(vk::CommandBuffer cmd)
{
    auto& sc     = VulkanSwapchain::Get();
    float aspect = static_cast<float>(sc.GetExtent().width) /
                   static_cast<float>(sc.GetExtent().height);

    m_sceneFb.BeginRendering(cmd);

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
                m_boneSSBO.Bind(cmd, *m_pipeline.GetLayout());
        });

    m_skybox.Draw(cmd, m_camera.GetView(), m_camera.GetProjection(aspect), -m_activeDirDir);

    m_pipeline.BindTransparent(cmd);
    m_boneSSBO.Bind(cmd, *m_pipeline.GetLayout());
    m_scene.DrawTransparent(cmd, m_pipeline, m_camera, aspect, m_lights);

    m_uiRenderer.Draw(cmd, m_sceneFb.GetExtent().width, m_sceneFb.GetExtent().height);

    m_sceneFb.EndRendering(cmd);
    m_sceneFb.TransitionToShaderRead(cmd);
}

void PlayerApp::RenderPostProcess(vk::CommandBuffer cmd)
{
    auto& sc     = VulkanSwapchain::Get();
    float aspect = static_cast<float>(sc.GetExtent().width) /
                   static_cast<float>(sc.GetExtent().height);

    m_postFb = &m_sceneFb;

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
                m_sceneFb.GetColorView(),  m_sceneFb.GetSampler(),
                m_sceneFb.GetDepthView(),  m_sceneFb.GetSampler(),
                m_shadowMap.GetArrayView(), m_shadowMap.GetSampler(),
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
                m_sceneFb.GetDepthView(), m_sceneFb.GetSampler(),
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
                m_sceneFb.GetDepthView(), m_sceneFb.GetSampler(),
                sunUV, shaftsComp->shaftsSunRadius, aspect);

            m_sunShafts.Draw(cmd,
                m_postFb->GetColorView(),  m_postFb->GetSampler(),
                m_occlusionPass.GetView(), m_sceneFb.GetDepthView(),
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
            m_sceneFb.GetDepthView(),    m_sceneFb.GetSampler(),
            m_ppGraph.effects[i].params);
        m_ppPasses[i]->GetOutput().TransitionToShaderRead(cmd);
        m_postFb = &m_ppPasses[i]->GetOutput();
    }
}

void PlayerApp::RenderMainPass(vk::CommandBuffer cmd)
{
    VulkanRenderer::Get().BeginMainPass();
    m_blit.Draw(cmd, m_postFb->GetColorView(), m_postFb->GetSampler());
}
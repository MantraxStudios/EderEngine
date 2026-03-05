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

// ─────────────────────────────────────────────────────────────────────────────
//  Run
// ─────────────────────────────────────────────────────────────────────────────

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
    float    physAccum = 0.f;
    static constexpr float PHYSICS_DT = 1.f / 60.f;
    static constexpr float MAX_DT     = 0.05f;

    while (m_running)
    {
        const uint64_t currTime = SDL_GetTicks();
        const float    dt       = std::min(
            static_cast<float>(currTime - prevTime) / 1000.0f, MAX_DT);
        prevTime = currTime;

        PollEvents();
        ProcessInput(dt);

        VulkanRenderer::Get().BeginFrame();
        if (!VulkanRenderer::Get().IsFrameStarted())
            continue;   // swapchain rebuilt — skip frame, no resize needed

        UpdateLightBuffer();

        auto cmd = VulkanRenderer::Get().GetCommandBuffer();
        SyncECSToScene();
        UpdateAnimations(dt);

        physAccum += dt;
        while (physAccum >= PHYSICS_DT)
        {
            PhysicsSystem::Get().SyncActors(m_registry);
            PhysicsSystem::Get().SyncControllers(m_registry);
            PhysicsSystem::Get().Step(PHYSICS_DT);
            PhysicsSystem::Get().WriteBack(m_registry);
            PhysicsSystem::Get().WriteBackControllers(m_registry);
            PhysicsSystem::Get().DispatchEvents(m_registry);
            LuaScriptSystem::Get().Update(m_registry, PHYSICS_DT);
            physAccum -= PHYSICS_DT;
        }

        RenderShadowPasses(cmd);
        RenderMainPass(cmd);

        VulkanRenderer::Get().EndFrame();
    }

    Shutdown();
    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Init
// ─────────────────────────────────────────────────────────────────────────────

void PlayerApp::Init(const std::string& windowTitle, const std::string& initialScene)
{
    SDL_Init(SDL_INIT_VIDEO);

    m_window = SDL_CreateWindow(windowTitle.c_str(), 1280, 720,
        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    if (!m_window)
        throw std::runtime_error("SDL_CreateWindow failed");

    // Camera — FPS mode
    m_camera.fpsMode = true;
    m_camera.fpsPos  = { 0.0f, 2.0f, 12.0f };
    m_camera.SetOrientation(0.0f, 0.0f);
    SDL_SetWindowRelativeMouseMode(m_window, false);

    VulkanInstance::Get().Init(m_window);
    VulkanSwapchain::Get().Init(m_window);
    VulkanRenderer::Get().Init();
    VulkanRenderer::Get().SetWindow(m_window);

    // Main PBR pipeline
    m_pipeline.Create(
        "shaders/triangle.vert.spv",
        "shaders/triangle.frag.spv",
        VulkanSwapchain::Get().GetFormat(),
        VulkanRenderer::Get().GetDepthFormat());

    InitMaterials();

    // Shadow system
    m_shadowMap.Create(1024);
    m_shadowPipeline.Create(m_shadowMap.GetFormat());
    m_spotShadowMap.Create(1024);
    m_pointShadowMap.Create(512);
    m_pointShadowPipeline.Create(m_pointShadowMap.GetFormat());

    m_lights.Build(m_pipeline);
    m_lights.BindShadowMap     (m_shadowMap.GetArrayView(),          m_shadowMap.GetSampler());
    m_lights.BindSpotShadowMap (m_spotShadowMap.GetArrayView(),      m_spotShadowMap.GetSampler());
    m_lights.BindPointShadowMap(m_pointShadowMap.GetCubeArrayView(), m_pointShadowMap.GetSampler());

    // Skybox + bone buffer
    m_skybox.Create(VulkanSwapchain::Get().GetFormat(),
                    VulkanRenderer::Get().GetDepthFormat());
    m_boneSSBO.Create(m_pipeline);

    // Load initial scene from PAK if provided
    if (!initialScene.empty())
    {
        auto& AM = Krayon::AssetManager::Get();
        const auto bytes = AM.GetBytes(initialScene);
        if (bytes.empty())
            std::cerr << "[EderPlayer] Warning: scene not found in PAK: " << initialScene << "\n";
        else
        {
            std::cout << "[EderPlayer] Loading scene: " << initialScene << "\n";
            Krayon::SceneSerializer::LoadFromBytes(bytes, m_registry);
        }
    }

    PhysicsSystem::Get().Init();
    LuaScriptSystem::Get().Init();
}

// ─────────────────────────────────────────────────────────────────────────────
//  RunPreview / InitPreview  — editor play mode
//
//  Assets are already initialised with a raw (non-PAK) workdir by
//  PlayerMain before this is called.  We load the scene from the absolute
//  path the editor wrote and run the full physics + scripting loop.
// ─────────────────────────────────────────────────────────────────────────────

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
    float    physAccum = 0.f;
    static constexpr float PHYSICS_DT = 1.f / 60.f;
    static constexpr float MAX_DT     = 0.05f;

    while (m_running)
    {
        const uint64_t currTime = SDL_GetTicks();
        const float    dt       = std::min(
            static_cast<float>(currTime - prevTime) / 1000.0f, MAX_DT);
        prevTime = currTime;

        PollEvents();
        ProcessInput(dt);

        VulkanRenderer::Get().BeginFrame();
        if (!VulkanRenderer::Get().IsFrameStarted())
            continue;

        UpdateLightBuffer();
        auto cmd = VulkanRenderer::Get().GetCommandBuffer();
        SyncECSToScene();
        UpdateAnimations(dt);

        physAccum += dt;
        while (physAccum >= PHYSICS_DT)
        {
            PhysicsSystem::Get().SyncActors(m_registry);
            PhysicsSystem::Get().SyncControllers(m_registry);
            PhysicsSystem::Get().Step(PHYSICS_DT);
            PhysicsSystem::Get().WriteBack(m_registry);
            PhysicsSystem::Get().WriteBackControllers(m_registry);
            PhysicsSystem::Get().DispatchEvents(m_registry);
            LuaScriptSystem::Get().Update(m_registry, PHYSICS_DT);
            physAccum -= PHYSICS_DT;
        }

        RenderShadowPasses(cmd);
        RenderMainPass(cmd);

        VulkanRenderer::Get().EndFrame();
    }

    Shutdown();
    return 0;
}

void PlayerApp::InitPreview(const std::string& scenePath)
{
    SDL_Init(SDL_INIT_VIDEO);

    // Borderless when embedded into the editor viewport; normal window otherwise.
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

    // Load scene from absolute file path (loose-file mode)
    if (!scenePath.empty())
    {
        std::string loadedName;
        if (!Krayon::SceneSerializer::Load(scenePath, m_registry, &loadedName))
            std::cerr << "[EderPlayer] Warning: failed to load preview scene: " << scenePath << "\n";
        else
            std::cout << "[EderPlayer] Preview scene loaded: " << loadedName << "\n";
    }

    PhysicsSystem::Get().Init();
    LuaScriptSystem::Get().Init();
}

// ─────────────────────────────────────────────────────────────────────────────
//  InitMaterials
// ─────────────────────────────────────────────────────────────────────────────

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

    // Try to load a default albedo texture; fall back to a solid white if absent
    try   { m_albedoTex.Load("assets/bush01.png"); }
    catch (...) { m_albedoTex.CreateDefault(); }

    def.BindTexture(0, m_albedoTex);
    m_floorMat.BindTexture(0, m_albedoTex);
    m_glassMat.BindTexture(0, m_albedoTex);
    m_glassMat2.BindTexture(0, m_albedoTex);
    m_glassMat3.BindTexture(0, m_albedoTex);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Shutdown
// ─────────────────────────────────────────────────────────────────────────────

void PlayerApp::Shutdown()
{
    VulkanInstance::Get().GetDevice().waitIdle();

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

    SDL_DestroyWindow(m_window);
    SDL_Quit();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Input & Events
// ─────────────────────────────────────────────────────────────────────────────

void PlayerApp::PollEvents()
{
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
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
            if (event.key.scancode == SDL_SCANCODE_ESCAPE)
                m_running = false;
            break;
        default:
            break;
        }
    }
}

void PlayerApp::ProcessInput(float dt)
{
    float mx, my;
    const bool rmb = (SDL_GetMouseState(&mx, &my) & SDL_BUTTON_RMASK) != 0;

    if (rmb && !m_lookActive)
    {
        m_lookActive = true;
        SDL_SetWindowRelativeMouseMode(m_window, true);
        SDL_RaiseWindow(m_window);
        SDL_GetRelativeMouseState(&mx, &my);
        m_mouseDX = m_mouseDY = 0.0f;
    }
    else if (!rmb && m_lookActive)
    {
        m_lookActive = false;
        SDL_SetWindowRelativeMouseMode(m_window, false);
        SDL_GetRelativeMouseState(&mx, &my);
        m_mouseDX = m_mouseDY = 0.0f;
    }
    else if (m_lookActive)
    {
        SDL_GetRelativeMouseState(&m_mouseDX, &m_mouseDY);
    }
    else
    {
        m_mouseDX = m_mouseDY = 0.0f;
    }

    if (!m_lookActive) return;

    m_camera.FPSLook(m_mouseDX, m_mouseDY);

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

// ─────────────────────────────────────────────────────────────────────────────
//  UpdateLightBuffer (identical to Application)
// ─────────────────────────────────────────────────────────────────────────────

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
                m_activeDirDir       = dir;
                m_activeDirColor     = l.color;
                m_activeDirIntensity = l.intensity;
                m_hasDir             = true;
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
        m_activeDirDir, m_camera.nearPlane, m_camera.farPlane,
        m_cascadeMatrices, m_cascadeSplits);

    m_lights.SetCascadeData    (m_cascadeMatrices, m_cascadeSplits);
    m_lights.SetCameraForward  (m_camera.GetForward());
    m_lights.Update            (m_camera.GetPosition());
    m_lights.SetSkyAmbient     (glm::vec3(0.04f), glm::vec3(0.04f));
}

// ─────────────────────────────────────────────────────────────────────────────
//  SyncECSToScene (identical to Application)
// ─────────────────────────────────────────────────────────────────────────────

void PlayerApp::SyncECSToScene()
{
    auto& objs = m_scene.GetObjects();
    objs.erase(std::remove_if(objs.begin(), objs.end(),
        [&](const SceneObject& o) {
            bool remove = o.entityId != 0 && !m_registry.Has<MeshRendererComponent>(o.entityId);
            if (remove) {
                m_lastMeshGuid    .erase(o.entityId);
                m_lastAnimMeshGuid.erase(o.entityId);
                m_lastMaterialName.erase(o.entityId);
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
                    if (!MaterialManager::Get().Has(matAsset.name))
                    {
                        MaterialLayout matLayout;
                        matLayout.AddVec4 ("albedo")
                                 .AddFloat("roughness")
                                 .AddFloat("metallic")
                                 .AddFloat("emissiveIntensity")
                                 .AddFloat("alphaThreshold");
                        MaterialManager::Get().Add(matAsset.name, matLayout, m_pipeline);
                    }
                    Material& rMat = MaterialManager::Get().Get(matAsset.name);
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
                        auto& lastTex = m_lastMatTexGuid[matAsset.name];
                        if (lastTex != matAsset.albedoTexGuid)
                        {
                            const Krayon::AssetMeta* texMeta =
                                Krayon::AssetManager::Get().FindByGuid(matAsset.albedoTexGuid);
                            if (texMeta)
                            {
                                VulkanTexture& tex = TextureManager::Get().Load(texMeta->path);
                                rMat.BindTexture(0, tex);
                                lastTex = matAsset.albedoTexGuid;
                            }
                        }
                    }
                    mr.materialName = matAsset.name;
                }
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

        Material&   mat  = MaterialManager::Get().Get(mr.materialName);
        VulkanMesh& mesh = MeshManager::Get().Load(loadPath);
        m_lastMeshGuid[e]     = trackGuid;
        m_lastMaterialName[e] = mr.materialName;

        if (existingObj)
        {
            existingObj->mesh     = &mesh;
            existingObj->material = &mat;
            return;
        }

        SceneObject& obj = m_scene.Add(mesh, mat);
        obj.entityId = e;
        m_lastMaterialName[e] = mr.materialName;

        if (m_registry.Has<TransformComponent>(e))
        {
            const auto& t    = m_registry.Get<TransformComponent>(e);
            obj.transform.position = t.position;
            obj.transform.rotation = t.rotation;
            obj.transform.scale    = t.scale;
        }
    });

    for (auto& obj : m_scene.GetObjects())
    {
        if (obj.entityId == 0 || !m_registry.Has<TransformComponent>(obj.entityId)) continue;

        obj.isSkinned = m_registry.Has<AnimationComponent>(obj.entityId);

        glm::mat4 world = TransformSystem::GetWorldMatrix(obj.entityId, m_registry);

        obj.transform.position = glm::vec3(world[3]);
        obj.transform.scale.x  = glm::length(glm::vec3(world[0]));
        obj.transform.scale.y  = glm::length(glm::vec3(world[1]));
        obj.transform.scale.z  = glm::length(glm::vec3(world[2]));
        glm::mat4 rot = world;
        rot[0] = glm::vec4(glm::vec3(world[0]) / obj.transform.scale.x, 0.0f);
        rot[1] = glm::vec4(glm::vec3(world[1]) / obj.transform.scale.y, 0.0f);
        rot[2] = glm::vec4(glm::vec3(world[2]) / obj.transform.scale.z, 0.0f);
        rot[3] = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        float yRad, xRad, zRad;
        glm::extractEulerAngleYXZ(rot, yRad, xRad, zRad);
        obj.transform.rotation = glm::degrees(glm::vec3(xRad, yRad, zRad));
    }

    for (auto& obj : m_scene.GetObjects())
    {
        if (!obj.material) continue;
        float threshold = (obj.material->alphaMode == Material::AlphaMode::AlphaTest)
                          ? obj.material->alphaCutoff : 0.0f;
        obj.material->SetFloat("alphaThreshold", threshold);
    }

    // Active CameraComponent entity → m_camera
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
        m_camera.SetOrientation(std::atan2(fwd.x, -fwd.z),
                                std::asin(glm::clamp(fwd.y, -1.0f, 1.0f)));
    });
}

// ─────────────────────────────────────────────────────────────────────────────
//  UpdateAnimations (identical to Application)
// ─────────────────────────────────────────────────────────────────────────────

void PlayerApp::UpdateAnimations(float dt)
{
    std::vector<glm::mat4> boneMatrices(MAX_BONES, glm::mat4(1.0f));

    m_registry.Each<AnimationComponent>([&](Entity e, AnimationComponent& anim)
    {
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
    });

    static const std::vector<glm::mat4> s_identity(MAX_BONES, glm::mat4(1.0f));
    m_boneSSBO.Upload(s_identity);
}

// ─────────────────────────────────────────────────────────────────────────────
//  RenderShadowPasses (identical to Application)
// ─────────────────────────────────────────────────────────────────────────────

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

// ─────────────────────────────────────────────────────────────────────────────
//  RenderMainPass — renders scene directly to the swapchain (no editor overlay)
// ─────────────────────────────────────────────────────────────────────────────

void PlayerApp::RenderMainPass(vk::CommandBuffer cmd)
{
    auto& sc     = VulkanSwapchain::Get();
    float aspect = static_cast<float>(sc.GetExtent().width) /
                   static_cast<float>(sc.GetExtent().height);

    VulkanRenderer::Get().BeginMainPass();

    // Opaque geometry
    m_pipeline.Bind(cmd);
    m_boneSSBO.Bind(cmd, *m_pipeline.GetLayout());
    m_scene.Draw(cmd, m_pipeline, m_camera, aspect, m_lights);

    // Skinned (animated) objects
    m_pipeline.Bind(cmd);
    m_scene.DrawSkinned(cmd, m_pipeline, m_camera, aspect, m_lights,
        [this, &cmd](uint32_t entityId)
        {
            auto it = m_entityBoneMatrices.find(entityId);
            if (it != m_entityBoneMatrices.end())
                m_boneSSBO.Upload(it->second);
            m_boneSSBO.Bind(cmd, *m_pipeline.GetLayout());
        });

    // Skybox
    m_skybox.Draw(cmd, m_camera.GetView(), m_camera.GetProjection(aspect), -m_activeDirDir);

    // Transparents
    m_pipeline.BindTransparent(cmd);
    m_boneSSBO.Bind(cmd, *m_pipeline.GetLayout());
    m_scene.DrawTransparent(cmd, m_pipeline, m_camera, aspect, m_lights);

    // No debug overlay — player renders directly to swapchain final output
}

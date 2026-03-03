#include "Application.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <imgui/imgui.h>
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <iostream>

#include "Core/MaterialLayout.h"
#include "Core/MaterialManager.h"
#include "Core/MeshManager.h"
#include "ECS/Components/VolumetricFogComponent.h"
#include "ECS/Components/AnimationComponent.h"
#include "Renderer/VulkanRenderer.h"
#include "Renderer/Vulkan/VulkanInstance.h"
#include "Renderer/Vulkan/VulkanSwapchain.h"

// ─────────────────────────────────────────────────────────────────────────────
//  Public entry point
// ─────────────────────────────────────────────────────────────────────────────

int Application::Run()
{
    try { Init(); }
    catch (const std::exception& e)
    {
        std::cerr << "[ERROR] Initialization failed: " << e.what() << "\n";
        return -1;
    }

    uint64_t prevTime = SDL_GetTicks();

    while (m_running)
    {
        const uint64_t currTime = SDL_GetTicks();
        const float    dt       = static_cast<float>(currTime - prevTime) / 1000.0f;
        prevTime = currTime;

        PollEvents();
        HandleSceneViewResize();
        ProcessInput(dt);

        m_editor.BeginFrame();
        if (m_lookActive)
        {
            // While in FPS mode imgui should not steal keyboard or mouse.
            ImGui::GetIO().WantCaptureKeyboard = false;
            ImGui::GetIO().WantCaptureMouse    = false;
        }

        VulkanRenderer::Get().BeginFrame();
        if (!VulkanRenderer::Get().IsFrameStarted())
        {
            // Swapchain was rebuilt — skip this frame and re-sync fb sizes.
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

        UpdateLightBuffer();

        auto cmd = VulkanRenderer::Get().GetCommandBuffer();

        SyncECSToScene();
        UpdateAnimations(dt);
        RenderShadowPasses(cmd);
        RenderSceneView(cmd);
        RenderPostProcess(cmd);
        RenderMainPass(cmd);

        m_editor.Draw(m_camera, m_registry, dt);
        m_editor.Render(cmd);
        VulkanRenderer::Get().EndFrame();
    }

    Shutdown();
    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Initialization
// ─────────────────────────────────────────────────────────────────────────────

void Application::Init()
{
    SDL_Init(SDL_INIT_VIDEO);

    m_window = SDL_CreateWindow("EderGraphics", 800, 600,
        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    if (!m_window)
        throw std::runtime_error("SDL_CreateWindow failed");

    // Camera — FPS mode, slightly elevated start position
    m_camera.fpsMode = true;
    m_camera.fpsPos  = { 0.0f, 2.0f, 12.0f };
    m_camera.SetOrientation(0.0f, 0.0f);
    SDL_SetWindowRelativeMouseMode(m_window, false);

    // Core renderer singletons
    VulkanInstance::Get().Init(m_window);
    VulkanSwapchain::Get().Init(m_window);
    VulkanRenderer::Get().Init();
    VulkanRenderer::Get().SetWindow(m_window);
    m_editor.Init(m_window);

    // Main PBR pipeline
    m_pipeline.Create(
        "shaders/triangle.vert.spv",
        "shaders/triangle.frag.spv",
        VulkanSwapchain::Get().GetFormat(),
        VulkanRenderer::Get().GetDepthFormat());

    InitMaterials();

    // Shadow maps — must exist before lights.Build() so samplers are available
    m_shadowMap.Create(1024);
    m_shadowPipeline.Create(m_shadowMap.GetFormat());
    m_spotShadowMap.Create(1024);
    m_pointShadowMap.Create(512);
    m_pointShadowPipeline.Create(m_pointShadowMap.GetFormat());

    m_lights.Build(m_pipeline);
    m_lights.BindShadowMap     (m_shadowMap.GetArrayView(),        m_shadowMap.GetSampler());
    m_lights.BindSpotShadowMap (m_spotShadowMap.GetArrayView(),    m_spotShadowMap.GetSampler());
    m_lights.BindPointShadowMap(m_pointShadowMap.GetCubeArrayView(), m_pointShadowMap.GetSampler());

    InitPostProcess();

    m_boneSSBO.Create(m_pipeline);
    m_editor.SetSceneViewFramebuffer(&m_debugFb);
}

void Application::InitMaterials()
{
    MaterialLayout layout;
    layout.AddVec4 ("albedo")
          .AddFloat("roughness")
          .AddFloat("metallic")
          .AddFloat("emissiveIntensity")
          .AddFloat("alphaThreshold");   // 0 = opaque/blend, >0 = cutout

    MaterialManager::Get().Add("default", layout, m_pipeline);
    m_floorMat.Build(layout, m_pipeline);
    m_glassMat.Build(layout, m_pipeline);
    m_glassMat2.Build(layout, m_pipeline);
    m_glassMat3.Build(layout, m_pipeline);

    // Default — warm off-white
    auto& def = MaterialManager::Get().GetDefault();
    def.SetVec4 ("albedo",            glm::vec4(1.0f, 0.92f, 0.78f, 1.0f));
    def.SetFloat("roughness",         0.55f);
    def.SetFloat("metallic",          0.0f);
    def.SetFloat("emissiveIntensity", 0.0f);

    // Floor — cool grey
    m_floorMat.SetVec4 ("albedo",            glm::vec4(0.55f, 0.58f, 0.62f, 1.0f));
    m_floorMat.SetFloat("roughness",         0.85f);
    m_floorMat.SetFloat("metallic",          0.0f);
    m_floorMat.SetFloat("emissiveIntensity", 0.0f);

    // Glass — translucent blue
    m_glassMat.SetVec4 ("albedo",            glm::vec4(0.40f, 0.70f, 1.0f,  0.35f));
    m_glassMat.SetFloat("roughness",         0.05f);
    m_glassMat.SetFloat("metallic",          0.0f);
    m_glassMat.SetFloat("emissiveIntensity", 0.0f);
    m_glassMat.opacity = 0.35f;

    // Glass 2 — translucent green
    m_glassMat2.SetVec4 ("albedo",            glm::vec4(0.30f, 1.0f,  0.45f, 0.40f));
    m_glassMat2.SetFloat("roughness",         0.05f);
    m_glassMat2.SetFloat("metallic",          0.0f);
    m_glassMat2.SetFloat("emissiveIntensity", 0.0f);
    m_glassMat2.opacity = 0.40f;

    // Glass 3 — translucent amber
    m_glassMat3.SetVec4 ("albedo",            glm::vec4(1.0f,  0.65f, 0.10f, 0.45f));
    m_glassMat3.SetFloat("roughness",         0.05f);
    m_glassMat3.SetFloat("metallic",          0.0f);
    m_glassMat3.SetFloat("emissiveIntensity", 0.0f);
    m_glassMat3.opacity = 0.45f;

    // Shared texture
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
    auto& sc     = VulkanSwapchain::Get();
    auto& rd     = VulkanRenderer::Get();
    uint32_t w   = sc.GetExtent().width  / 2;
    uint32_t h   = sc.GetExtent().height / 2;
    auto depthFmt = rd.GetDepthFormat();

    m_debugFb.Create(w, h, vk::Format::eB8G8R8A8Unorm, depthFmt);
    m_debugOverlay.Create(sc.GetFormat(), depthFmt);
    m_skybox.Create(sc.GetFormat(), depthFmt);
    m_gizmo.Create(m_debugFb.GetColorFormat(), depthFmt);
    m_occlusionPass.Create(w, h);
    m_sunShafts.Create(m_debugFb.GetColorFormat(), w, h);
    m_volumetricLight.Create(m_debugFb.GetColorFormat(), w, h,
                             *m_pipeline.GetLightDescriptorSetLayout());
    m_volumetricFog.Create(m_debugFb.GetColorFormat(), w, h,
                           *m_pipeline.GetLightDescriptorSetLayout());
}

// ─────────────────────────────────────────────────────────────────────────────
//  Shutdown
// ─────────────────────────────────────────────────────────────────────────────

void Application::Shutdown()
{
    VulkanInstance::Get().GetDevice().waitIdle();

    m_editor.Shutdown();
    m_gizmo.Destroy();
    m_boneSSBO.Destroy();
    m_occlusionPass.Destroy();
    m_volumetricLight.Destroy();
    m_volumetricFog.Destroy();
    m_sunShafts.Destroy();
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

    SDL_DestroyWindow(m_window);
    SDL_Quit();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Per-frame: input & events
// ─────────────────────────────────────────────────────────────────────────────

void Application::PollEvents()
{
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        m_editor.ProcessEvent(event);

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

void Application::ProcessInput(float dt)
{
    // RMB state is polled rather than event-driven to avoid losing keyboard
    // events when toggling relative mouse mode inside the event loop.
    float mx, my;
    bool  rmb = (SDL_GetMouseState(&mx, &my) & SDL_BUTTON_RMASK) != 0;

    if (rmb && !m_lookActive)
    {
        m_lookActive = true;
        SDL_SetWindowRelativeMouseMode(m_window, true);
        SDL_RaiseWindow(m_window);
        SDL_GetRelativeMouseState(&mx, &my);   // flush accumulated delta
        m_mouseDX = m_mouseDY = 0.0f;
    }
    else if (!rmb && m_lookActive)
    {
        m_lookActive = false;
        SDL_SetWindowRelativeMouseMode(m_window, false);
        SDL_GetRelativeMouseState(&mx, &my);   // flush
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
    if (keys[SDL_SCANCODE_LCTRL])  m_camera.fpsPos.y -= speed * dt;
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
}

// ─────────────────────────────────────────────────────────────────────────────
//  Per-frame: ECS synchronization
// ─────────────────────────────────────────────────────────────────────────────

void Application::UpdateLightBuffer()
{
    auto& sc     = VulkanSwapchain::Get();
    float aspect = static_cast<float>(sc.GetExtent().width) /
                   static_cast<float>(sc.GetExtent().height);

    // Reset per-frame state
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
        const auto& tr = m_registry.Get<TransformComponent>(e);

        if (l.type == LightType::Directional)
        {
            glm::mat4 mat = tr.GetMatrix();
            glm::vec3 dir = glm::normalize(glm::vec3(mat * glm::vec4(0, -1, 0, 0)));

            if (!m_hasDir)
            {
                m_activeDirDir       = dir;
                m_activeDirColor     = l.color;
                m_activeDirIntensity = l.intensity;
                m_hasDir             = true;
            }

            // Fade out as the sun crosses below the horizon
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
            pl.position  = tr.position;
            pl.color     = l.color;
            pl.intensity = l.intensity;
            pl.radius    = l.range;

            if (l.castShadow && pointSlot < 1)
            {
                pl.shadowIdx       = 0;
                m_hasPointShadow   = true;
                m_activePointPos   = tr.position;
                m_activePointFar   = l.range;
                m_lights.SetPointFarPlane(0, l.range);
                ++pointSlot;
            }
            else { pl.shadowIdx = -1; }

            m_lights.AddPoint(pl);
        }
        else if (l.type == LightType::Spot)
        {
            glm::mat4 mat = tr.GetMatrix();
            glm::vec3 dir = glm::normalize(glm::vec3(mat * glm::vec4(0, -1, 0, 0)));

            SpotLight sl{};
            sl.position  = tr.position;
            sl.direction = dir;
            sl.innerCos  = std::cos(glm::radians(l.innerConeAngle));
            sl.outerCos  = std::cos(glm::radians(l.outerConeAngle));
            sl.color     = l.color;
            sl.intensity = l.intensity;
            sl.radius    = l.range;

            if (l.castShadow && spotSlot < 1)
            {
                sl.shadowIdx          = 0;
                m_hasSpotShadow       = true;
                m_activeSpotPos       = tr.position;
                m_activeSpotDir       = dir;
                m_activeSpotOuterCos  = sl.outerCos;
                m_activeSpotFar       = l.range;
                m_activeSpotMatrix    = VulkanSpotShadowMap::ComputeMatrix(
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

    m_lights.SetCascadeData(m_cascadeMatrices, m_cascadeSplits);
    m_lights.SetCameraForward(m_camera.GetForward());
    m_lights.Update(m_camera.GetPosition());
    m_lights.SetSkyAmbient(glm::vec3(0.04f), glm::vec3(0.04f));
}

void Application::SyncECSToScene()
{
    // 1. Remove SceneObjects whose entity no longer has a MeshRendererComponent
    auto& objs = m_scene.GetObjects();
    objs.erase(std::remove_if(objs.begin(), objs.end(),
        [&](const SceneObject& o) {
            return o.entityId != 0 && !m_registry.Has<MeshRendererComponent>(o.entityId);
        }), objs.end());

    // 2. Add SceneObjects for entities that gained a MeshRendererComponent
    m_registry.Each<MeshRendererComponent>([&](Entity e, MeshRendererComponent& mr)
    {
        bool exists = std::any_of(objs.begin(), objs.end(),
            [e](const SceneObject& o) { return o.entityId == e; });
        if (exists) return;

        Material&    mat = MaterialManager::Get().Get(mr.materialName);
        VulkanMesh&  m   = MeshManager::Get().Load(mr.meshPath);
        SceneObject& obj = m_scene.Add(m, mat);
        obj.entityId     = e;

        if (m_registry.Has<TransformComponent>(e))
        {
            const auto& t    = m_registry.Get<TransformComponent>(e);
            obj.transform.position = t.position;
            obj.transform.rotation = t.rotation;
            obj.transform.scale    = t.scale;
        }
    });

    // 3. Mirror TransformComponent → SceneObject every frame
    for (auto& obj : objs)
    {
        if (obj.entityId == 0 || !m_registry.Has<TransformComponent>(obj.entityId)) continue;
        const auto& t    = m_registry.Get<TransformComponent>(obj.entityId);
        obj.transform.position = t.position;
        obj.transform.rotation = t.rotation;
        obj.transform.scale    = t.scale;
    }

    // 4. Sync alphaMode → alphaThreshold UBO field
    for (auto& obj : objs)
    {
        if (!obj.material) continue;
        float threshold = (obj.material->alphaMode == Material::AlphaMode::AlphaTest)
                          ? obj.material->alphaCutoff : 0.0f;
        obj.material->SetFloat("alphaThreshold", threshold);
    }
}

void Application::UpdateAnimations(float dt)
{
    std::vector<glm::mat4> boneMatrices(MAX_BONES, glm::mat4(1.0f));

    m_registry.Each<AnimationComponent>([&](Entity e, AnimationComponent& anim)
    {
        if (!m_registry.Has<MeshRendererComponent>(e)) return;
        const auto& mr = m_registry.Get<MeshRendererComponent>(e);
        if (!MeshManager::Get().Has(mr.meshPath)) return;

        VulkanMesh& mesh = MeshManager::Get().Load(mr.meshPath);
        if (mesh.GetBoneCount() == 0 || mesh.GetAnimationCount() == 0) return;

        int clipIdx = glm::clamp(anim.animIndex, 0,
            static_cast<int>(mesh.GetAnimationCount()) - 1);

        // Detect clip change → trigger crossfade
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

        const float step          = dt * anim.speed;
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
    });

    m_boneSSBO.Upload(boneMatrices);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Render passes
// ─────────────────────────────────────────────────────────────────────────────

void Application::RenderShadowPasses(vk::CommandBuffer cmd)
{
    // Cascaded directional shadow (4 splits)
    for (uint32_t c = 0; c < VulkanShadowMap::NUM_CASCADES; ++c)
    {
        m_shadowMap.BeginRendering(cmd, c);
        m_shadowPipeline.Bind(cmd);
        m_boneSSBO.BindToSet(cmd, *m_shadowPipeline.GetLayout(), 0);
        m_scene.DrawShadow(cmd, m_shadowPipeline, m_cascadeMatrices[c]);
        m_shadowMap.EndRendering(cmd);
    }
    m_shadowMap.TransitionToShaderRead(cmd);

    // Spot shadow (slot 0)
    if (m_hasSpotShadow)
    {
        m_spotShadowMap.BeginRendering(cmd, 0);
        m_shadowPipeline.Bind(cmd);
        m_boneSSBO.BindToSet(cmd, *m_shadowPipeline.GetLayout(), 0);
        m_scene.DrawShadow(cmd, m_shadowPipeline, m_activeSpotMatrix);
        m_spotShadowMap.EndRendering(cmd);
    }
    m_spotShadowMap.TransitionToShaderRead(cmd);

    // Point shadow — 6 cubemap faces (slot 0)
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

void Application::RenderSceneView(vk::CommandBuffer cmd)
{
    const float aspect = SceneViewAspect();

    m_debugFb.BeginRendering(cmd);

    // Opaque geometry
    m_pipeline.Bind(cmd);
    m_boneSSBO.Bind(cmd, *m_pipeline.GetLayout());
    m_scene.Draw(cmd, m_pipeline, m_camera, aspect, m_lights);

    // Skybox — after opaques so depth test skips covered pixels
    m_skybox.Draw(cmd, m_camera.GetView(), m_camera.GetProjection(aspect), -m_activeDirDir);

    // Transparents — after skybox so skybox doesn't overwrite them
    m_pipeline.BindTransparent(cmd);
    m_scene.DrawTransparent(cmd, m_pipeline, m_camera, aspect, m_lights);

    // Light gizmos on top
    glm::mat4 vp = m_camera.GetProjection(aspect) * m_camera.GetView();
    m_gizmo.Draw(cmd, m_registry, vp, m_editor.GetSelected());

    m_debugFb.EndRendering(cmd);
    m_debugFb.TransitionToShaderRead(cmd);
}

void Application::RenderPostProcess(vk::CommandBuffer cmd)
{
    const float aspect = SceneViewAspect();
    m_postFb = &m_debugFb;

    // ── Volumetric Lighting ───────────────────────────────────────────────────
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

            // Fade directional scatter below the horizon
            float sunAbove = m_hasDir
                ? glm::clamp(sunWorldDir.y * 5.0f + 1.0f, 0.0f, 1.0f)
                : 0.0f;

            glm::mat4 view  = m_camera.GetView();
            glm::mat4 proj  = m_camera.GetProjection(aspect);
            glm::mat4 invVP = glm::inverse(proj * view);

            m_volumetricLight.Draw(cmd,
                m_debugFb.GetColorView(),  m_debugFb.GetSampler(),
                m_debugFb.GetDepthView(),  m_debugFb.GetSampler(),
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

    // ── Volumetric Fog ────────────────────────────────────────────────────────
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

    // ── Sun Shafts ────────────────────────────────────────────────────────────
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
                : glm::vec2(-10.0f);   // off-screen → god-ray intensity = 0

            float sunHeight = glm::normalize(-m_activeDirDir).y;

            m_occlusionPass.Draw(cmd,
                m_debugFb.GetDepthView(), m_debugFb.GetSampler(),
                sunUV, shaftsComp->shaftsSunRadius);

            m_sunShafts.Draw(cmd,
                m_postFb->GetColorView(),  m_postFb->GetSampler(),
                m_occlusionPass.GetView(), m_debugFb.GetDepthView(),
                sunUV,
                shaftsComp->shaftsDensity,    shaftsComp->shaftsBloomScale,
                shaftsComp->shaftsDecay,      shaftsComp->shaftsWeight,
                shaftsComp->shaftsExposure,   shaftsComp->shaftsTint,
                sunHeight);

            m_sunShafts.GetOutput().TransitionToShaderRead(cmd);
            m_editor.SetSceneViewFramebuffer(&m_sunShafts.GetOutput());
        }
        else
        {
            m_editor.SetSceneViewFramebuffer(m_postFb);
        }
    }
}

void Application::RenderMainPass(vk::CommandBuffer cmd)
{
    auto& sc     = VulkanSwapchain::Get();
    float aspect = static_cast<float>(sc.GetExtent().width) /
                   static_cast<float>(sc.GetExtent().height);

    VulkanRenderer::Get().BeginMainPass();

    // Opaque geometry
    m_pipeline.Bind(cmd);
    m_boneSSBO.Bind(cmd, *m_pipeline.GetLayout());
    m_scene.Draw(cmd, m_pipeline, m_camera, aspect, m_lights);

    // Skybox
    m_skybox.Draw(cmd, m_camera.GetView(), m_camera.GetProjection(aspect), -m_activeDirDir);

    // Transparents
    m_pipeline.BindTransparent(cmd);
    m_boneSSBO.Bind(cmd, *m_pipeline.GetLayout());
    m_scene.DrawTransparent(cmd, m_pipeline, m_camera, aspect, m_lights);

    m_debugOverlay.Draw(cmd, m_debugFb, m_shadowMap);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Utility
// ─────────────────────────────────────────────────────────────────────────────

float Application::SceneViewAspect() const
{
    return static_cast<float>(m_debugFb.GetExtent().width) /
           static_cast<float>(m_debugFb.GetExtent().height);
}

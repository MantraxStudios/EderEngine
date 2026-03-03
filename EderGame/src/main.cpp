#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <algorithm>
#include <iostream>
#include <imgui/imgui.h>
#include "Editor/Editor.h"
#include "Renderer/Vulkan/VulkanInstance.h"
#include "Renderer/Vulkan/VulkanSwapchain.h"
#include "Renderer/Vulkan/VulkanPipeline.h"
#include "Renderer/Vulkan/VulkanMesh.h"
#include "Renderer/Vulkan/VulkanTexture.h"
#include "Renderer/Vulkan/VulkanDepthBuffer.h"
#include "Renderer/Vulkan/VulkanFramebuffer.h"
#include "Renderer/Vulkan/VulkanDebugOverlay.h"
#include "Renderer/Vulkan/VulkanSkybox.h"
#include "Renderer/Vulkan/VulkanShadowMap.h"
#include "Renderer/Vulkan/VulkanShadowPipeline.h"
#include "Renderer/Vulkan/VulkanSpotShadowMap.h"
#include "Renderer/Vulkan/VulkanPointShadowMap.h"
#include "Renderer/Vulkan/VulkanPointShadowPipeline.h"
#include "Renderer/VulkanRenderer.h"
#include "Core/MaterialManager.h"
#include "Core/MeshManager.h"
#include "Core/Material.h"
#include "Core/MaterialLayout.h"
#include "Core/Camera.h"
#include "Core/Scene.h"
#include "Core/LightBuffer.h"
#include "EderCore.h"
#include "VulkanGizmo.h"
#include "VulkanSunShafts.h"
#include "VulkanOcclusionPass.h"
#include "VulkanVolumetricLight.h"
#include "VulkanVolumetricFog.h"
#include "Renderer/Vulkan/BoneSSBO.h"
#include "ECS/Components/VolumetricLightComponent.h"
#include "ECS/Components/VolumetricFogComponent.h"
#include "ECS/Components/AnimationComponent.h"
#include <glm/gtc/constants.hpp>

int main()
{
    SDL_Init(SDL_INIT_VIDEO);

    SDL_Window* window = SDL_CreateWindow("EderGraphics", 800, 600,
        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    if (!window) return -1;

    Camera camera({ 0.0f, 0.0f, 0.0f }, 35.0f, 50.0f);
    camera.fpsMode = true;   
    camera.fpsPos  = { 0.0f, 2.0f, 12.0f };
    camera.SetOrientation(0.0f, 0.0f);
    SDL_SetWindowRelativeMouseMode(window, false);

    Editor editor;

    VulkanPipeline       pipeline;
    VulkanTexture        albedoTex;
    Material             floorMat;
    Material             glassMat;
    Material             glassMat2;  // green
    Material             glassMat3;  // amber
    VulkanFramebuffer    debugFb;
    VulkanDebugOverlay   debugOverlay;
    VulkanSkybox         skybox;
    VulkanGizmo          gizmo;
    VulkanSunShafts         sunShafts;
    VulkanOcclusionPass     occlusionPass;
    VulkanVolumetricLight   volumetricLight;
    VulkanVolumetricFog     volumetricFog;
    BoneSSBO                boneSSBO;
    VulkanShadowMap          shadowMap;
    VulkanShadowPipeline     shadowPipeline;
    VulkanSpotShadowMap       spotShadowMap;
    VulkanPointShadowMap      pointShadowMap;
    VulkanPointShadowPipeline pointShadowPipeline;
    Scene                scene;
    LightBuffer          lights;
    Registry             registry;

    try
    {
        VulkanInstance::Get().Init(window);
        VulkanSwapchain::Get().Init(window);
        VulkanRenderer::Get().Init();
        VulkanRenderer::Get().SetWindow(window);
        editor.Init(window);

        pipeline.Create(
            "shaders/triangle.vert.spv",
            "shaders/triangle.frag.spv",
            VulkanSwapchain::Get().GetFormat(),
            VulkanRenderer::Get().GetDepthFormat());

        {
            MaterialLayout layout;
            layout.AddVec4 ("albedo")
                  .AddFloat("roughness")
                  .AddFloat("metallic")
                  .AddFloat("emissiveIntensity")
                  .AddFloat("alphaThreshold");  // 0=opaque/blend, >0=cutout mode
            MaterialManager::Get().Add("default", layout, pipeline);
            floorMat.Build(layout, pipeline);
            glassMat.Build(layout, pipeline);
            glassMat2.Build(layout, pipeline);
            glassMat3.Build(layout, pipeline);
        }

        // Default material — warm off-white
        MaterialManager::Get().GetDefault().SetVec4 ("albedo",            glm::vec4(1.0f, 0.92f, 0.78f, 1.0f));
        MaterialManager::Get().GetDefault().SetFloat("roughness",         0.55f);
        MaterialManager::Get().GetDefault().SetFloat("metallic",          0.0f);
        MaterialManager::Get().GetDefault().SetFloat("emissiveIntensity", 0.0f);

        // Floor material — cool grey
        floorMat.SetVec4 ("albedo",            glm::vec4(0.55f, 0.58f, 0.62f, 1.0f));
        floorMat.SetFloat("roughness",         0.85f);
        floorMat.SetFloat("metallic",          0.0f);
        floorMat.SetFloat("emissiveIntensity", 0.0f);

        // Glass material — translucent blue
        glassMat.SetVec4 ("albedo",            glm::vec4(0.40f, 0.70f, 1.0f,  0.35f));
        glassMat.SetFloat("roughness",         0.05f);
        glassMat.SetFloat("metallic",          0.0f);
        glassMat.SetFloat("emissiveIntensity", 0.0f);
        glassMat.opacity = 0.35f;

        // Glass material 2 — translucent green
        glassMat2.SetVec4 ("albedo",            glm::vec4(0.30f, 1.0f,  0.45f, 0.40f));
        glassMat2.SetFloat("roughness",         0.05f);
        glassMat2.SetFloat("metallic",          0.0f);
        glassMat2.SetFloat("emissiveIntensity", 0.0f);
        glassMat2.opacity = 0.40f;

        // Glass material 3 — translucent amber
        glassMat3.SetVec4 ("albedo",            glm::vec4(1.0f,  0.65f, 0.10f, 0.45f));
        glassMat3.SetFloat("roughness",         0.05f);
        glassMat3.SetFloat("metallic",          0.0f);
        glassMat3.SetFloat("emissiveIntensity", 0.0f);
        glassMat3.opacity = 0.45f;

        try { albedoTex.Load("assets/bush01.png"); }
        catch (const std::exception& e)
        {
            std::cerr << "[WARNING] " << e.what() << std::endl;
            albedoTex.CreateDefault();
        }
        MaterialManager::Get().GetDefault().BindTexture(0, albedoTex);
        floorMat.BindTexture(0, albedoTex);
        glassMat.BindTexture(0, albedoTex);
        glassMat2.BindTexture(0, albedoTex);
        glassMat3.BindTexture(0, albedoTex);

        // Scene starts empty — add entities at runtime via the editor

        // Shadow map must be created before lights.Build so the sampler is available
        shadowMap.Create(1024);
        shadowPipeline.Create(shadowMap.GetFormat());

        spotShadowMap.Create(1024);
        pointShadowMap.Create(512);
        pointShadowPipeline.Create(pointShadowMap.GetFormat());

        lights.Build(pipeline);

        // Lights are managed entirely via ECS — no hardcoded lights at startup.
        // LightBuffer is rebuilt each frame from LightComponent entities.

        // Bind cascade shadow map (array) into the light descriptor set
        lights.BindShadowMap(shadowMap.GetArrayView(), shadowMap.GetSampler());
        lights.BindSpotShadowMap(spotShadowMap.GetArrayView(), spotShadowMap.GetSampler());
        lights.BindPointShadowMap(pointShadowMap.GetCubeArrayView(), pointShadowMap.GetSampler());

        auto& sc = VulkanSwapchain::Get();
        debugFb.Create(sc.GetExtent().width / 2, sc.GetExtent().height / 2,
                       vk::Format::eB8G8R8A8Unorm, VulkanRenderer::Get().GetDepthFormat());
        debugOverlay.Create(sc.GetFormat(), VulkanRenderer::Get().GetDepthFormat());
        skybox.Create(sc.GetFormat(), VulkanRenderer::Get().GetDepthFormat());
        gizmo.Create(debugFb.GetColorFormat(), VulkanRenderer::Get().GetDepthFormat());
        sunShafts.Create(debugFb.GetColorFormat(), sc.GetExtent().width / 2, sc.GetExtent().height / 2);
        occlusionPass.Create(sc.GetExtent().width / 2, sc.GetExtent().height / 2);
        volumetricLight.Create(debugFb.GetColorFormat(), sc.GetExtent().width / 2, sc.GetExtent().height / 2,
                                *pipeline.GetLightDescriptorSetLayout());
        volumetricFog.Create(debugFb.GetColorFormat(), sc.GetExtent().width / 2, sc.GetExtent().height / 2,
                              *pipeline.GetLightDescriptorSetLayout());
        boneSSBO.Create(pipeline);
        editor.SetSceneViewFramebuffer(&debugFb);
    }
    catch (const std::exception& e)
    {
        std::cerr << "[ERROR] " << e.what() << std::endl;
        return -1;
    }

    glm::mat4 cascadeMatrices[VulkanShadowMap::NUM_CASCADES];
    glm::vec4 cascadeSplits;

    uint64_t prevTime  = SDL_GetTicks();
    float    mouseDX   = 0.0f, mouseDY = 0.0f;
    bool     running   = true;
    bool     lookActive = false;  // true solo mientras se mantiene click derecho

    while (running)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            editor.ProcessEvent(event);

            if (event.type == SDL_EVENT_QUIT)
                running = false;
            else if (event.type == SDL_EVENT_WINDOW_RESIZED ||
                     event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
                VulkanRenderer::Get().SetFramebufferResized();
            else if (event.type == SDL_EVENT_KEY_DOWN &&
                     event.key.scancode == SDL_SCANCODE_ESCAPE)
                running = false;
        }

        // ── Right-click FPS mode — polled each frame, not event-based ────────
        // Calling SDL_SetWindowRelativeMouseMode inside the event loop can
        // discard queued keyboard events. Poll the button state here instead.
        {
            float mx, my;
            bool rmb = (SDL_GetMouseState(&mx, &my) & SDL_BUTTON_RMASK) != 0;

            if (rmb && !lookActive)
            {
                lookActive = true;
                SDL_SetWindowRelativeMouseMode(window, true);
                SDL_RaiseWindow(window);
                SDL_GetRelativeMouseState(&mouseDX, &mouseDY); // flush
                mouseDX = 0.0f;
                mouseDY = 0.0f;
            }
            else if (!rmb && lookActive)
            {
                lookActive = false;
                SDL_SetWindowRelativeMouseMode(window, false);
                SDL_GetRelativeMouseState(&mouseDX, &mouseDY); // flush
                mouseDX = 0.0f;
                mouseDY = 0.0f;
            }
            else if (lookActive)
            {
                SDL_GetRelativeMouseState(&mouseDX, &mouseDY);
            }
            else
            {
                mouseDX = 0.0f;
                mouseDY = 0.0f;
            }
        }
        // ─────────────────────────────────────────────────────────────────────

        uint64_t currTime = SDL_GetTicks();
        float    dt       = static_cast<float>(currTime - prevTime) / 1000.0f;
        prevTime          = currTime;

        // --- SceneView resize ---
        {
            uint32_t svW = 0, svH = 0;
            editor.GetSceneViewSize(svW, svH);
            if (svW > 4 && svH > 4 &&
                (svW != debugFb.GetExtent().width || svH != debugFb.GetExtent().height))
            {
                VulkanInstance::Get().GetDevice().waitIdle();
                editor.ReleaseSceneViewFramebuffer();  // release stale imageview before resize
                debugFb.Recreate(svW, svH);
                sunShafts.Resize(svW, svH);
                occlusionPass.Resize(svW, svH);
                volumetricLight.Resize(svW, svH);
                volumetricFog.Resize(svW, svH);
                // The render loop (below) always calls SetSceneViewFramebuffer with the
                // correct active framebuffer — no need to set it here and cause a double-set.
            }
        }
        // -------------------------

        editor.BeginFrame();

        // Mientras lookActive, ImGui no captura teclado ni mouse
        if (lookActive)
        {
            ImGui::GetIO().WantCaptureKeyboard = false;
            ImGui::GetIO().WantCaptureMouse    = false;
        }

        // FPS look — solo con click derecho sostenido
        if (lookActive)
            camera.FPSLook(mouseDX, mouseDY);

        // Movimiento WASD — solo con click derecho sostenido
        if (lookActive)
        {
            const bool* keys = SDL_GetKeyboardState(nullptr);
            const float spd  = 8.0f;
            glm::vec3 fwd    = camera.GetForward();
            glm::vec3 right  = camera.GetRight();
            glm::vec3 fwdXZ  = glm::normalize(glm::vec3(fwd.x, 0.0f, fwd.z));
            if (keys[SDL_SCANCODE_W])     camera.fpsPos += fwdXZ * spd * dt;
            if (keys[SDL_SCANCODE_S])     camera.fpsPos -= fwdXZ * spd * dt;
            if (keys[SDL_SCANCODE_A])     camera.fpsPos -= right  * spd * dt;
            if (keys[SDL_SCANCODE_D])     camera.fpsPos += right  * spd * dt;
            if (keys[SDL_SCANCODE_SPACE]) camera.fpsPos.y += spd * dt;
            if (keys[SDL_SCANCODE_LCTRL])camera.fpsPos.y -= spd * dt;
        }

        double currTimeSec = static_cast<double>(currTime) / 1000.0;

        auto&  sc     = VulkanSwapchain::Get();
        float  aspect = static_cast<float>(sc.GetExtent().width) /
                        static_cast<float>(sc.GetExtent().height);

        glm::vec3 camForward = camera.GetForward();

        // ── ECS → LightBuffer sync ───────────────────────────────────────────
        // Collect shadow-caster data first (needed for shadow passes below)
        glm::vec3 activeDirDir       = glm::normalize(glm::vec3(-1,-1,-0.4f)); // fallback
        glm::vec3 activeDirColor     = glm::vec3(1.0f, 0.9f, 0.7f);
        float     activeDirIntensity = 1.0f;
        bool      hasDir             = false;
        bool      hasSpotShadow      = false;
        glm::vec3 activeSpotPos      = glm::vec3(0);
        glm::vec3 activeSpotDir      = glm::vec3(0,-1,0);
        float     activeSpotOuterCos = std::cos(glm::radians(30.0f));
        float     activeSpotNear     = 0.3f;
        float     activeSpotFar      = 50.0f;
        glm::mat4 activeSpotMatrix   = glm::mat4(1);
        bool      hasPointShadow     = false;
        glm::vec3 activePointPos     = glm::vec3(0);
        float     activePointFar     = 50.0f;

        lights.ClearLights();
        {
            int spotSlot = 0, pointSlot = 0;
            registry.Each<LightComponent>([&](Entity e, LightComponent& l)
            {
                if (!registry.Has<TransformComponent>(e)) return;
                const auto& tr = registry.Get<TransformComponent>(e);

                if (l.type == LightType::Directional)
                {
                    glm::mat4 m  = tr.GetMatrix();
                    glm::vec3 dir = glm::normalize(glm::vec3(m * glm::vec4(0,-1,0,0)));
                    if (!hasDir) { activeDirDir = dir; activeDirColor = l.color; activeDirIntensity = l.intensity; hasDir = true; }
                    // Fade directional light to 0 as sun goes below horizon
                    // (dir.y > 0 means light points upward = sun is below; -dir.y is sun altitude)
                    float sunHorizon = glm::clamp(-dir.y * 5.0f + 1.0f, 0.0f, 1.0f);
                    DirectionalLight dl{};
                    dl.direction = dir;
                    dl.color     = l.color;
                    dl.intensity = l.intensity * sunHorizon;
                    lights.AddDirectional(dl);
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
                        pl.shadowIdx    = 0;
                        hasPointShadow  = true;
                        activePointPos  = tr.position;
                        activePointFar  = l.range;
                        lights.SetPointFarPlane(0, l.range);
                        pointSlot++;
                    }
                    else { pl.shadowIdx = -1; }
                    lights.AddPoint(pl);
                }
                else if (l.type == LightType::Spot)
                {
                    glm::mat4 m   = tr.GetMatrix();
                    glm::vec3 dir = glm::normalize(glm::vec3(m * glm::vec4(0,-1,0,0)));
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
                        sl.shadowIdx      = 0;
                        hasSpotShadow     = true;
                        activeSpotPos     = tr.position;
                        activeSpotDir     = dir;
                        activeSpotOuterCos = sl.outerCos;
                        activeSpotFar     = l.range;
                        activeSpotMatrix  = VulkanSpotShadowMap::ComputeMatrix(
                            activeSpotPos, activeSpotDir, activeSpotOuterCos, 0.3f, activeSpotFar);
                        lights.SetSpotMatrix(0, activeSpotMatrix);
                        spotSlot++;
                    }
                    else { sl.shadowIdx = -1; }
                    lights.AddSpot(sl);
                }
            });
        }

        shadowMap.ComputeCascades(
            camera.GetView(), camera.GetProjection(aspect),
            activeDirDir, camera.nearPlane, camera.farPlane,
            cascadeMatrices, cascadeSplits);
        lights.SetCascadeData(cascadeMatrices, cascadeSplits);
        lights.SetCameraForward(camForward);
        lights.Update(camera.GetPosition());

        // Neutral constant ambient — skybox colours do NOT affect world objects.
        // Ambient is just a small fill light to prevent pure-black shadows.
        lights.SetSkyAmbient(glm::vec3(0.04f), glm::vec3(0.04f));

        VulkanRenderer::Get().BeginFrame();

        if (!VulkanRenderer::Get().IsFrameStarted())
        {
            editor.EndFrame();
            uint32_t fw = sc.GetExtent().width  / 2;
            uint32_t fh = sc.GetExtent().height / 2;
            if (fw != debugFb.GetExtent().width || fh != debugFb.GetExtent().height)
            {
                debugFb.Recreate(fw, fh);
                sunShafts.Resize(fw, fh);
                occlusionPass.Resize(fw, fh);
                volumetricLight.Resize(fw, fh);
                volumetricFog.Resize(fw, fh);
            }
            continue;
        }

        auto cmd = VulkanRenderer::Get().GetCommandBuffer();

        // ── ECS → Scene sync ─────────────────────────────────────────────────
        // 1. Remove SceneObjects whose entity lost its MeshRenderer (or was destroyed)
        {
            auto& objs = scene.GetObjects();
            objs.erase(std::remove_if(objs.begin(), objs.end(),
                [&](const SceneObject& o) {
                    return o.entityId != 0 && !registry.Has<MeshRendererComponent>(o.entityId);
                }), objs.end());
        }
        // 2. Create SceneObjects for entities that have MeshRenderer but no SceneObject yet
        registry.Each<MeshRendererComponent>([&](Entity e, MeshRendererComponent& mr)
        {
            auto& objs = scene.GetObjects();
            bool exists = false;
            for (const auto& o : objs)
                if (o.entityId == e) { exists = true; break; }
            if (!exists)
            {
                Material& mat  = MaterialManager::Get().Get(mr.materialName);
                VulkanMesh& m  = MeshManager::Get().Load(mr.meshPath);
                SceneObject& obj = scene.Add(m, mat);
                obj.entityId = e;
                if (registry.Has<TransformComponent>(e))
                {
                    const auto& t = registry.Get<TransformComponent>(e);
                    obj.transform.position = t.position;
                    obj.transform.rotation = t.rotation;
                    obj.transform.scale    = t.scale;
                }
            }
        });
        // 3. Mirror TransformComponent → SceneObject::transform every frame
        for (auto& obj : scene.GetObjects())
        {
            if (obj.entityId != 0 && registry.Has<TransformComponent>(obj.entityId))
            {
                const auto& t = registry.Get<TransformComponent>(obj.entityId);
                obj.transform.position = t.position;
                obj.transform.rotation = t.rotation;
                obj.transform.scale    = t.scale;
            }
        }
        // 4. Sync Material::alphaMode → alphaThreshold UBO field every frame
        for (auto& obj : scene.GetObjects())
        {
            if (!obj.material) continue;
            float threshold = (obj.material->alphaMode == Material::AlphaMode::AlphaTest)
                              ? obj.material->alphaCutoff : 0.0f;
            obj.material->SetFloat("alphaThreshold", threshold);
        }
        // ─────────────────────────────────────────────────────────────────────

        // ── Animation update ──────────────────────────────────────────────────────
        // Must run BEFORE shadow passes so bone matrices are current
        // ─────────────────────────────────────────────────────────────────────────
        {
            std::vector<glm::mat4> boneMatrices(MAX_BONES, glm::mat4(1.0f));
            registry.Each<AnimationComponent>([&](Entity e, AnimationComponent& anim) {
                if (!registry.Has<MeshRendererComponent>(e)) return;
                const auto& mr = registry.Get<MeshRendererComponent>(e);
                if (!MeshManager::Get().Has(mr.meshPath)) return;
                VulkanMesh& m = MeshManager::Get().Load(mr.meshPath);
                if (m.GetBoneCount() == 0 || m.GetAnimationCount() == 0) return;

                int maxIdx  = static_cast<int>(m.GetAnimationCount()) - 1;
                int clipIdx = glm::clamp(anim.animIndex, 0, maxIdx);

                // ── Detect index change → start crossfade ─────────────────
                if (anim.activeIndex != clipIdx)
                {
                    anim.prevIndex  = (anim.activeIndex >= 0) ? anim.activeIndex : clipIdx;
                    anim.prevTime   = anim.currentTime;
                    anim.currentTime = 0.0f;
                    anim.activeIndex = clipIdx;
                    anim.blendTime   = 0.0f;
                    anim.playing     = true;
                }

                if (!anim.playing) return;

                // ── Advance times ─────────────────────────────────────────
                float step = dt * anim.speed;

                float durationActive = m.GetAnimationDuration(static_cast<uint32_t>(anim.activeIndex));
                anim.currentTime += step;
                if (!anim.loop && anim.currentTime >= durationActive)
                {
                    anim.currentTime = durationActive;
                    anim.playing     = false;
                }

                // Also advance the source clip during blend so it doesn't freeze
                if (anim.prevIndex >= 0 && anim.blendTime < anim.blendDuration)
                {
                    anim.prevTime  += step;
                    anim.blendTime += step;
                }

                // ── Sample pose(s) ────────────────────────────────────────
                std::vector<glm::mat4> boneMats;
                m.ComputeBoneTransforms(static_cast<uint32_t>(anim.activeIndex),
                                        anim.currentTime, boneMats);

                float blendFactor = (anim.prevIndex >= 0 && anim.blendDuration > 0.0f)
                    ? glm::clamp(anim.blendTime / anim.blendDuration, 0.0f, 1.0f)
                    : 1.0f;

                if (blendFactor < 1.0f)
                {
                    // Blend source pose with destination pose
                    std::vector<glm::mat4> prevMats;
                    m.ComputeBoneTransforms(static_cast<uint32_t>(anim.prevIndex),
                                            anim.prevTime, prevMats);

                    uint32_t boneCount = static_cast<uint32_t>(
                        std::min(boneMats.size(), prevMats.size()));
                    for (uint32_t i = 0; i < boneCount; i++)
                    {
                        // Per-bone matrix lerp: decompose to TRS for correctness
                        // Simple component lerp is good enough for smooth blends
                        const glm::mat4& src = prevMats[i];
                        const glm::mat4& dst = boneMats[i];
                        boneMats[i] = src + blendFactor * (dst - src);
                    }
                }
                else
                {
                    // Blend complete — clear source
                    anim.prevIndex = -1;
                    anim.prevTime  = 0.0f;
                }

                for (uint32_t i = 0; i < static_cast<uint32_t>(boneMats.size()) && i < MAX_BONES; i++)
                    boneMatrices[i] = boneMats[i];
            });
            boneSSBO.Upload(boneMatrices);
        }

        // --- Shadow pass (4 cascades) ---
        for (uint32_t c = 0; c < VulkanShadowMap::NUM_CASCADES; c++)
        {
            shadowMap.BeginRendering(cmd, c);
            shadowPipeline.Bind(cmd);
            boneSSBO.BindToSet(cmd, *shadowPipeline.GetLayout(), 0);
            scene.DrawShadow(cmd, shadowPipeline, cascadeMatrices[c]);
            shadowMap.EndRendering(cmd);
        }
        shadowMap.TransitionToShaderRead(cmd);

        // --- Spot shadow pass (slot 0) ---
        if (hasSpotShadow)
        {
            spotShadowMap.BeginRendering(cmd, 0);
            shadowPipeline.Bind(cmd);
            boneSSBO.BindToSet(cmd, *shadowPipeline.GetLayout(), 0);
            scene.DrawShadow(cmd, shadowPipeline, activeSpotMatrix);
            spotShadowMap.EndRendering(cmd);
        }
        spotShadowMap.TransitionToShaderRead(cmd);

        // --- Point shadow pass (6 faces, slot 0) ---
        if (hasPointShadow)
        {
            auto faceMats = VulkanPointShadowMap::ComputeFaceMatrices(activePointPos, 0.1f, activePointFar);
            for (uint32_t face = 0; face < 6; face++)
            {
                pointShadowMap.BeginRendering(cmd, 0, face);
                pointShadowPipeline.Bind(cmd);
                boneSSBO.BindToSet(cmd, pointShadowPipeline.GetLayout(), 0);
                scene.DrawShadowPoint(cmd, pointShadowPipeline, faceMats[face], activePointPos, activePointFar);
                pointShadowMap.EndRendering(cmd);
            }
        }
        pointShadowMap.TransitionToShaderRead(cmd, 0);


        // --- SceneView framebuffer pass (mismo contenido que el main pass) ---
        float dbAspect = static_cast<float>(debugFb.GetExtent().width) /
                         static_cast<float>(debugFb.GetExtent().height);

        debugFb.BeginRendering(cmd);
        pipeline.Bind(cmd);
        boneSSBO.Bind(cmd, *pipeline.GetLayout());  // set 2 — bone matrices
        scene.Draw(cmd, pipeline, camera, dbAspect, lights);
        skybox.Draw(cmd, camera.GetView(), camera.GetProjection(dbAspect), -activeDirDir);
        pipeline.BindTransparent(cmd);
        scene.DrawTransparent(cmd, pipeline, camera, dbAspect, lights);
        // Light gizmos drawn on top in the scene-view framebuffer
        {
            glm::mat4 vp = camera.GetProjection(dbAspect) * camera.GetView();
            gizmo.Draw(cmd, registry, vp, editor.GetSelected());
        }
        debugFb.EndRendering(cmd);
        debugFb.TransitionToShaderRead(cmd);

        // --- Volumetric Lighting post-process ---
        // postFb tracks the latest composited framebuffer (starts on debugFb, advances with each pass)
        VulkanFramebuffer* postFb = &debugFb;
        {
            VolumetricLightComponent* volComp = nullptr;
            registry.Each<VolumetricLightComponent>([&](Entity e, VolumetricLightComponent& v) {
                if (v.enabled && !volComp) volComp = &v;
            });

            if (volComp && hasDir)
            {
                glm::vec3 sunWorldDir = glm::normalize(-activeDirDir);
                // Fade out below horizon
                float sunAbove = glm::clamp(sunWorldDir.y * 5.0f + 1.0f, 0.0f, 1.0f);

                glm::mat4 view  = camera.GetView();
                glm::mat4 proj  = camera.GetProjection(dbAspect);
                glm::mat4 invVP = glm::inverse(proj * view);

                volumetricLight.Draw(cmd,
                    debugFb.GetColorView(), debugFb.GetSampler(),
                    debugFb.GetDepthView(), debugFb.GetSampler(),
                    shadowMap.GetArrayView(), shadowMap.GetSampler(),
                    invVP,
                    cascadeMatrices,
                    cascadeSplits,
                    sunWorldDir,
                    activeDirColor,
                    activeDirIntensity,
                    camera.GetPosition(),
                    lights.GetDescriptorSet(),
                    volComp->numSteps,
                    volComp->density,
                    volComp->absorption,
                    volComp->g,
                    volComp->intensity * sunAbove,
                    volComp->maxDistance,
                    volComp->jitter,
                    volComp->tint);

                volumetricLight.GetOutput().TransitionToShaderRead(cmd);
                postFb = &volumetricLight.GetOutput();
            }
        }

        // --- Volumetric Fog post-process ---
        {
            VolumetricFogComponent* fogComp = nullptr;
            registry.Each<VolumetricFogComponent>([&](Entity e, VolumetricFogComponent& f) {
                if (f.enabled && !fogComp) fogComp = &f;
            });

            if (fogComp)
            {
                glm::mat4 view  = camera.GetView();
                glm::mat4 proj  = camera.GetProjection(dbAspect);
                glm::mat4 invVP = glm::inverse(proj * view);

                glm::vec3 sunTowardDir = hasDir ? glm::normalize(-activeDirDir) : glm::vec3(0.0f, 1.0f, 0.0f);
                float     sunIntensity = hasDir ? activeDirIntensity : 0.0f;

                volumetricFog.Draw(cmd,
                    postFb->GetColorView(), postFb->GetSampler(),
                    debugFb.GetDepthView(), debugFb.GetSampler(),
                    invVP,
                    camera.GetPosition(),
                    fogComp->fogColor,       fogComp->density,
                    fogComp->horizonColor,   fogComp->heightFalloff,
                    fogComp->sunScatterColor,fogComp->scatterStrength,
                    sunTowardDir,            sunIntensity,
                    fogComp->heightOffset,
                    fogComp->maxFogAmount,
                    fogComp->fogStart,
                    fogComp->fogEnd,
                    lights.GetDescriptorSet());

                volumetricFog.GetOutput().TransitionToShaderRead(cmd);
                postFb = &volumetricFog.GetOutput();
            }
        }

        // --- Sun Shafts post-process ---
        {
            SunShaftsComponent* shaftsComp = nullptr;
            registry.Each<SunShaftsComponent>([&](Entity e, SunShaftsComponent& ss) {
                if (ss.enabled && !shaftsComp) shaftsComp = &ss;
            });

            if (shaftsComp && activeDirDir != glm::vec3(0.0f))
            {
                glm::mat4 vp       = camera.GetProjection(dbAspect) * camera.GetView();
                glm::vec3 sunWorldDir = glm::normalize(-activeDirDir);
                glm::vec4 sunClip  = vp * glm::vec4(sunWorldDir * 1000.0f, 1.0f);

                // Sun behind the camera: push UV off-screen so the shader zeroes all effects
                bool sunInFront = (sunClip.w > 0.0f) &&
                                  (glm::dot(camForward, sunWorldDir) > 0.0f);
                // NOTE: proj[1][1] is already negated (Vulkan Y-flip), so NDC Y
                // is negative for points above the horizon.  No extra negation needed.
                glm::vec2 sunUV = sunInFront
                    ? glm::vec2(sunClip.x / sunClip.w, sunClip.y / sunClip.w) * 0.5f + 0.5f
                    : glm::vec2(-10.0f);   // off-screen → onScreen gate = 0

                float sunHeight = glm::normalize(-activeDirDir).y;

                // Occlusion pass — builds depth-aware sun disk + sky mask
                occlusionPass.Draw(cmd,
                    debugFb.GetDepthView(), debugFb.GetSampler(),
                    sunUV, shaftsComp->sunRadius);

                // Sun shafts reads from postFb so volumetric + god-rays stack correctly
                sunShafts.Draw(cmd,
                    postFb->GetColorView(), postFb->GetSampler(),
                    occlusionPass.GetView(),
                    debugFb.GetDepthView(),
                    sunUV,
                    shaftsComp->density,    shaftsComp->bloomScale,
                    shaftsComp->decay,      shaftsComp->weight,
                    shaftsComp->exposure,
                    shaftsComp->tint,
                    sunHeight);
                sunShafts.GetOutput().TransitionToShaderRead(cmd);
                editor.SetSceneViewFramebuffer(&sunShafts.GetOutput());
            }
            else
            {
                editor.SetSceneViewFramebuffer(postFb);
            }
        }

        // --- Main pass ---
        VulkanRenderer::Get().BeginMainPass();
        pipeline.Bind(cmd);
        boneSSBO.Bind(cmd, *pipeline.GetLayout());  // re-bind for main pass
        scene.Draw(cmd, pipeline, camera, aspect, lights);

        // Skybox — drawn after opaques so depth test skips covered pixels
        skybox.Draw(cmd, camera.GetView(), camera.GetProjection(aspect), -activeDirDir);

        // Transparents drawn after skybox so skybox doesn't overwrite them
        pipeline.Bind(cmd);
        boneSSBO.Bind(cmd, *pipeline.GetLayout());  // re-bind for transparent pass
        scene.DrawTransparent(cmd, pipeline, camera, aspect, lights);

        debugOverlay.Draw(cmd, debugFb, shadowMap);

        editor.Draw(camera, registry, dt);
        editor.Render(cmd);

        VulkanRenderer::Get().EndFrame();
    }

    VulkanInstance::Get().GetDevice().waitIdle();
    editor.Shutdown();
    gizmo.Destroy();
    boneSSBO.Destroy();
    occlusionPass.Destroy();
    volumetricLight.Destroy();
    volumetricFog.Destroy();
    sunShafts.Destroy();
    debugOverlay.Destroy();
    skybox.Destroy();
    debugFb.Destroy();
    pointShadowPipeline.Destroy();
    pointShadowMap.Destroy();
    spotShadowMap.Destroy();
    shadowPipeline.Destroy();
    shadowMap.Destroy();
    lights.Destroy();
    scene.Destroy();
    MaterialManager::Get().Destroy();
    MeshManager::Get().Destroy();
    floorMat.Destroy();
    albedoTex.Destroy();
    pipeline.Destroy();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

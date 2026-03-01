#include <GLFW/glfw3.h>
#include <iostream>
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
#include "Core/Material.h"
#include "Core/MaterialLayout.h"
#include "Core/Camera.h"
#include "Core/Scene.h"
#include "Core/LightBuffer.h"
#include <glm/gtc/constants.hpp>

int main()
{
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    GLFWwindow* window = glfwCreateWindow(800, 600, "EderGraphics", nullptr, nullptr);
    if (!window) return -1;

    glfwSetFramebufferSizeCallback(window, [](GLFWwindow*, int, int)
    {
        VulkanRenderer::Get().SetFramebufferResized();
    });

    Camera camera({ 0.0f, 0.0f, 0.0f }, 35.0f, 50.0f);
    camera.fpsMode = true;
    camera.fpsPos  = { 0.0f, 2.0f, 12.0f };
    camera.SetOrientation(0.0f, 0.0f);  // mirar horizontal al frente
    glfwSetWindowUserPointer(window, &camera);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    VulkanPipeline       pipeline;
    VulkanMesh           mesh;
    VulkanTexture        albedoTex;
    Material             material;
    Material             floorMat;
    Material             glassMat;
    VulkanFramebuffer    debugFb;
    VulkanDebugOverlay   debugOverlay;
    VulkanSkybox         skybox;
    VulkanShadowMap          shadowMap;
    VulkanShadowPipeline     shadowPipeline;
    VulkanSpotShadowMap       spotShadowMap;
    VulkanPointShadowMap      pointShadowMap;
    VulkanPointShadowPipeline pointShadowPipeline;
    Scene                scene;
    LightBuffer          lights;

    glm::mat4 spotMatrix;
    glm::vec3 spotPos      = glm::vec3(0.0f, 15.0f, 0.0f);
    glm::vec3 spotDir      = glm::normalize(glm::vec3(0.0f, -1.0f, 0.3f));
    float     spotOuterCos = std::cos(glm::radians(40.0f));
    float     spotFar      = 50.0f;
    glm::vec3 pointShadowPos = glm::vec3(-5.0f, 8.0f, 0.0f);
    float     pointShadowFar = 100.0f;

    try
    {
        VulkanInstance::Get().Init(window);
        VulkanSwapchain::Get().Init(window);
        VulkanRenderer::Get().Init();
        VulkanRenderer::Get().SetWindow(window);

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
                  .AddFloat("emissiveIntensity");
            material.Build(layout, pipeline);
            floorMat.Build(layout, pipeline);
            glassMat.Build(layout, pipeline);
        }

        // Box material — warm off-white
        material.SetVec4 ("albedo",            glm::vec4(1.0f, 0.92f, 0.78f, 1.0f));
        material.SetFloat("roughness",         0.55f);
        material.SetFloat("metallic",          0.0f);
        material.SetFloat("emissiveIntensity", 0.0f);

        // Floor material — cool grey
        floorMat.SetVec4 ("albedo",            glm::vec4(0.55f, 0.58f, 0.62f, 1.0f));
        floorMat.SetFloat("roughness",         0.85f);
        floorMat.SetFloat("metallic",          0.0f);
        floorMat.SetFloat("emissiveIntensity", 0.0f);

        // Glass material — translucent blue
        glassMat.SetVec4 ("albedo",            glm::vec4(0.55f, 0.78f, 1.0f, 0.35f));
        glassMat.SetFloat("roughness",         0.05f);
        glassMat.SetFloat("metallic",          0.0f);
        glassMat.SetFloat("emissiveIntensity", 0.0f);
        glassMat.opacity = 0.35f;

        try { albedoTex.Load("assets/box_albedo.jpg"); }
        catch (const std::exception& e)
        {
            std::cerr << "[WARNING] " << e.what() << std::endl;
            albedoTex.CreateDefault();
        }
        material.BindTexture(0, albedoTex);
        floorMat.BindTexture(0, albedoTex);
        glassMat.BindTexture(0, albedoTex);

        mesh.Load("assets/box.fbx");
        if (mesh.GetIndexCount() == 0)
            throw std::runtime_error("Mesh empty");

        // Floor — large flat slab at y = -1 (top face at y = -0.5)
        {
            SceneObject& floor = scene.Add(mesh, floorMat);
            floor.transform.position = { 0.0f, -1.0f, 0.0f };
            floor.transform.scale    = { 30.0f, 1.0f, 20.0f };
        }

        // Glass box — transparent panel in front of the boxes
        {
            SceneObject& glass = scene.Add(mesh, glassMat);
            glass.transform.position = { 0.0f, 1.5f, -4.0f };
            glass.transform.scale    = { 3.0f, 4.0f, 0.2f };
        }

        // Boxes on top of the floor (bottom at y = -0.5 = top of floor)
        constexpr int   cols    = 5;
        constexpr int   rows    = 3;
        constexpr float spacing = 4.5f;
        const float heights[3]  = { 1.0f, 2.0f, 1.5f };
        for (int row = 0; row < rows; row++)
        {
            for (int col = 0; col < cols; col++)
            {
                float h = heights[(col + row) % 3];
                SceneObject& obj = scene.Add(mesh, material);
                obj.transform.position.x = (col - (cols - 1) * 0.5f) * spacing;
                obj.transform.position.y = (h - 1.0f) * 0.5f; // center so bottom = -0.5
                obj.transform.position.z = (row - (rows - 1) * 0.5f) * spacing;
                obj.transform.scale.y    = h;
            }
        }

        // Shadow map must be created before lights.Build so the sampler is available
        shadowMap.Create(1024);
        shadowPipeline.Create(shadowMap.GetFormat());

        spotShadowMap.Create(1024);
        pointShadowMap.Create(512);
        pointShadowPipeline.Create(pointShadowMap.GetFormat());

        lights.Build(pipeline);

        // Sun — low angle to cast long shadows across the floor
        DirectionalLight sun;
        sun.direction = glm::normalize(glm::vec3(-1.0f, -0.8f, -0.4f));
        sun.color     = glm::vec3(1.0f, 0.95f, 0.85f);
        sun.intensity = .0f;
        //lights.AddDirectional(sun);

        // Soft fill light from above (no shadows)
        {
            PointLight fill;
            fill.position  = glm::vec3(0.0f, 12.0f, 0.0f);
            fill.color     = glm::vec3(0.45f, 0.55f, 0.75f);
            fill.intensity = 1.5f;
            fill.radius    = 60.0f;
            lights.AddPoint(fill);
        }

        // Spot light with shadow (slot 0)
        {
            SpotLight spot;
            spot.position  = spotPos;
            spot.direction = spotDir;
            spot.innerCos  = std::cos(glm::radians(30.0f));
            spot.outerCos  = spotOuterCos;
            spot.color     = glm::vec3(1.0f, 0.9f, 0.7f);
            spot.intensity = 1.0f;
            spot.radius    = spotFar;
            spot.shadowIdx = 0;  // uses spot shadow slot 0
            lights.AddSpot(spot);
        }

        // Point light with shadow (slot 0)
        {
            PointLight pShadow;
            pShadow.position  = pointShadowPos;
            pShadow.color     = glm::vec3(1.0f, 0.7f, 0.4f);
            pShadow.intensity = 1.0f;
            pShadow.radius    = pointShadowFar;
            pShadow.shadowIdx = 0;
            lights.AddPoint(pShadow);
        }
        lights.SetPointFarPlane(0, pointShadowFar);

        // Bind cascade shadow map (array) into the light descriptor set
        lights.BindShadowMap(shadowMap.GetArrayView(), shadowMap.GetSampler());
        lights.BindSpotShadowMap(spotShadowMap.GetArrayView(), spotShadowMap.GetSampler());
        lights.BindPointShadowMap(pointShadowMap.GetCubeArrayView(), pointShadowMap.GetSampler());

        auto& sc = VulkanSwapchain::Get();
        debugFb.Create(sc.GetExtent().width / 2, sc.GetExtent().height / 2,
                       sc.GetFormat(), VulkanRenderer::Get().GetDepthFormat());
        debugOverlay.Create(sc.GetFormat(), VulkanRenderer::Get().GetDepthFormat());
        skybox.Create(sc.GetFormat(), VulkanRenderer::Get().GetDepthFormat());
    }
    catch (const std::exception& e)
    {
        std::cerr << "[ERROR] " << e.what() << std::endl;
        return -1;
    }

    const glm::vec3 sunDir = glm::normalize(glm::vec3(-1.0f, -0.8f, -0.4f));

    glm::mat4 cascadeMatrices[VulkanShadowMap::NUM_CASCADES];
    glm::vec4 cascadeSplits;

    double prevTime   = glfwGetTime();
    double lastMouseX = 0.0, lastMouseY = 0.0;
    bool   firstMouse = true;

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        double currTime = glfwGetTime();
        float  dt       = static_cast<float>(currTime - prevTime);
        prevTime        = currTime;

        // FPS look (cursor capturado)
        double mx, my;
        glfwGetCursorPos(window, &mx, &my);
        if (!firstMouse)
            camera.FPSLook(static_cast<float>(mx - lastMouseX),
                           static_cast<float>(my - lastMouseY));
        firstMouse = false;
        lastMouseX = mx;
        lastMouseY = my;

        // Movimiento WASD (sin componente vertical en fwd para no "volar" con W)
        {
            const float spd  = 8.0f;
            glm::vec3 fwd    = camera.GetForward();
            glm::vec3 right  = camera.GetRight();
            glm::vec3 fwdXZ  = glm::normalize(glm::vec3(fwd.x, 0.0f, fwd.z));
            if (glfwGetKey(window, GLFW_KEY_W)            == GLFW_PRESS) camera.fpsPos += fwdXZ * spd * dt;
            if (glfwGetKey(window, GLFW_KEY_S)            == GLFW_PRESS) camera.fpsPos -= fwdXZ * spd * dt;
            if (glfwGetKey(window, GLFW_KEY_A)            == GLFW_PRESS) camera.fpsPos -= right  * spd * dt;
            if (glfwGetKey(window, GLFW_KEY_D)            == GLFW_PRESS) camera.fpsPos += right  * spd * dt;
            if (glfwGetKey(window, GLFW_KEY_SPACE)        == GLFW_PRESS) camera.fpsPos.y += spd * dt;
            if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) camera.fpsPos.y -= spd * dt;
            if (glfwGetKey(window, GLFW_KEY_ESCAPE)       == GLFW_PRESS) glfwSetWindowShouldClose(window, GLFW_TRUE);
        }

        auto&  sc     = VulkanSwapchain::Get();
        float  aspect = static_cast<float>(sc.GetExtent().width) /
                        static_cast<float>(sc.GetExtent().height);

        float  t    = static_cast<float>(currTime);
        auto&  objs = scene.GetObjects();
        // Index 0 is the floor — skip it
        for (size_t i = 1; i < objs.size(); i++)
            objs[i].transform.rotation.y = t * 25.0f + static_cast<float>(i) * 1.2f;

        glm::vec3 camForward = camera.GetForward();
        shadowMap.ComputeCascades(
            camera.GetView(), camera.GetProjection(aspect),
            sunDir, camera.nearPlane, camera.farPlane,
            cascadeMatrices, cascadeSplits);
        // Spotlight tipo linterna: ligeramente por encima y delante de la camara
        spotDir = camera.GetForward();
        spotPos = camera.GetPosition()
                + camera.GetRight()  * 0.25f   // desplazamiento lateral (hombro derecho)
                + glm::vec3(0.0f, -0.15f, 0.0f); // ligeramente bajo el ojo
        lights.UpdateSpotPosDir(0, spotPos, spotDir);
        lights.SetCascadeData(cascadeMatrices, cascadeSplits);
        lights.SetCameraForward(camForward);
        spotMatrix = VulkanSpotShadowMap::ComputeMatrix(spotPos, spotDir, spotOuterCos, 0.3f, spotFar);
        lights.SetSpotMatrix(0, spotMatrix);
        lights.Update(camera.GetPosition());

        VulkanRenderer::Get().BeginFrame();

        if (!VulkanRenderer::Get().IsFrameStarted())
        {
            uint32_t fw = sc.GetExtent().width  / 2;
            uint32_t fh = sc.GetExtent().height / 2;
            if (fw != debugFb.GetExtent().width || fh != debugFb.GetExtent().height)
                debugFb.Recreate(fw, fh);
            continue;
        }

        auto cmd = VulkanRenderer::Get().GetCommandBuffer();

        // --- Shadow pass (4 cascades) ---
        for (uint32_t c = 0; c < VulkanShadowMap::NUM_CASCADES; c++)
        {
            shadowMap.BeginRendering(cmd, c);
            shadowPipeline.Bind(cmd);
            scene.DrawShadow(cmd, shadowPipeline, cascadeMatrices[c]);
            shadowMap.EndRendering(cmd);
        }
        shadowMap.TransitionToShaderRead(cmd);

        // --- Spot shadow pass (slot 0) ---
        spotShadowMap.BeginRendering(cmd, 0);
        shadowPipeline.Bind(cmd);
        scene.DrawShadow(cmd, shadowPipeline, spotMatrix);
        spotShadowMap.EndRendering(cmd);
        spotShadowMap.TransitionToShaderRead(cmd);

        // --- Point shadow pass (6 faces, slot 0) ---
        {
            auto faceMats = VulkanPointShadowMap::ComputeFaceMatrices(pointShadowPos, 0.1f, pointShadowFar);
            for (uint32_t face = 0; face < 6; face++)
            {
                pointShadowMap.BeginRendering(cmd, 0, face);
                pointShadowPipeline.Bind(cmd);
                scene.DrawShadowPoint(cmd, pointShadowPipeline, faceMats[face], pointShadowPos, pointShadowFar);
                pointShadowMap.EndRendering(cmd);
            }
            pointShadowMap.TransitionToShaderRead(cmd, 0);
        }

        // --- Offscreen debug framebuffer pass ---
        float dbAspect = static_cast<float>(debugFb.GetExtent().width) /
                         static_cast<float>(debugFb.GetExtent().height);

        debugFb.BeginRendering(cmd);
        pipeline.Bind(cmd);
        scene.Draw(cmd, pipeline, camera, dbAspect, lights);
        scene.DrawTransparent(cmd, pipeline, camera, dbAspect, lights);
        debugFb.EndRendering(cmd);
        debugFb.TransitionToShaderRead(cmd);

        // --- Main pass ---
        VulkanRenderer::Get().BeginMainPass();
        pipeline.Bind(cmd);
        scene.Draw(cmd, pipeline, camera, aspect, lights);
        scene.DrawTransparent(cmd, pipeline, camera, aspect, lights);

        // Skybox — drawn after scene so depth test skips covered pixels
        {
            auto invVP = glm::inverse(camera.GetProjection(aspect) * camera.GetView());
            skybox.Draw(cmd, invVP, -sunDir);
        }

        debugOverlay.Draw(cmd, debugFb, shadowMap);

        VulkanRenderer::Get().EndFrame();
    }

    VulkanInstance::Get().GetDevice().waitIdle();
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
    floorMat.Destroy();
    material.Destroy();
    albedoTex.Destroy();
    mesh.Destroy();
    pipeline.Destroy();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

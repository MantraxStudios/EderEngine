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
#include "Renderer/Vulkan/VulkanShadowMap.h"
#include "Renderer/Vulkan/VulkanShadowPipeline.h"
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

    Camera camera({ 0.0f, 1.5f, 0.0f }, 35.0f, 50.0f);
    glfwSetWindowUserPointer(window, &camera);
    glfwSetScrollCallback(window, [](GLFWwindow* w, double, double dy)
    {
        static_cast<Camera*>(glfwGetWindowUserPointer(w))->OnScroll(dy);
    });

    VulkanPipeline       pipeline;
    VulkanMesh           mesh;
    VulkanTexture        albedoTex;
    Material             material;
    Material             floorMat;
    VulkanFramebuffer    debugFb;
    VulkanDebugOverlay   debugOverlay;
    VulkanShadowMap      shadowMap;
    VulkanShadowPipeline shadowPipeline;
    Scene                scene;
    LightBuffer          lights;

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

        try { albedoTex.Load("assets/box_albedo.jpg"); }
        catch (const std::exception& e)
        {
            std::cerr << "[WARNING] " << e.what() << std::endl;
            albedoTex.CreateDefault();
        }
        material.BindTexture(0, albedoTex);
        floorMat.BindTexture(0, albedoTex);

        mesh.Load("assets/box.fbx");
        if (mesh.GetIndexCount() == 0)
            throw std::runtime_error("Mesh empty");

        // Floor — large flat slab at y = -1 (top face at y = -0.5)
        {
            SceneObject& floor = scene.Add(mesh, floorMat);
            floor.transform.position = { 0.0f, -1.0f, 0.0f };
            floor.transform.scale    = { 30.0f, 1.0f, 20.0f };
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

        lights.Build(pipeline);

        // Sun — low angle to cast long shadows across the floor
        DirectionalLight sun;
        sun.direction = glm::normalize(glm::vec3(-1.0f, -0.8f, -0.4f));
        sun.color     = glm::vec3(1.0f, 0.95f, 0.85f);
        sun.intensity = 1.4f;
        lights.AddDirectional(sun);

        // Soft fill light from above (no shadows)
        {
            PointLight fill;
            fill.position  = glm::vec3(0.0f, 12.0f, 0.0f);
            fill.color     = glm::vec3(0.45f, 0.55f, 0.75f);
            fill.intensity = 4.0f;
            fill.radius    = 60.0f;
            lights.AddPoint(fill);
        }

        // Bind cascade shadow map (array) into the light descriptor set
        lights.BindShadowMap(shadowMap.GetArrayView(), shadowMap.GetSampler());

        auto& sc = VulkanSwapchain::Get();
        debugFb.Create(sc.GetExtent().width / 2, sc.GetExtent().height / 2,
                       sc.GetFormat(), VulkanRenderer::Get().GetDepthFormat());
        debugOverlay.Create(sc.GetFormat(), VulkanRenderer::Get().GetDepthFormat());
    }
    catch (const std::exception& e)
    {
        std::cerr << "[ERROR] " << e.what() << std::endl;
        return -1;
    }

    const glm::vec3 sunDir = glm::normalize(glm::vec3(-1.0f, -0.8f, -0.4f));

    glm::mat4 cascadeMatrices[VulkanShadowMap::NUM_CASCADES];
    glm::vec4 cascadeSplits;

    double prevTime = glfwGetTime();

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        double currTime = glfwGetTime();
        float  dt       = static_cast<float>(currTime - prevTime);
        prevTime        = currTime;

        camera.Update(window, dt);

        auto&  sc     = VulkanSwapchain::Get();
        float  aspect = static_cast<float>(sc.GetExtent().width) /
                        static_cast<float>(sc.GetExtent().height);

        float  t    = static_cast<float>(currTime);
        auto&  objs = scene.GetObjects();
        // Index 0 is the floor — skip it
        for (size_t i = 1; i < objs.size(); i++)
            objs[i].transform.rotation.y = t * 25.0f + static_cast<float>(i) * 1.2f;

        glm::vec3 camForward = glm::normalize(camera.target - camera.GetPosition());
        shadowMap.ComputeCascades(
            camera.GetView(), camera.GetProjection(aspect),
            sunDir, camera.nearPlane, camera.farPlane,
            cascadeMatrices, cascadeSplits);
        lights.SetCascadeData(cascadeMatrices, cascadeSplits);
        lights.SetCameraForward(camForward);
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

        // --- Offscreen debug framebuffer pass ---
        float dbAspect = static_cast<float>(debugFb.GetExtent().width) /
                         static_cast<float>(debugFb.GetExtent().height);

        debugFb.BeginRendering(cmd);
        pipeline.Bind(cmd);
        scene.Draw(cmd, pipeline, camera, dbAspect, lights);
        debugFb.EndRendering(cmd);
        debugFb.TransitionToShaderRead(cmd);

        // --- Main pass ---
        VulkanRenderer::Get().BeginMainPass();
        pipeline.Bind(cmd);
        scene.Draw(cmd, pipeline, camera, aspect, lights);
        debugOverlay.Draw(cmd, debugFb, shadowMap);

        VulkanRenderer::Get().EndFrame();
    }

    VulkanInstance::Get().GetDevice().waitIdle();
    debugOverlay.Destroy();
    debugFb.Destroy();
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

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

    Camera camera({ 0.0f, 0.0f, 0.0f }, 80.0f, 45.0f);
    glfwSetWindowUserPointer(window, &camera);
    glfwSetScrollCallback(window, [](GLFWwindow* w, double, double dy)
    {
        static_cast<Camera*>(glfwGetWindowUserPointer(w))->OnScroll(dy);
    });

    VulkanPipeline     pipeline;
    VulkanMesh         mesh;
    VulkanTexture      albedoTex;
    Material           material;
    VulkanFramebuffer  debugFb;
    VulkanDebugOverlay debugOverlay;
    Scene              scene;
    LightBuffer        lights;

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
        }

        material.SetVec4 ("albedo",            glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
        material.SetFloat("roughness",         0.5f);
        material.SetFloat("metallic",          0.0f);
        material.SetFloat("emissiveIntensity", 0.0f);

        try { albedoTex.Load("assets/box_albedo.jpg"); }
        catch (const std::exception& e)
        {
            std::cerr << "[WARNING] " << e.what() << std::endl;
            albedoTex.CreateDefault();
        }
        material.BindTexture(0, albedoTex);

        mesh.Load("assets/box.fbx");
        if (mesh.GetIndexCount() == 0)
            throw std::runtime_error("Mesh empty");

        constexpr int   cols    = 100;
        constexpr int   rows    = 50;
        constexpr float spacing = 2.0f;
        for (int row = 0; row < rows; row++)
        {
            for (int col = 0; col < cols; col++)
            {
                SceneObject& obj = scene.Add(mesh, material);
                obj.transform.position.x = (col - (cols - 1) * 0.5f) * spacing;
                obj.transform.position.z = (row - (rows - 1) * 0.5f) * spacing;
            }
        }

        lights.Build(pipeline);

        {
            DirectionalLight sun;
            sun.direction = glm::normalize(glm::vec3(-1.0f, -2.0f, -1.0f));
            sun.color     = glm::vec3(1.0f, 0.95f, 0.85f);
            sun.intensity = 1.2f;
            lights.AddDirectional(sun);
        }
        {
            PointLight p;
            p.position  = glm::vec3(0.0f, 5.0f, 0.0f);
            p.color     = glm::vec3(0.4f, 0.7f, 1.0f);
            p.intensity = 8.0f;
            p.radius    = 40.0f;
            lights.AddPoint(p);
        }
        {
            SpotLight sl;
            sl.position  = glm::vec3(50.0f, 15.0f, 0.0f);
            sl.direction = glm::normalize(glm::vec3(0.0f, -1.0f, 0.0f));
            sl.color     = glm::vec3(1.0f, 0.8f, 0.3f);
            sl.intensity = 20.0f;
            sl.radius    = 60.0f;
            sl.innerCos  = glm::cos(glm::radians(15.0f));
            sl.outerCos  = glm::cos(glm::radians(30.0f));
            lights.AddSpot(sl);
        }

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
        for (size_t i = 0; i < objs.size(); i++)
            objs[i].transform.rotation.y = t * 45.0f + static_cast<float>(i) * 0.5f;

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

        float dbAspect = static_cast<float>(debugFb.GetExtent().width) /
                         static_cast<float>(debugFb.GetExtent().height);

        debugFb.BeginRendering(cmd);
        pipeline.Bind(cmd);
        scene.Draw(cmd, pipeline, camera, dbAspect, lights);
        debugFb.EndRendering(cmd);
        debugFb.TransitionToShaderRead(cmd);

        VulkanRenderer::Get().BeginMainPass();
        pipeline.Bind(cmd);
        scene.Draw(cmd, pipeline, camera, aspect, lights);
        debugOverlay.Draw(cmd, debugFb);

        VulkanRenderer::Get().EndFrame();
    }

    VulkanInstance::Get().GetDevice().waitIdle();
    debugOverlay.Destroy();
    debugFb.Destroy();
    lights.Destroy();
    scene.Destroy();
    material.Destroy();
    albedoTex.Destroy();
    mesh.Destroy();
    pipeline.Destroy();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

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

    Camera camera({ 0.0f, 0.0f, 0.0f }, 5.0f, 45.0f);
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

        scene.Add(mesh, material);

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

        scene.GetObjects()[0].transform.rotation.y = static_cast<float>(currTime) * 45.0f;

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
        scene.Draw(cmd, pipeline, camera, dbAspect);
        debugFb.EndRendering(cmd);
        debugFb.TransitionToShaderRead(cmd);

        VulkanRenderer::Get().BeginMainPass();
        pipeline.Bind(cmd);
        scene.Draw(cmd, pipeline, camera, aspect);
        debugOverlay.Draw(cmd, debugFb);

        VulkanRenderer::Get().EndFrame();
    }

    VulkanInstance::Get().GetDevice().waitIdle();
    debugOverlay.Destroy();
    debugFb.Destroy();
    material.Destroy();
    albedoTex.Destroy();
    mesh.Destroy();
    pipeline.Destroy();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

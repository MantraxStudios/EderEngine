#include <GLFW/glfw3.h>
#include <iostream>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
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

    VulkanPipeline     pipeline;
    VulkanMesh         mesh;
    VulkanTexture      albedoTex;
    Material           material;
    VulkanFramebuffer  debugFb;
    VulkanDebugOverlay debugOverlay;

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

        material.SetVec4 ("albedo",            glm::vec4(0.8f, 0.8f, 0.8f, 1.0f));
        material.SetFloat("roughness",         0.5f);
        material.SetFloat("metallic",          0.0f);
        material.SetFloat("emissiveIntensity", 0.0f);

        try
        {
            albedoTex.Load("assets/box_albedo.jpg");
        }
        catch (const std::exception& e)
        {
            std::cerr << "[WARNING] Texture load failed: " << e.what() << std::endl;
            albedoTex.CreateDefault();
        }

        if (!albedoTex.IsValid())
            throw std::runtime_error("Texture is invalid");

        material.BindTexture(0, albedoTex);
        mesh.Load("assets/box.fbx");

        if (mesh.GetIndexCount() == 0 || mesh.GetVertexCount() == 0)
            throw std::runtime_error("Mesh is empty after loading");

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

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        auto& swapchain = VulkanSwapchain::Get();
        float aspect    = static_cast<float>(swapchain.GetExtent().width) /
                          static_cast<float>(swapchain.GetExtent().height);

        glm::mat4 model = glm::rotate(glm::mat4(1.0f),
            static_cast<float>(glfwGetTime()), glm::vec3(0.0f, 1.0f, 0.0f));

        glm::mat4 view = glm::lookAt(
            glm::vec3(0.0f, 2.0f, 5.0f),
            glm::vec3(0.0f, 0.0f, 0.0f),
            glm::vec3(0.0f, 1.0f, 0.0f));

        glm::mat4 proj = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);
        proj[1][1] *= -1;

        glm::mat4 mvp = proj * view * model;

        struct PushData { glm::mat4 mvp; glm::mat4 model; } push{ mvp, model };

        VulkanRenderer::Get().BeginFrame();

        if (!VulkanRenderer::Get().IsFrameStarted())
            continue;

        auto  cmd    = VulkanRenderer::Get().GetCommandBuffer();
        auto& layout = pipeline.GetLayout();

        // Offscreen pass → debug framebuffer
        debugFb.BeginRendering(cmd);
        pipeline.Bind(cmd);
        material.Bind(cmd, *layout);
        cmd.pushConstants(*layout, vk::ShaderStageFlagBits::eVertex, 0, sizeof(PushData), &push);
        mesh.Draw(cmd);
        debugFb.EndRendering(cmd);
        debugFb.TransitionToShaderRead(cmd);

        // Main pass → swapchain
        VulkanRenderer::Get().BeginMainPass();
        pipeline.Bind(cmd);
        material.Bind(cmd, *layout);
        cmd.pushConstants(*layout, vk::ShaderStageFlagBits::eVertex, 0, sizeof(PushData), &push);
        mesh.Draw(cmd);
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

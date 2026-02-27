#include <GLFW/glfw3.h>
#include <iostream>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "Renderer/Vulkan/VulkanInstance.h"
#include "Renderer/Vulkan/VulkanSwapchain.h"
#include "Renderer/Vulkan/VulkanPipeline.h"
#include "Renderer/Vulkan/VulkanMesh.h"
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

    VulkanPipeline pipeline;
    VulkanMesh     mesh;
    Material       material;

    try
    {
        VulkanInstance::Get().Init(window);
        VulkanSwapchain::Get().Init(window);
        VulkanRenderer::Get().Init();
        VulkanRenderer::Get().SetWindow(window);

        pipeline.Create("shaders/triangle.vert.spv", "shaders/triangle.frag.spv",
                         VulkanSwapchain::Get().GetFormat());

        {
            MaterialLayout layout;
            layout.AddVec4 ("albedo")
                  .AddFloat ("roughness")
                  .AddFloat ("metallic")
                  .AddFloat ("emissiveIntensity");
            material.Build(layout, pipeline);
        }

        material.SetVec4 ("albedo",            glm::vec4(1.0f, 0.6f, 0.2f, 1.0f));
        material.SetFloat("roughness",         0.4f);
        material.SetFloat("metallic",          0.8f);
        material.SetFloat("emissiveIntensity", 0.05f);

        mesh.Load("assets/box.fbx");
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

        VulkanRenderer::Get().BeginFrame();

        auto  cmd    = VulkanRenderer::Get().GetCommandBuffer();
        auto& layout = pipeline.GetLayout();

        pipeline.Bind(cmd);
        material.Bind(cmd, *layout);

        struct PushData { glm::mat4 mvp; glm::mat4 model; } push{ mvp, model };
        cmd.pushConstants(*layout, vk::ShaderStageFlagBits::eVertex, 0, sizeof(PushData), &push);
        mesh.Draw(cmd);

        VulkanRenderer::Get().EndFrame();
    }

    VulkanInstance::Get().GetDevice().waitIdle();
    material.Destroy();
    mesh.Destroy();
    pipeline.Destroy();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
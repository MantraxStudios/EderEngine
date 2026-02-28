#include "Scene.h"
#include "Camera.h"
#include "Material.h"
#include "LightBuffer.h"
#include "Renderer/Vulkan/VulkanMesh.h"
#include "Renderer/Vulkan/VulkanPipeline.h"
#include <glm/glm.hpp>

SceneObject& Scene::Add(VulkanMesh& mesh, Material& material)
{
    objects.push_back({ &mesh, &material, Transform{} });
    return objects.back();
}

void Scene::Draw(vk::CommandBuffer cmd, VulkanPipeline& pipeline, const Camera& camera, float aspect, LightBuffer& lights)
{
    glm::mat4 viewProj        = camera.GetProjection(aspect) * camera.GetView();
    vk::PipelineLayout layout = *pipeline.GetLayout();

    std::map<std::pair<VulkanMesh*, Material*>, std::vector<glm::mat4>> groups;
    for (auto& obj : objects)
    {
        if (!obj.mesh || !obj.material) continue;
        groups[{ obj.mesh, obj.material }].push_back(obj.transform.GetMatrix());
    }

    std::vector<glm::mat4> allMatrices;
    for (auto& [key, matrices] : groups)
        for (auto& m : matrices)
            allMatrices.push_back(m);

    instanceBuffer.Upload(allMatrices);
    instanceBuffer.Bind(cmd);
    lights.Bind(cmd, layout);

    uint32_t first = 0;
    for (auto& [key, matrices] : groups)
    {
        auto [mesh, mat] = key;
        mat->Bind(cmd, layout);
        cmd.pushConstants(layout, vk::ShaderStageFlagBits::eVertex, 0, sizeof(glm::mat4), &viewProj);
        mesh->DrawInstanced(cmd, first, static_cast<uint32_t>(matrices.size()));
        first += static_cast<uint32_t>(matrices.size());
    }
}

void Scene::Clear()
{
    objects.clear();
}

void Scene::Destroy()
{
    instanceBuffer.Destroy();
    objects.clear();
}

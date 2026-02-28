#include "Scene.h"
#include "Camera.h"
#include "Material.h"
#include "Renderer/Vulkan/VulkanMesh.h"
#include "Renderer/Vulkan/VulkanPipeline.h"

SceneObject& Scene::Add(VulkanMesh& mesh, Material& material)
{
    objects.push_back({ &mesh, &material, Transform{} });
    return objects.back();
}

void Scene::Draw(vk::CommandBuffer cmd, VulkanPipeline& pipeline, const Camera& camera, float aspect)
{
    glm::mat4 view = camera.GetView();
    glm::mat4 proj = camera.GetProjection(aspect);
    vk::PipelineLayout layout = *pipeline.GetLayout();

    struct PushData { glm::mat4 mvp; glm::mat4 model; };

    for (auto& obj : objects)
    {
        if (!obj.mesh || !obj.material) continue;

        glm::mat4 model = obj.transform.GetMatrix();
        PushData  push  = { proj * view * model, model };

        obj.material->Bind(cmd, layout);
        cmd.pushConstants(layout, vk::ShaderStageFlagBits::eVertex, 0, sizeof(PushData), &push);
        obj.mesh->Draw(cmd);
    }
}

void Scene::Clear()
{
    objects.clear();
}

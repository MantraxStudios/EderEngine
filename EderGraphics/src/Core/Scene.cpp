#include "Scene.h"
#include "Camera.h"
#include "Material.h"
#include "LightBuffer.h"
#include "Renderer/Vulkan/VulkanMesh.h"
#include "Renderer/Vulkan/VulkanPipeline.h"
#include "Renderer/Vulkan/VulkanShadowPipeline.h"
#include "Renderer/Vulkan/VulkanPointShadowPipeline.h"
#include <glm/glm.hpp>
#include <algorithm>

SceneObject& Scene::Add(VulkanMesh& mesh, Material& material)
{
    objects.push_back({ &mesh, &material, Transform{}, 0 });
    return objects.back();
}

void Scene::Remove(uint32_t entityId)
{
    objects.erase(
        std::remove_if(objects.begin(), objects.end(),
            [entityId](const SceneObject& o){ return o.entityId == entityId; }),
        objects.end());
}

void Scene::Draw(vk::CommandBuffer cmd, VulkanPipeline& pipeline, const Camera& camera, float aspect, LightBuffer& lights)
{
    glm::mat4 viewProj        = camera.GetProjection(aspect) * camera.GetView();
    vk::PipelineLayout layout = *pipeline.GetLayout();

    std::map<std::pair<VulkanMesh*, Material*>, std::vector<glm::mat4>> groups;
    for (auto& obj : objects)
    {
        if (!obj.mesh || !obj.material) continue;
        if (obj.material->IsTransparent()) continue;  // skip transparents
        if (obj.isSkinned) continue;                  // skip skinned — drawn separately
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

void Scene::DrawSkinned(vk::CommandBuffer cmd, VulkanPipeline& pipeline, const Camera& camera, float aspect, LightBuffer& lights,
                        const std::function<void(uint32_t)>& bindBonesFn)
{
    glm::mat4 viewProj        = camera.GetProjection(aspect) * camera.GetView();
    vk::PipelineLayout layout = *pipeline.GetLayout();

    lights.Bind(cmd, layout);

    for (auto& obj : objects)
    {
        if (!obj.mesh || !obj.material || !obj.isSkinned) continue;
        if (obj.material->IsTransparent()) continue;

        // Upload this object's single transform into the per-skinned instance buffer
        const glm::mat4 model = obj.transform.GetMatrix();
        skinnedInstanceBuffer.Upload({ model });
        skinnedInstanceBuffer.Bind(cmd);

        // Let the caller bind the correct BoneSSBO for this entity
        bindBonesFn(obj.entityId);

        obj.material->Bind(cmd, layout);
        cmd.pushConstants(layout, vk::ShaderStageFlagBits::eVertex, 0, sizeof(glm::mat4), &viewProj);
        obj.mesh->DrawInstanced(cmd, 0, 1);
    }
}

void Scene::DrawTransparent(vk::CommandBuffer cmd, VulkanPipeline& pipeline, const Camera& camera, float aspect, LightBuffer& lights)
{
    glm::mat4  viewProj = camera.GetProjection(aspect) * camera.GetView();
    glm::vec3  camPos   = camera.GetPosition();
    vk::PipelineLayout layout = *pipeline.GetLayout();

    // Collect + sort back-to-front
    struct Entry { VulkanMesh* mesh; Material* mat; glm::mat4 model; float depth; };
    std::vector<Entry> entries;
    for (auto& obj : objects)
    {
        if (!obj.mesh || !obj.material || !obj.material->IsTransparent()) continue;
        glm::mat4 m   = obj.transform.GetMatrix();
        float     d   = glm::length(glm::vec3(m[3]) - camPos);
        entries.push_back({ obj.mesh, obj.material, m, d });
    }
    std::stable_sort(entries.begin(), entries.end(),
              [](const Entry& a, const Entry& b){ return a.depth > b.depth; });
    if (entries.empty()) return;

    // Upload ALL sorted matrices at once into the SEPARATE transparent buffer
    std::vector<glm::mat4> sortedMats;
    sortedMats.reserve(entries.size());
    for (auto& e : entries) sortedMats.push_back(e.model);
    transparentInstanceBuffer.Upload(sortedMats);
    transparentInstanceBuffer.Bind(cmd);

    pipeline.BindTransparent(cmd);
    lights.Bind(cmd, layout);

    for (uint32_t i = 0; i < static_cast<uint32_t>(entries.size()); i++)
    {
        entries[i].mat->Bind(cmd, layout);
        cmd.pushConstants(layout, vk::ShaderStageFlagBits::eVertex, 0, sizeof(glm::mat4), &viewProj);
        entries[i].mesh->DrawInstanced(cmd, i, 1);
    }
}

void Scene::DrawShadow(vk::CommandBuffer cmd, VulkanShadowPipeline& shadowPipeline, const glm::mat4& lightViewProj)
{
    vk::PipelineLayout layout = *shadowPipeline.GetLayout();

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

    shadowInstanceBuffer.Upload(allMatrices);
    shadowInstanceBuffer.Bind(cmd);
    cmd.pushConstants(layout, vk::ShaderStageFlagBits::eVertex, 0, sizeof(glm::mat4), &lightViewProj);

    uint32_t first = 0;
    for (auto& [key, matrices] : groups)
    {
        auto [mesh, mat] = key;
        mesh->DrawInstanced(cmd, first, static_cast<uint32_t>(matrices.size()));
        first += static_cast<uint32_t>(matrices.size());
    }
}

void Scene::DrawShadowPoint(vk::CommandBuffer cmd, VulkanPointShadowPipeline& pipeline,
                            const glm::mat4& lightViewProj, const glm::vec3& lightPos, float farPlane)
{
    vk::PipelineLayout layout = pipeline.GetLayout();

    struct PushData { glm::mat4 viewProj; glm::vec4 lightPosAndFar; } push;
    push.viewProj       = lightViewProj;
    push.lightPosAndFar = glm::vec4(lightPos, farPlane);

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

    shadowInstanceBuffer.Upload(allMatrices);
    shadowInstanceBuffer.Bind(cmd);
    cmd.pushConstants(layout,
        vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        0, static_cast<uint32_t>(sizeof(PushData)), &push);

    uint32_t first = 0;
    for (auto& [key, matrices] : groups)
    {
        auto [mesh, mat] = key;
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
    shadowInstanceBuffer.Destroy();
    transparentInstanceBuffer.Destroy();
    objects.clear();
}

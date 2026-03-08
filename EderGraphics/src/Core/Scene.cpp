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
    objects.push_back({ &mesh, &material, glm::mat4(1.0f), 0 });
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
        if (!obj.subMeshMaterials.empty()) continue;  // skip multi-material — drawn per-submesh below
        groups[{ obj.mesh, obj.material }].push_back(obj.worldMatrix);
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

    // ── Per-submesh multi-material objects ──────────────────────────────────────
    // One matrix per submesh (not per object) so each submesh can have a local transform.
    {
        std::vector<SceneObject*> mmObjs;
        for (auto& obj : objects)
        {
            if (!obj.mesh || obj.subMeshMaterials.empty()) continue;
            if (obj.isSkinned) continue;
            mmObjs.push_back(&obj);
        }
        if (!mmObjs.empty())
        {
            struct DrawEntry { SceneObject* obj; uint32_t si; Material* mat; };
            std::vector<glm::mat4> smMats;
            std::vector<DrawEntry> drawList;

            for (auto* obj : mmObjs)
            {
                uint32_t smCount  = obj->mesh->GetSubmeshCount();
                uint32_t matCount = static_cast<uint32_t>(obj->subMeshMaterials.size());
                uint32_t tCount   = static_cast<uint32_t>(obj->subMeshLocalTransforms.size());
                for (uint32_t si = 0; si < smCount; si++)
                {
                    Material* mat = (si < matCount && obj->subMeshMaterials[si])
                                  ? obj->subMeshMaterials[si]
                                  : obj->material;
                    glm::mat4 local = (si < tCount) ? obj->subMeshLocalTransforms[si] : glm::mat4(1.0f);
                    smMats.push_back(obj->worldMatrix * local);
                    drawList.push_back({obj, si, mat});
                }
            }

            subMeshInstanceBuffer.Upload(smMats);
            subMeshInstanceBuffer.Bind(cmd);

            for (uint32_t i = 0; i < static_cast<uint32_t>(drawList.size()); i++)
            {
                auto& e = drawList[i];
                if (!e.mat || e.mat->IsTransparent()) continue;
                e.mat->Bind(cmd, layout);
                cmd.pushConstants(layout, vk::ShaderStageFlagBits::eVertex, 0, sizeof(glm::mat4), &viewProj);
                e.obj->mesh->DrawSubMesh(cmd, e.si, i, 1);
            }
        }
    }
}

void Scene::DrawSkinned(vk::CommandBuffer cmd, VulkanPipeline& pipeline, const Camera& camera, float aspect, LightBuffer& lights,
                        const std::function<void(uint32_t)>& bindBonesFn)
{
    glm::mat4 viewProj        = camera.GetProjection(aspect) * camera.GetView();
    vk::PipelineLayout layout = *pipeline.GetLayout();

    // Collect skinned objects and upload all transforms at once so each draw
    // uses a stable slot in the buffer (avoids GPU aliasing when drawing sequentially).
    std::vector<SceneObject*> skinnedObjs;
    for (auto& obj : objects)
    {
        if (!obj.mesh || !obj.material || !obj.isSkinned) continue;
        if (obj.material->IsTransparent()) continue;
        skinnedObjs.push_back(&obj);
    }
    if (skinnedObjs.empty()) return;

    std::vector<glm::mat4> transforms;
    transforms.reserve(skinnedObjs.size());
    for (auto* obj : skinnedObjs) transforms.push_back(obj->worldMatrix);
    skinnedInstanceBuffer.Upload(transforms);
    skinnedInstanceBuffer.Bind(cmd);

    lights.Bind(cmd, layout);

    for (uint32_t i = 0; i < static_cast<uint32_t>(skinnedObjs.size()); ++i)
    {
        auto* obj = skinnedObjs[i];
        bindBonesFn(obj->entityId);
        obj->material->Bind(cmd, layout);
        cmd.pushConstants(layout, vk::ShaderStageFlagBits::eVertex, 0, sizeof(glm::mat4), &viewProj);
        obj->mesh->DrawInstanced(cmd, i, 1);  // i = first instance index in the batch
    }
}

void Scene::DrawTransparent(vk::CommandBuffer cmd, VulkanPipeline& pipeline, const Camera& camera, float aspect, LightBuffer& lights)
{
    glm::mat4  viewProj = camera.GetProjection(aspect) * camera.GetView();
    glm::vec3  camPos   = camera.GetPosition();
    vk::PipelineLayout layout = *pipeline.GetLayout();

    // Collect + sort back-to-front
    struct Entry { VulkanMesh* mesh; Material* mat; glm::mat4 model; float depth; uint32_t submesh; };
    std::vector<Entry> entries;
    for (auto& obj : objects)
    {
        if (!obj.mesh) continue;
        glm::mat4 m = obj.worldMatrix;
        float     d = glm::length(glm::vec3(m[3]) - camPos);

        if (!obj.subMeshMaterials.empty())
        {
            // Multi-material: add one entry per transparent submesh
            uint32_t smCount  = obj.mesh->GetSubmeshCount();
            uint32_t matCount = static_cast<uint32_t>(obj.subMeshMaterials.size());
            uint32_t tCount   = static_cast<uint32_t>(obj.subMeshLocalTransforms.size());
            for (uint32_t si = 0; si < smCount; si++)
            {
                Material* mat = (si < matCount && obj.subMeshMaterials[si])
                              ? obj.subMeshMaterials[si]
                              : obj.material;
                if (!mat || !mat->IsTransparent()) continue;
                glm::mat4 local = (si < tCount) ? obj.subMeshLocalTransforms[si] : glm::mat4(1.0f);
                entries.push_back({ obj.mesh, mat, obj.worldMatrix * local, d, si });
            }
        }
        else
        {
            if (!obj.material || !obj.material->IsTransparent()) continue;
            entries.push_back({ obj.mesh, obj.material, m, d, UINT32_MAX });
        }
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
        if (entries[i].submesh == UINT32_MAX)
            entries[i].mesh->DrawInstanced(cmd, i, 1);
        else
            entries[i].mesh->DrawSubMesh(cmd, entries[i].submesh, i, 1);
    }
}

void Scene::DrawShadow(vk::CommandBuffer cmd, VulkanShadowPipeline& shadowPipeline, const glm::mat4& lightViewProj)
{
    vk::PipelineLayout layout = *shadowPipeline.GetLayout();
    cmd.pushConstants(layout, vk::ShaderStageFlagBits::eVertex, 0, sizeof(glm::mat4), &lightViewProj);

    // --- Opaque single-material objects (instanced) ---
    std::map<VulkanMesh*, std::vector<glm::mat4>> groups;
    for (auto& obj : objects)
    {
        if (!obj.mesh || !obj.material) continue;
        if (obj.material->IsTransparent()) continue;        // alpha-blend casts no shadow
        if (!obj.subMeshMaterials.empty()) continue;        // handled below per-submesh
        groups[obj.mesh].push_back(obj.worldMatrix);
    }

    if (!groups.empty())
    {
        std::vector<glm::mat4> allMatrices;
        for (auto& [mesh, matrices] : groups)
            for (auto& m : matrices)
                allMatrices.push_back(m);

        shadowInstanceBuffer.Upload(allMatrices);
        shadowInstanceBuffer.Bind(cmd);

        uint32_t first = 0;
        for (auto& [mesh, matrices] : groups)
        {
            mesh->DrawInstanced(cmd, first, static_cast<uint32_t>(matrices.size()));
            first += static_cast<uint32_t>(matrices.size());
        }
    }

    // --- Multi-material objects: draw only opaque submeshes (separate buffer from main pass) ---
    {
        std::vector<SceneObject*> mmObjs;
        for (auto& obj : objects)
        {
            if (!obj.mesh || obj.subMeshMaterials.empty()) continue;
            mmObjs.push_back(&obj);
        }

        if (!mmObjs.empty())
        {
            struct ShadowEntry { SceneObject* obj; uint32_t si; bool opaque; };
            std::vector<glm::mat4> smMats;
            std::vector<ShadowEntry> drawList;

            for (auto* obj : mmObjs)
            {
                uint32_t smCount  = obj->mesh->GetSubmeshCount();
                uint32_t matCount = static_cast<uint32_t>(obj->subMeshMaterials.size());
                uint32_t tCount   = static_cast<uint32_t>(obj->subMeshLocalTransforms.size());
                for (uint32_t si = 0; si < smCount; si++)
                {
                    Material* mat = (si < matCount && obj->subMeshMaterials[si])
                                  ? obj->subMeshMaterials[si]
                                  : obj->material;
                    glm::mat4 local = (si < tCount) ? obj->subMeshLocalTransforms[si] : glm::mat4(1.0f);
                    smMats.push_back(obj->worldMatrix * local);
                    drawList.push_back({obj, si, mat && !mat->IsTransparent()});
                }
            }

            shadowSubMeshInstanceBuffer.Upload(smMats);
            shadowSubMeshInstanceBuffer.Bind(cmd);

            for (uint32_t i = 0; i < static_cast<uint32_t>(drawList.size()); i++)
            {
                if (!drawList[i].opaque) continue;
                drawList[i].obj->mesh->DrawSubMesh(cmd, drawList[i].si, i, 1);
            }
        }
    }
}

void Scene::DrawShadowPoint(vk::CommandBuffer cmd, VulkanPointShadowPipeline& pipeline,
                            const glm::mat4& lightViewProj, const glm::vec3& lightPos, float farPlane)
{
    vk::PipelineLayout layout = pipeline.GetLayout();

    struct PushData { glm::mat4 viewProj; glm::vec4 lightPosAndFar; } push;
    push.viewProj       = lightViewProj;
    push.lightPosAndFar = glm::vec4(lightPos, farPlane);

    cmd.pushConstants(layout,
        vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        0, static_cast<uint32_t>(sizeof(PushData)), &push);

    // --- Opaque single-material objects (instanced) ---
    std::map<VulkanMesh*, std::vector<glm::mat4>> groups;
    for (auto& obj : objects)
    {
        if (!obj.mesh || !obj.material) continue;
        if (obj.material->IsTransparent()) continue;
        if (!obj.subMeshMaterials.empty()) continue;
        groups[obj.mesh].push_back(obj.worldMatrix);
    }

    if (!groups.empty())
    {
        std::vector<glm::mat4> allMatrices;
        for (auto& [mesh, matrices] : groups)
            for (auto& m : matrices)
                allMatrices.push_back(m);

        shadowInstanceBuffer.Upload(allMatrices);
        shadowInstanceBuffer.Bind(cmd);

        uint32_t first = 0;
        for (auto& [mesh, matrices] : groups)
        {
            mesh->DrawInstanced(cmd, first, static_cast<uint32_t>(matrices.size()));
            first += static_cast<uint32_t>(matrices.size());
        }
    }

    // --- Multi-material objects (point/spot shadow — dedicated buffer) ---
    {
        std::vector<SceneObject*> mmObjs;
        for (auto& obj : objects)
        {
            if (!obj.mesh || obj.subMeshMaterials.empty()) continue;
            mmObjs.push_back(&obj);
        }

        if (!mmObjs.empty())
        {
            struct ShadowEntry { SceneObject* obj; uint32_t si; bool opaque; };
            std::vector<glm::mat4> smMats;
            std::vector<ShadowEntry> drawList;

            for (auto* obj : mmObjs)
            {
                uint32_t smCount  = obj->mesh->GetSubmeshCount();
                uint32_t matCount = static_cast<uint32_t>(obj->subMeshMaterials.size());
                uint32_t tCount   = static_cast<uint32_t>(obj->subMeshLocalTransforms.size());
                for (uint32_t si = 0; si < smCount; si++)
                {
                    Material* mat = (si < matCount && obj->subMeshMaterials[si])
                                  ? obj->subMeshMaterials[si]
                                  : obj->material;
                    glm::mat4 local = (si < tCount) ? obj->subMeshLocalTransforms[si] : glm::mat4(1.0f);
                    smMats.push_back(obj->worldMatrix * local);
                    drawList.push_back({obj, si, mat && !mat->IsTransparent()});
                }
            }

            pointShadowSubMeshInstanceBuffer.Upload(smMats);
            pointShadowSubMeshInstanceBuffer.Bind(cmd);

            for (uint32_t i = 0; i < static_cast<uint32_t>(drawList.size()); i++)
            {
                if (!drawList[i].opaque) continue;
                drawList[i].obj->mesh->DrawSubMesh(cmd, drawList[i].si, i, 1);
            }
        }
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
    skinnedInstanceBuffer.Destroy();
    subMeshInstanceBuffer.Destroy();
    shadowSubMeshInstanceBuffer.Destroy();
    pointShadowSubMeshInstanceBuffer.Destroy();
    objects.clear();
}

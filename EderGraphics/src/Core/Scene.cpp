#include "Scene.h"
#include "Camera.h"
#include "Material.h"
#include "LightBuffer.h"
#include "Renderer/Vulkan/VulkanMesh.h"
#include "Renderer/Vulkan/VulkanPipeline.h"
#include "Renderer/Vulkan/VulkanShadowPipeline.h"
#include "Renderer/Vulkan/VulkanPointShadowPipeline.h"
#include "Renderer/Vulkan/VulkanShadowPipeline.h"
#include "Renderer/Vulkan/BoneSSBO.h"
#include <glm/glm.hpp>
#include <algorithm>

namespace
{
    // Conservative world-space bounding sphere of an object, from its mesh's
    // local AABB transformed by the world matrix. If the mesh has no valid
    // bounds (empty geometry) the radius is left huge so the object is never
    // culled — the safe failure mode for a shadow caster.
    inline void ObjectWorldSphere(const SceneObject& obj, glm::vec3& outCenter, float& outRadius)
    {
        glm::vec3 mn = obj.mesh->GetBoundsMin();
        glm::vec3 mx = obj.mesh->GetBoundsMax();
        if (mn.x > mx.x) { outCenter = glm::vec3(obj.worldMatrix[3]); outRadius = 1e30f; return; }

        glm::vec3 localCenter = 0.5f * (mn + mx);
        glm::vec3 localExtent = 0.5f * (mx - mn);
        outCenter = glm::vec3(obj.worldMatrix * glm::vec4(localCenter, 1.0f));

        // Largest scale across the three basis columns → conservative radius.
        float sx = glm::length(glm::vec3(obj.worldMatrix[0]));
        float sy = glm::length(glm::vec3(obj.worldMatrix[1]));
        float sz = glm::length(glm::vec3(obj.worldMatrix[2]));
        float maxScale = std::max(sx, std::max(sy, sz));
        outRadius = glm::length(localExtent) * maxScale;
    }

    // Sphere-vs-cull-sphere test. cull == nullptr means "keep everything".
    inline bool CasterVisible(const SceneObject& obj, const glm::vec4* cull)
    {
        if (!cull) return true;
        glm::vec3 c; float r;
        ObjectWorldSphere(obj, c, r);
        float maxDist = cull->w + r;
        glm::vec3 d = c - glm::vec3(*cull);
        return glm::dot(d, d) <= maxDist * maxDist;
    }
}

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

void Scene::DrawShadow(vk::CommandBuffer cmd, VulkanShadowPipeline& shadowPipeline, const glm::mat4& lightViewProj,
                       const glm::vec4* cullSphere, BoneSSBO* fallbackBones)
{
    vk::PipelineLayout layout = *shadowPipeline.GetLayout();
    cmd.pushConstants(layout, vk::ShaderStageFlagBits::eVertex, 0, sizeof(glm::mat4), &lightViewProj);

    // --- Opaque single-material, non-skinned objects (instanced) ---
    // Collect into a scratch list, sort by mesh so identical meshes are
    // contiguous, then issue one instanced draw per run. No per-frame allocs.
    m_scratchGroups.clear();
    for (auto& obj : objects)
    {
        if (!obj.mesh || !obj.material) continue;
        if (obj.isSkinned) continue;                        // skinned drawn separately via DrawSkinnedShadow
        if (obj.material->IsTransparent()) continue;        // alpha-blend casts no shadow
        if (obj.material->IsAlphaTested()) continue;        // texture-shaped shadow pass below
        if (!obj.subMeshMaterials.empty()) continue;        // handled below per-submesh
        if (!CasterVisible(obj, cullSphere)) continue;
        m_scratchGroups.push_back({ obj.mesh, obj.worldMatrix });
    }

    if (!m_scratchGroups.empty())
    {
        std::sort(m_scratchGroups.begin(), m_scratchGroups.end(),
                  [](const ShadowGroupEntry& a, const ShadowGroupEntry& b){ return a.mesh < b.mesh; });

        m_scratchMatrices.clear();
        for (auto& g : m_scratchGroups) m_scratchMatrices.push_back(g.matrix);

        shadowInstanceBuffer.Upload(m_scratchMatrices);
        shadowInstanceBuffer.Bind(cmd);

        uint32_t first = 0;
        while (first < m_scratchGroups.size())
        {
            VulkanMesh* mesh = m_scratchGroups[first].mesh;
            uint32_t count = 1;
            while (first + count < m_scratchGroups.size() &&
                   m_scratchGroups[first + count].mesh == mesh)
                ++count;
            mesh->DrawInstanced(cmd, first, count);
            first += count;
        }
    }

    // --- Multi-material objects: draw only opaque, non-skinned submeshes ---
    {
        m_scratchSmMats.clear();
        m_scratchSmDraw.clear();

        for (auto& obj : objects)
        {
            if (!obj.mesh || obj.subMeshMaterials.empty()) continue;
            if (obj.isSkinned) continue;                        // skinned drawn via DrawSkinnedShadow
            if (!CasterVisible(obj, cullSphere)) continue;

            uint32_t smCount  = obj.mesh->GetSubmeshCount();
            uint32_t matCount = static_cast<uint32_t>(obj.subMeshMaterials.size());
            uint32_t tCount   = static_cast<uint32_t>(obj.subMeshLocalTransforms.size());
            for (uint32_t si = 0; si < smCount; si++)
            {
                Material* mat = (si < matCount && obj.subMeshMaterials[si])
                              ? obj.subMeshMaterials[si]
                              : obj.material;
                glm::mat4 local = (si < tCount) ? obj.subMeshLocalTransforms[si] : glm::mat4(1.0f);
                m_scratchSmMats.push_back(obj.worldMatrix * local);
                m_scratchSmDraw.push_back({ &obj, si, mat && !mat->IsTransparent() });
            }
        }

        if (!m_scratchSmDraw.empty())
        {
            shadowSubMeshInstanceBuffer.Upload(m_scratchSmMats);
            shadowSubMeshInstanceBuffer.Bind(cmd);

            for (uint32_t i = 0; i < static_cast<uint32_t>(m_scratchSmDraw.size()); i++)
            {
                if (!m_scratchSmDraw[i].opaque) continue;
                m_scratchSmDraw[i].obj->mesh->DrawSubMesh(cmd, m_scratchSmDraw[i].si, i, 1);
            }
        }
    }

    // --- Alpha-tested casters: dedicated pipeline that samples the albedo and
    //     discards transparent texels, so foliage/fences shadow their texture
    //     shape rather than the full triangle silhouette. ---
    if (shadowPipeline.HasAlpha() && fallbackBones)
    {
        m_scratchGroups.clear();          // reuse as (mesh, matrix) list
        for (auto& obj : objects)
        {
            if (!obj.mesh || !obj.material) continue;
            if (obj.isSkinned) continue;
            if (!obj.subMeshMaterials.empty()) continue;
            if (!obj.material->IsAlphaTested()) continue;
            if (!CasterVisible(obj, cullSphere)) continue;
            m_scratchGroups.push_back({ obj.mesh, obj.worldMatrix });
        }

        if (!m_scratchGroups.empty())
        {
            m_scratchMatrices.clear();
            for (auto& g : m_scratchGroups) m_scratchMatrices.push_back(g.matrix);
            alphaShadowInstanceBuffer.Upload(m_scratchMatrices);
            alphaShadowInstanceBuffer.Bind(cmd);

            shadowPipeline.BindAlpha(cmd);
            vk::PipelineLayout aLayout = *shadowPipeline.GetAlphaLayout();
            cmd.pushConstants(aLayout, vk::ShaderStageFlagBits::eVertex, 0, sizeof(glm::mat4), &lightViewProj);
            fallbackBones->BindToSet(cmd, aLayout, 1);   // set 1 = bones

            // Walk objects in the same order the instance buffer was filled,
            // binding each caster's material (set 0) before its instanced draw.
            uint32_t inst = 0;
            for (auto& obj : objects)
            {
                if (!obj.mesh || !obj.material) continue;
                if (obj.isSkinned || !obj.subMeshMaterials.empty()) continue;
                if (!obj.material->IsAlphaTested()) continue;
                if (!CasterVisible(obj, cullSphere)) continue;
                obj.material->Bind(cmd, aLayout);        // set 0 = material
                obj.mesh->DrawInstanced(cmd, inst, 1);
                ++inst;
            }

            // Restore the plain depth-only pipeline for subsequent draws (the
            // caller may still draw skinned casters into the same pass).
            shadowPipeline.Bind(cmd);
            cmd.pushConstants(layout, vk::ShaderStageFlagBits::eVertex, 0, sizeof(glm::mat4), &lightViewProj);
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

    // A point light only lights within `farPlane` of its position, so cull any
    // caster whose world sphere doesn't reach the light's range sphere.
    const glm::vec4 rangeSphere(lightPos, farPlane);

    // --- Opaque single-material, non-skinned objects (instanced) ---
    m_scratchGroups.clear();
    for (auto& obj : objects)
    {
        if (!obj.mesh || !obj.material) continue;
        if (obj.isSkinned) continue;                    // skinned drawn via DrawSkinnedShadowPoint
        if (obj.material->IsTransparent()) continue;
        if (!obj.subMeshMaterials.empty()) continue;
        if (!CasterVisible(obj, &rangeSphere)) continue;
        m_scratchGroups.push_back({ obj.mesh, obj.worldMatrix });
    }

    if (!m_scratchGroups.empty())
    {
        std::sort(m_scratchGroups.begin(), m_scratchGroups.end(),
                  [](const ShadowGroupEntry& a, const ShadowGroupEntry& b){ return a.mesh < b.mesh; });

        m_scratchMatrices.clear();
        for (auto& g : m_scratchGroups) m_scratchMatrices.push_back(g.matrix);

        shadowInstanceBuffer.Upload(m_scratchMatrices);
        shadowInstanceBuffer.Bind(cmd);

        uint32_t first = 0;
        while (first < m_scratchGroups.size())
        {
            VulkanMesh* mesh = m_scratchGroups[first].mesh;
            uint32_t count = 1;
            while (first + count < m_scratchGroups.size() &&
                   m_scratchGroups[first + count].mesh == mesh)
                ++count;
            mesh->DrawInstanced(cmd, first, count);
            first += count;
        }
    }

    // --- Multi-material objects (point/spot shadow — dedicated buffer) ---
    {
        m_scratchSmMats.clear();
        m_scratchSmDraw.clear();

        for (auto& obj : objects)
        {
            if (!obj.mesh || obj.subMeshMaterials.empty()) continue;
            if (obj.isSkinned) continue;                        // skinned drawn via DrawSkinnedShadowPoint
            if (!CasterVisible(obj, &rangeSphere)) continue;

            uint32_t smCount  = obj.mesh->GetSubmeshCount();
            uint32_t matCount = static_cast<uint32_t>(obj.subMeshMaterials.size());
            uint32_t tCount   = static_cast<uint32_t>(obj.subMeshLocalTransforms.size());
            for (uint32_t si = 0; si < smCount; si++)
            {
                Material* mat = (si < matCount && obj.subMeshMaterials[si])
                              ? obj.subMeshMaterials[si]
                              : obj.material;
                glm::mat4 local = (si < tCount) ? obj.subMeshLocalTransforms[si] : glm::mat4(1.0f);
                m_scratchSmMats.push_back(obj.worldMatrix * local);
                m_scratchSmDraw.push_back({ &obj, si, mat && !mat->IsTransparent() });
            }
        }

        if (!m_scratchSmDraw.empty())
        {
            pointShadowSubMeshInstanceBuffer.Upload(m_scratchSmMats);
            pointShadowSubMeshInstanceBuffer.Bind(cmd);

            for (uint32_t i = 0; i < static_cast<uint32_t>(m_scratchSmDraw.size()); i++)
            {
                if (!m_scratchSmDraw[i].opaque) continue;
                m_scratchSmDraw[i].obj->mesh->DrawSubMesh(cmd, m_scratchSmDraw[i].si, i, 1);
            }
        }
    }
}

void Scene::DrawSkinnedShadow(vk::CommandBuffer cmd, VulkanShadowPipeline& shadowPipeline,
                               const glm::mat4& lightViewProj,
                               const std::function<void(uint32_t)>& bindBonesFn,
                               const glm::vec4* /*cullSphere*/)
{
    // NOTE: skinned casters are not frustum-culled — their bind-pose AABB does
    // not bound the animated pose, so culling could clip valid shadows.
    vk::PipelineLayout layout = *shadowPipeline.GetLayout();
    cmd.pushConstants(layout, vk::ShaderStageFlagBits::eVertex, 0, sizeof(glm::mat4), &lightViewProj);

    std::vector<SceneObject*> skinnedObjs;
    for (auto& obj : objects)
    {
        if (!obj.mesh || !obj.isSkinned) continue;
        if (obj.material && obj.material->IsTransparent()) continue;
        skinnedObjs.push_back(&obj);
    }
    if (skinnedObjs.empty()) return;

    std::vector<glm::mat4> transforms;
    transforms.reserve(skinnedObjs.size());
    for (auto* obj : skinnedObjs) transforms.push_back(obj->worldMatrix);
    skinnedShadowInstanceBuffer.Upload(transforms);
    skinnedShadowInstanceBuffer.Bind(cmd);

    for (uint32_t i = 0; i < static_cast<uint32_t>(skinnedObjs.size()); ++i)
    {
        bindBonesFn(skinnedObjs[i]->entityId);
        skinnedObjs[i]->mesh->DrawInstanced(cmd, i, 1);
    }
}

void Scene::DrawSkinnedShadowPoint(vk::CommandBuffer cmd, VulkanPointShadowPipeline& pipeline,
                                    const glm::mat4& lightViewProj, const glm::vec3& lightPos, float farPlane,
                                    const std::function<void(uint32_t)>& bindBonesFn)
{
    vk::PipelineLayout layout = pipeline.GetLayout();

    struct PushData { glm::mat4 viewProj; glm::vec4 lightPosAndFar; } push;
    push.viewProj       = lightViewProj;
    push.lightPosAndFar = glm::vec4(lightPos, farPlane);
    cmd.pushConstants(layout,
        vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        0, static_cast<uint32_t>(sizeof(PushData)), &push);

    std::vector<SceneObject*> skinnedObjs;
    for (auto& obj : objects)
    {
        if (!obj.mesh || !obj.isSkinned) continue;
        if (obj.material && obj.material->IsTransparent()) continue;
        skinnedObjs.push_back(&obj);
    }
    if (skinnedObjs.empty()) return;

    std::vector<glm::mat4> transforms;
    transforms.reserve(skinnedObjs.size());
    for (auto* obj : skinnedObjs) transforms.push_back(obj->worldMatrix);
    skinnedShadowInstanceBuffer.Upload(transforms);
    skinnedShadowInstanceBuffer.Bind(cmd);

    for (uint32_t i = 0; i < static_cast<uint32_t>(skinnedObjs.size()); ++i)
    {
        bindBonesFn(skinnedObjs[i]->entityId);
        skinnedObjs[i]->mesh->DrawInstanced(cmd, i, 1);
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
    skinnedShadowInstanceBuffer.Destroy();
    subMeshInstanceBuffer.Destroy();
    shadowSubMeshInstanceBuffer.Destroy();
    pointShadowSubMeshInstanceBuffer.Destroy();
    alphaShadowInstanceBuffer.Destroy();
    objects.clear();
}

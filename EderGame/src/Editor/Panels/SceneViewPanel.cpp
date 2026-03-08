#include "SceneViewPanel.h"
#include <imgui/imgui_impl_vulkan.h>
#include <imgui/imgui_internal.h>
#include <imgui/ImGuizmo.h>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include "ECS/Components/TransformComponent.h"
#include "ECS/Components/HierarchyComponent.h"
#include "ECS/Components/MeshRendererComponent.h"
#include "ECS/Systems/TransformSystem.h"
#include "Core/MeshManager.h"
#include "Renderer/Vulkan/VulkanMesh.h"
#include <cmath>
#include <cfloat>
#include <algorithm>

void SceneViewPanel::SetFramebuffer(VulkanFramebuffer* fb)
{
    VkImageView newView = fb ? (VkImageView)fb->GetColorView() : VK_NULL_HANDLE;
    // Skip re-registration when nothing actually changed (same fb, same backing image)
    if (fb == framebuffer && newView == lastView && texDS != VK_NULL_HANDLE) return;

    ReleaseTexture();
    framebuffer = fb;
    lastView    = newView;
    if (!fb) return;

    texDS = ImGui_ImplVulkan_AddTexture(
        (VkSampler)    fb->GetSampler(),
        (VkImageView)  fb->GetColorView(),
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    );
}

void SceneViewPanel::ReleaseTexture()
{
    if (texDS != VK_NULL_HANDLE)
    {
        ImGui_ImplVulkan_RemoveTexture(texDS);
        texDS = VK_NULL_HANDLE;
    }
    framebuffer = nullptr;
    lastView    = VK_NULL_HANDLE;
}

void SceneViewPanel::OnDraw() { OnDraw(GizmoMode::Translate, false, 1.0f, glm::mat4(1), glm::mat4(1), nullptr, 0); }

void SceneViewPanel::OnDraw(GizmoMode gizmoMode, bool snap, float snapValue,
                             const glm::mat4& view, const glm::mat4& proj,
                             Registry* registry, Entity selected)
{
    // Cache matrices for ray picking
    m_lastView = view;
    m_lastProj = proj;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoScrollbar      |
        ImGuiWindowFlags_NoScrollWithMouse;

    ImGui::SetNextWindowSize(ImVec2(800, 500), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(Title(), &open, flags))
    {
        ImGui::PopStyleVar();
        ImGui::End();
        return;
    }

    ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x > 4 && avail.y > 4)
        desiredSize = avail;

    if (texDS != VK_NULL_HANDLE && avail.x > 0 && avail.y > 0)
    {
        ImGui::Image(ImTextureRef((ImTextureID)(uint64_t)texDS), avail);
        // Store screen-space position/size so the editor can overlay/embed
        // the EderPlayer preview window on top of this exact rectangle.
        contentScreenPos  = ImGui::GetItemRectMin();
        contentScreenSize = ImGui::GetItemRectSize();

        // ── Click-to-select picking ──────────────────────────────────────────
        if (registry &&
            ImGui::IsItemHovered() &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            !ImGuizmo::IsOver())
        {
            ImVec2 mouse = ImGui::GetMousePos();
            float  vpW   = contentScreenSize.x;
            float  vpH   = contentScreenSize.y;
            if (vpW > 0.0f && vpH > 0.0f)
            {
                // Screen → NDC  (-1..+1 each axis)
                float ndcX =  (mouse.x - contentScreenPos.x) / vpW * 2.0f - 1.0f;
                float ndcY =  (mouse.y - contentScreenPos.y) / vpH * 2.0f - 1.0f;

                // Unproject: clip → eye → world
                glm::mat4 invProj = glm::inverse(m_lastProj);
                glm::mat4 invView = glm::inverse(m_lastView);

                glm::vec4 rayEye = invProj * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
                rayEye = glm::vec4(rayEye.x, rayEye.y, -1.0f, 0.0f);  // direction, not position

                glm::vec3 rayDir  = glm::normalize(glm::vec3(invView * rayEye));
                glm::vec3 rayOrig = glm::vec3(invView[3]);

                m_pickedEntity = DoRayPick(*registry, rayOrig, rayDir);
                m_hasPick = true;
            }
        }
    }
    else
    {
        ImGui::SetCursorPos(ImVec2(avail.x * 0.5f - 60, avail.y * 0.5f));
        ImGui::TextDisabled("Scene not available");
    }

    // ── ImGuizmo — transform manipulator ────────────────────────────────────
    if (registry && selected != NULL_ENTITY && registry->Has<TransformComponent>(selected))
    {
        auto& tr = registry->Get<TransformComponent>(selected);

        // ImGuizmo expects an OpenGL-style projection (Y up, depth -1..1).
        // GetProjection() already flips Y for Vulkan, so we un-flip it here.
        glm::mat4 imguizmoProj  = proj;
        imguizmoProj[1][1] *= -1.0f;

        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());

        // Rect = the image area inside the window
        ImVec2 imageMin  = ImGui::GetItemRectMin();
        ImVec2 imageSize = ImGui::GetItemRectSize();
        ImGuizmo::SetRect(imageMin.x, imageMin.y, imageSize.x, imageSize.y);

        // Map GizmoMode → ImGuizmo operation
        ImGuizmo::OPERATION op;
        switch (gizmoMode)
        {
        case GizmoMode::Rotate: op = ImGuizmo::ROTATE; break;
        case GizmoMode::Scale:  op = ImGuizmo::SCALE;  break;
        default:                op = ImGuizmo::TRANSLATE; break;
        }

        // Scale is only meaningful in local space
        ImGuizmo::MODE coordMode = (gizmoMode == GizmoMode::Scale)
            ? ImGuizmo::LOCAL : ImGuizmo::WORLD;

        float snapVals[3] = { snapValue, snapValue, snapValue };
        float* pSnap      = snap ? snapVals : nullptr;

        float viewF[16], projF[16], worldF[16];
        memcpy(viewF,  glm::value_ptr(view),         sizeof(float) * 16);
        memcpy(projF,  glm::value_ptr(imguizmoProj), sizeof(float) * 16);

        // Use WORLD matrix so the gizmo positions correctly for nested entities
        glm::mat4 worldMat = TransformSystem::GetWorldMatrix(selected, *registry);
        memcpy(worldF, glm::value_ptr(worldMat), sizeof(float) * 16);

        if (ImGuizmo::Manipulate(viewF, projF, op, coordMode, worldF, nullptr, pSnap))
        {
            // Convert manipulated world matrix back to LOCAL space
            Entity parent = registry->Has<HierarchyComponent>(selected)
                ? registry->Get<HierarchyComponent>(selected).parent
                : NULL_ENTITY;

            glm::mat4 newWorld(1.0f);
            memcpy(glm::value_ptr(newWorld), worldF, sizeof(float) * 16);

            glm::mat4 parentWorld = (parent != NULL_ENTITY)
                ? TransformSystem::GetWorldMatrix(parent, *registry)
                : glm::mat4(1.0f);

            glm::mat4 newLocal = glm::inverse(parentWorld) * newWorld;
            TransformSystem::DecomposeInto(newLocal, tr);
        }
    }
    ImGui::PopStyleVar();
    ImGui::End();
}

// ─── Ray picking ──────────────────────────────────────────────────────────────
// Slab-method ray-AABB test in local space.
// Returns true and sets tHit to the entry distance along the ray (>= 0).
static bool RayAABB(const glm::vec3& orig, const glm::vec3& dir,
                    const glm::vec3& bmin, const glm::vec3& bmax, float& tHit)
{
    float tmin = 0.0f, tmax = FLT_MAX;
    for (int i = 0; i < 3; i++)
    {
        if (std::abs(dir[i]) < 1e-7f)
        {
            if (orig[i] < bmin[i] || orig[i] > bmax[i]) return false;
        }
        else
        {
            float t1 = (bmin[i] - orig[i]) / dir[i];
            float t2 = (bmax[i] - orig[i]) / dir[i];
            if (t1 > t2) std::swap(t1, t2);
            tmin = std::max(tmin, t1);
            tmax = std::min(tmax, t2);
            if (tmin > tmax) return false;
        }
    }
    tHit = tmin;
    return true;
}

Entity SceneViewPanel::DoRayPick(Registry& registry,
                                  const glm::vec3& rayOrig,
                                  const glm::vec3& rayDir) const
{
    Entity closest = NULL_ENTITY;
    float  minDist = FLT_MAX;

    registry.Each<TransformComponent>([&](Entity e, TransformComponent& /*tr*/)
    {
        // World matrix of this entity
        glm::mat4 world    = TransformSystem::GetWorldMatrix(e, registry);
        glm::mat4 invWorld = glm::inverse(world);

        // Transform ray into local (object) space
        glm::vec3 localOrig = glm::vec3(invWorld * glm::vec4(rayOrig, 1.0f));
        glm::vec3 localDir  = glm::vec3(invWorld * glm::vec4(rayDir,  0.0f));
        // Normalize the local direction — non-uniform scale changes its length
        float localDirLen = glm::length(localDir);
        if (localDirLen < 1e-7f) return;
        localDir /= localDirLen;

        // ── AABB from mesh, or fall back to a unit-cube bounding box ─────────
        glm::vec3 bmin(-0.5f), bmax(0.5f);   // unit-cube fallback

        if (registry.Has<MeshRendererComponent>(e))
        {
            const auto& mr = registry.Get<MeshRendererComponent>(e);
            const std::string& path = mr.meshPath;
            if (!path.empty() && MeshManager::Get().Has(path))
            {
                VulkanMesh& mesh = MeshManager::Get().Load(path);
                bmin = mesh.GetBoundsMin();
                bmax = mesh.GetBoundsMax();
            }
        }

        // ── Ray-OBB slab test ────────────────────────────────────────────────
        float tLocal = 0.0f;
        if (!RayAABB(localOrig, localDir, bmin, bmax, tLocal)) return;

        // Convert the local-space hit point back to world space for a
        // world-space distance comparison that is scale-invariant.
        glm::vec3 localHit = localOrig + tLocal * localDir;
        glm::vec3 worldHit = glm::vec3(world * glm::vec4(localHit, 1.0f));
        float worldDist    = glm::length(worldHit - rayOrig);

        if (worldDist < minDist)
        {
            minDist = worldDist;
            closest = e;
        }
    });

    return closest;
}

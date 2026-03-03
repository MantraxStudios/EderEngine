#include "SceneViewPanel.h"
#include <imgui/imgui_impl_vulkan.h>
#include <imgui/imgui_internal.h>
#include <imgui/ImGuizmo.h>
#include <glm/gtc/type_ptr.hpp>
#include "ECS/Components/TransformComponent.h"
#include "ECS/Components/HierarchyComponent.h"
#include "ECS/Systems/TransformSystem.h"

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
        ImGui::Image(ImTextureRef((ImTextureID)(uint64_t)texDS), avail);
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

    // ── Gizmo mode overlay (top-left corner) ─────────────────────────────────
    const ImVec2 vpMin  = ImGui::GetWindowPos();
    const float  pad    = 8.0f;
    const float  btnSz  = 26.0f;
    ImVec2       cursor = ImVec2(vpMin.x + pad, vpMin.y + pad);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(
        ImVec2(cursor.x - 4, cursor.y - 3),
        ImVec2(cursor.x + btnSz * 4 + 4 + (snap ? btnSz + 2 : 0), cursor.y + btnSz + 3),
        IM_COL32(20, 20, 20, 180), 4.0f);

    auto GizmoBtn = [&](const char* label, GizmoMode mode, ImVec4 activeCol) -> bool
    {
        bool isActive = (gizmoMode == mode);
        ImGui::SetCursorScreenPos(cursor);
        ImGui::PushID(label);
        if (isActive) ImGui::PushStyleColor(ImGuiCol_Button, activeCol);
        bool clicked = ImGui::Button(label, ImVec2(btnSz, btnSz));
        if (isActive) ImGui::PopStyleColor();
        ImGui::PopID();
        cursor.x += btnSz + 2;
        return clicked;
    };

    (void)GizmoBtn("T", GizmoMode::Translate, ImVec4(0.85f, 0.25f, 0.25f, 1.0f));
    (void)GizmoBtn("R", GizmoMode::Rotate,    ImVec4(0.85f, 0.25f, 0.25f, 1.0f));
    (void)GizmoBtn("S", GizmoMode::Scale,     ImVec4(0.85f, 0.25f, 0.25f, 1.0f));

    // Snap indicator
    ImGui::SetCursorScreenPos(cursor);
    if (snap)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.25f, 0.25f, 1.0f));
        char snapLabel[16];
        snprintf(snapLabel, sizeof(snapLabel), "%.4g###snap", snapValue);
        ImGui::Button(snapLabel, ImVec2(btnSz + 8, btnSz));
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Snap ON: %.4g", snapValue);
    }

    ImGui::PopStyleVar();
    ImGui::End();
}

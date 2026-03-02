#include "SceneViewPanel.h"
#include <imgui/imgui_impl_vulkan.h>
#include <imgui/imgui_internal.h>

void SceneViewPanel::SetFramebuffer(VulkanFramebuffer* fb)
{
    ReleaseTexture();
    framebuffer = fb;
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
}

void SceneViewPanel::OnDraw() { OnDraw(GizmoMode::Translate, false, 1.0f); }

void SceneViewPanel::OnDraw(GizmoMode gizmoMode, bool snap, float snapValue)
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

    // ---- Gizmo overlay (top-left corner of the viewport) ----
    const ImVec2 vpMin  = ImGui::GetWindowPos();
    const float  pad    = 8.0f;
    const float  btnSz  = 26.0f;
    ImVec2       cursor = ImVec2(vpMin.x + pad, vpMin.y + pad);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    // semi-transparent background pill
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

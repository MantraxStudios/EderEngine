#include "SceneViewPanel.h"
#include <imgui/imgui_impl_vulkan.h>

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

void SceneViewPanel::OnDraw()
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse;

    ImGui::SetNextWindowSize(ImVec2(800, 500), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(Title(), &open, flags))
    {
        ImGui::PopStyleVar();
        ImGui::End();
        return;
    }

    ImVec2 avail = ImGui::GetContentRegionAvail();

    // Guardar tamaño para que main.cpp pueda redimensionar el framebuffer
    if (avail.x > 4 && avail.y > 4)
        desiredSize = avail;

    if (texDS != VK_NULL_HANDLE && avail.x > 0 && avail.y > 0)
    {
        // ImGui 1.92+: Image toma ImTextureRef, construible desde ImTextureID (ImU64)
        ImGui::Image(ImTextureRef((ImTextureID)(uint64_t)texDS), avail);
    }
    else
    {
        ImGui::SetCursorPos(ImVec2(avail.x * 0.5f - 60, avail.y * 0.5f));
        ImGui::TextDisabled("Scene not available");
    }

    ImGui::PopStyleVar();
    ImGui::End();
}

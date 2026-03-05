#pragma once
#include "Panel.h"
#include "Renderer/Vulkan/VulkanFramebuffer.h"
#include "../EditorTypes.h"
#include "ECS/Registry.h"
#include "ECS/Entity.h"
#include <vulkan/vulkan.h>
#include <imgui/imgui.h>
#include <glm/glm.hpp>

class SceneViewPanel : public Panel
{
public:
    ~SceneViewPanel() { ReleaseTexture(); }

    const char* Title() const override { return "Viewport"; }

    // Full draw with ImGuizmo support
    void OnDraw(GizmoMode gizmoMode, bool snap, float snapValue,
                const glm::mat4& view, const glm::mat4& proj,
                Registry* registry, Entity selected);
    void OnDraw() override;

    // Registra el framebuffer como textura de ImGui. Llamar después de (re)crear el framebuffer.
    void SetFramebuffer(VulkanFramebuffer* fb);

    // Elimina el registro de textura. Llamar ANTES de recrear el framebuffer.
    void ReleaseTexture();

    // Tamaño deseado por el panel (content region del frame anterior). {0,0} al inicio.
    ImVec2 GetDesiredSize() const { return desiredSize; }

    // Screen-space position and size of the rendered image (updated each Draw).
    // Use these to overlay/embed the EderPlayer preview window.
    ImVec2 GetContentScreenPos()  const { return contentScreenPos;  }
    ImVec2 GetContentScreenSize() const { return contentScreenSize; }

private:
    VulkanFramebuffer* framebuffer      = nullptr;
    VkDescriptorSet    texDS            = VK_NULL_HANDLE;
    VkImageView        lastView         = VK_NULL_HANDLE;
    ImVec2             desiredSize      = { 0.0f, 0.0f };
    ImVec2             contentScreenPos  = { 0.0f, 0.0f };
    ImVec2             contentScreenSize = { 0.0f, 0.0f };
};

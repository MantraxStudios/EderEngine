#pragma once
#include "Panel.h"
#include "Renderer/Vulkan/VulkanFramebuffer.h"
#include <vulkan/vulkan.h>
#include <imgui/imgui.h>

class SceneViewPanel : public Panel
{
public:
    ~SceneViewPanel() { ReleaseTexture(); }

    const char* Title() const override { return "Scene View"; }
    void        OnDraw()      override;

    // Registra el framebuffer como textura de ImGui. Llamar después de (re)crear el framebuffer.
    void SetFramebuffer(VulkanFramebuffer* fb);

    // Elimina el registro de textura. Llamar ANTES de recrear el framebuffer.
    void ReleaseTexture();

    // Tamaño deseado por el panel (content region del frame anterior). {0,0} al inicio.
    ImVec2 GetDesiredSize() const { return desiredSize; }

private:
    VulkanFramebuffer* framebuffer = nullptr;
    VkDescriptorSet    texDS       = VK_NULL_HANDLE;
    ImVec2             desiredSize = { 0.0f, 0.0f };
};

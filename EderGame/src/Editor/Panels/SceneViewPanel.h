#pragma once
#include "Panel.h"
#include "Renderer/Vulkan/VulkanFramebuffer.h"
#include "../EditorTypes.h"
#include "ECS/Registry.h"
#include "ECS/Entity.h"
#include <vulkan/vulkan.h>
#include <imgui/imgui.h>
#include <glm/glm.hpp>
#include <utility>

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

    // ── Ray picking ─────────────────────────────────────────────────
    /// True for the one frame in which a left-click pick occurred.
    bool   HasPick()    const { return m_hasPick; }
    /// Consume the pick result (resets flag). Returns NULL_ENTITY if miss.
    Entity ConsumePick() { m_hasPick = false; return std::exchange(m_pickedEntity, NULL_ENTITY); }

private:
    // ── Framebuffer / texture ──────────────────────────────────────────────
    VulkanFramebuffer* framebuffer      = nullptr;
    VkDescriptorSet    texDS            = VK_NULL_HANDLE;
    VkImageView        lastView         = VK_NULL_HANDLE;
    ImVec2             desiredSize      = { 0.0f, 0.0f };
    ImVec2             contentScreenPos  = { 0.0f, 0.0f };
    ImVec2             contentScreenSize = { 0.0f, 0.0f };

    // ── Picking state ─────────────────────────────────────────────────
    glm::mat4 m_lastView     = glm::mat4(1.0f);
    glm::mat4 m_lastProj     = glm::mat4(1.0f);
    Entity    m_pickedEntity = NULL_ENTITY;
    bool      m_hasPick      = false;

    Entity DoRayPick(Registry& registry,
                     const glm::vec3& rayOrig,
                     const glm::vec3& rayDir) const;
};

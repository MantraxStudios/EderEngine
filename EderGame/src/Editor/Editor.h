#pragma once
#include <SDL3/SDL.h>
#include <vulkan/vulkan.h>
#include "EditorTypes.h"
#include "Core/Camera.h"
#include "ECS/Registry.h"
#include "Renderer/Vulkan/VulkanFramebuffer.h"
#include "Panels/StatsPanel.h"
#include "Panels/CameraPanel.h"
#include "Panels/HierarchyPanel.h"
#include "Panels/InspectorPanel.h"
#include "Panels/SceneViewPanel.h"

class Editor
{
public:
    void Init    (SDL_Window* window);
    void Shutdown();

    void ProcessEvent(const SDL_Event& event);
    void BeginFrame();
    void EndFrame();                          // frame Vulkan saltado
    void Draw    (Camera& camera, Registry& registry, float dt);
    void Render  (VkCommandBuffer cmd);

    // Escena 3D — llamar tras crear/recrear el framebuffer de escena
    void SetSceneViewFramebuffer(VulkanFramebuffer* fb);
    void ReleaseSceneViewFramebuffer();
    void GetSceneViewSize(uint32_t& w, uint32_t& h) const;

    bool WantCaptureMouse()    const;
    bool WantCaptureKeyboard() const;

    PlayState GetPlayState() const { return playState; }
    GizmoMode GetGizmoMode() const { return gizmoMode; }
    Entity    GetSelected()  const { return hierarchy.GetSelected(); }

private:
    void DrawMenuBar();
    void DrawToolbar();
    void DrawDockspace();
    void ApplyTheme();

    bool showDemo          = false;
    bool firstLayout       = true;

    PlayState playState    = PlayState::Stopped;
    GizmoMode gizmoMode    = GizmoMode::Translate;
    bool      snapEnabled  = false;
    float     snapValue    = 10.0f;

    StatsPanel     stats;
    CameraPanel    cameraPanel;
    HierarchyPanel hierarchy;
    InspectorPanel inspector;
    SceneViewPanel sceneView;
};

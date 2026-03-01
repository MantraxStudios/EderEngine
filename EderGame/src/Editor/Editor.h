#pragma once
#include <SDL3/SDL.h>
#include <vulkan/vulkan.h>
#include "Core/Camera.h"
#include "Core/Scene.h"
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
    void Draw    (Camera& camera, Scene& scene, float dt);
    void Render  (VkCommandBuffer cmd);

    // Escena 3D — llamar tras crear/recrear el framebuffer de escena
    void SetSceneViewFramebuffer(VulkanFramebuffer* fb);
    void ReleaseSceneViewFramebuffer();
    void GetSceneViewSize(uint32_t& w, uint32_t& h) const;

    bool WantCaptureMouse()    const;
    bool WantCaptureKeyboard() const;

private:
    void DrawMenuBar();
    void DrawDockspace();

    bool showDemo = false;

    StatsPanel     stats;
    CameraPanel    camera;
    HierarchyPanel hierarchy;
    InspectorPanel inspector;
    SceneViewPanel sceneView;
};

#pragma once
#include <SDL3/SDL.h>
#include <vulkan/vulkan.h>
#include <functional>
#include <string>
#include "EditorTypes.h"
#include "Core/Camera.h"
#include "ECS/Registry.h"
#include "Renderer/Vulkan/VulkanFramebuffer.h"
#include "Panels/StatsPanel.h"
#include "Panels/CameraPanel.h"
#include "Panels/HierarchyPanel.h"
#include "Panels/InspectorPanel.h"
#include "Panels/SceneViewPanel.h"
#include "Panels/AssetBrowserPanel.h"
#include "Panels/MaterialEditorPanel.h"

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

    // ── Scene operation callbacks ─────────────────────────────────────────────────

    void SetNewSceneCallback   (std::function<void()>                   cb) { m_onNewScene    = std::move(cb); }
    void SetSaveSceneCallback  (std::function<void()>                   cb) { m_onSaveScene   = std::move(cb); }
    void SetSaveAsCallback     (std::function<void(const std::string&)> cb) { m_onSaveAs      = std::move(cb); }
    void SetOpenSceneCallback  (std::function<void(const std::string&)> cb) { m_onOpenScene   = std::move(cb); }

    void SetCurrentSceneName(const std::string& name) { m_currentSceneName = name; }
    const std::string& GetCurrentSceneName() const    { return m_currentSceneName; }

private:
    void DrawMenuBar();
    void DrawToolbar();
    void DrawDockspace();
    void DrawOpenScenePicker();
    void DrawSaveSceneAsModal();
    void HandleSceneShortcuts();
    void ApplyTheme();

    bool showDemo          = false;
    bool firstLayout       = true;

    PlayState playState    = PlayState::Stopped;
    GizmoMode gizmoMode    = GizmoMode::Translate;
    bool      snapEnabled  = false;
    float     snapValue    = 10.0f;

    // ── Scene state ─────────────────────────────────────────────────────────────────
    std::string m_currentSceneName     = "Untitled";
    bool        m_openScenePickerOpen  = false;
    bool        m_saveSceneAsOpen      = false;
    char        m_saveSceneAsName[128] = {};

    // ── Callbacks ─────────────────────────────────────────────────────────────────────
    std::function<void()>                   m_onNewScene;
    std::function<void()>                   m_onSaveScene;
    std::function<void(const std::string&)> m_onSaveAs;
    std::function<void(const std::string&)> m_onOpenScene;

    StatsPanel        stats;
    CameraPanel       cameraPanel;
    HierarchyPanel    hierarchy;
    InspectorPanel    inspector;
    SceneViewPanel    sceneView;
    AssetBrowserPanel assetBrowser;
    MaterialEditorPanel materialEditor;
};

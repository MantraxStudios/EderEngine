#pragma once
#include <SDL3/SDL.h>
#include <vulkan/vulkan.h>
#include <imgui/imgui.h>
#include <functional>
#include <string>
#include <atomic>
#include <mutex>
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

    PlayState  GetPlayState()  const { return playState;  }
    GizmoMode  GetGizmoMode()  const { return gizmoMode;  }
    PlayTarget GetPlayTarget() const { return m_playTarget; }
    Entity     GetSelected()   const { return hierarchy.GetSelected(); }

    // Force the editor back to Stopped state (e.g. when EderPlayer process exits).
    void ForceStop() { playState = PlayState::Stopped; }

    // Screen-space rectangle of the rendered scene image inside the Viewport panel.
    // Valid after the first Draw() call.  Use for Win32 window embedding.
    ImVec2 GetSceneViewContentPos()  const { return sceneView.GetContentScreenPos();  }
    ImVec2 GetSceneViewContentSize() const { return sceneView.GetContentScreenSize(); }

    // ── Scene operation callbacks ─────────────────────────────────────────────────

    void SetNewSceneCallback   (std::function<void()>                   cb) { m_onNewScene    = std::move(cb); }
    void SetSaveSceneCallback  (std::function<void()>                   cb) { m_onSaveScene   = std::move(cb); }
    void SetSaveAsCallback     (std::function<void(const std::string&)> cb) { m_onSaveAs      = std::move(cb); }
    void SetOpenSceneCallback  (std::function<void(const std::string&)> cb) { m_onOpenScene   = std::move(cb); }

    // ── Build / Play callbacks ────────────────────────────────────────────────────
    // outPak        : absolute path where the .pak should be written
    // initialScene  : relative content path e.g. "scenes/main.scene"
    void SetBuildPakCallback(std::function<void(const std::string& outPak,
                                                const std::string& initialScene,
                                                const std::string& gameName)> cb)
    { m_onBuildPak = std::move(cb); }
    void SetPlayCallback (std::function<void()> cb) { m_onPlay  = std::move(cb); }
    void SetStopCallback (std::function<void()> cb) { m_onStop  = std::move(cb); }

    // Called by Application (possibly from a background thread) to append to the build log
    void AppendBuildLog(const std::string& line)
    {
        std::lock_guard<std::mutex> lk(m_buildLogMutex);
        m_pendingLogLines += line + "\n";
    }
    void SetBuildRunning(bool v) { m_buildRunning.store(v); }

    void SetCurrentSceneName(const std::string& name) { m_currentSceneName = name; }
    const std::string& GetCurrentSceneName() const    { return m_currentSceneName; }

private:
    void DrawMenuBar();
    void DrawToolbar();
    void DrawDockspace();
    void DrawOpenScenePicker();
    void DrawSaveSceneAsModal();
    void DrawBuildGameModal();
    void HandleSceneShortcuts();
    void ApplyTheme();

    bool showDemo          = false;
    bool firstLayout       = true;

    PlayState playState    = PlayState::Stopped;
    GizmoMode gizmoMode    = GizmoMode::Translate;
    bool        snapEnabled  = false;
    float       snapValue    = 10.0f;
    PlayTarget  m_playTarget = PlayTarget::Embedded;

    // ── Scene state ─────────────────────────────────────────────────────────────────
    std::string m_currentSceneName     = "Untitled";
    bool        m_openScenePickerOpen  = false;
    bool        m_saveSceneAsOpen      = false;
    char        m_saveSceneAsName[128] = {};

    // ── Build Game modal state ───────────────────────────────────────────────────────
    bool        m_buildModalOpen         = false;
    char        m_buildGameName[128]     = "EderGame";
    char        m_buildOutPath[512]      = "Game.pak";
    char        m_buildInitialScene[512] = {};   // relative path selected by user
    std::string m_buildLog;
    std::string m_pendingLogLines;           // written from bg thread, drained on main thread
    bool        m_buildLogDirty  = false;    // scroll-to-bottom flag
    std::atomic<bool> m_buildRunning{false};
    std::mutex  m_buildLogMutex;

    // ── Callbacks ─────────────────────────────────────────────────────────────────────
    std::function<void()>                   m_onNewScene;
    std::function<void()>                   m_onSaveScene;
    std::function<void(const std::string&)> m_onSaveAs;
    std::function<void(const std::string&)> m_onOpenScene;
    std::function<void(const std::string&, const std::string&, const std::string&)> m_onBuildPak;
    std::function<void()>                   m_onPlay;
    std::function<void()>                   m_onStop;

    StatsPanel        stats;
    CameraPanel       cameraPanel;
    HierarchyPanel    hierarchy;
    InspectorPanel    inspector;
    SceneViewPanel    sceneView;
    AssetBrowserPanel assetBrowser;
    MaterialEditorPanel materialEditor;
};

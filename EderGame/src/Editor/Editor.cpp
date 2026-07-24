#include "Editor.h"
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imgui/imgui_impl_sdl3.h>
#include <imgui/imgui_impl_vulkan.h>
#include <imgui/ImGuizmo.h>
#include "Renderer/Vulkan/VulkanInstance.h"
#include "Renderer/Vulkan/VulkanSwapchain.h"
#include "Renderer/Vulkan/VulkanTexture.h"
#include <IO/AssetManager.h>
#include <filesystem>
#include "ECS/Components/TagComponent.h"
#include "ECS/Components/TransformComponent.h"
#include "ECS/Components/MeshRendererComponent.h"
#include "ECS/Systems/TransformSystem.h"
#include "Core/MeshManager.h"
#include "Renderer/Vulkan/VulkanMesh.h"
#include <Events/EventBus.h>
#include <Events/Events.h>
#include <glm/glm.hpp>
#include <cstring>
#include <cmath>
#include <algorithm>

// ─────────────────────────────────────────────────────────────────────────────
// Theme
// ─────────────────────────────────────────────────────────────────────────────

void Editor::ApplyTheme()
{
    ImGuiStyle& s = ImGui::GetStyle();

    // ── Shape — Unity-style: flat, square, subtle 1px field borders ──────────
    s.WindowRounding    = 0.0f;
    s.ChildRounding     = 0.0f;
    s.FrameRounding     = 0.0f;
    s.PopupRounding     = 0.0f;
    s.ScrollbarRounding = 0.0f;
    s.GrabRounding      = 0.0f;
    s.TabRounding       = 0.0f;

    s.WindowBorderSize  = 1.0f;
    s.FrameBorderSize   = 1.0f;
    s.PopupBorderSize   = 1.0f;

    s.WindowPadding     = ImVec2(8.0f, 6.0f);
    s.FramePadding      = ImVec2(6.0f, 3.0f);
    s.ItemSpacing       = ImVec2(6.0f, 4.0f);
    s.ItemInnerSpacing  = ImVec2(4.0f, 4.0f);
    s.IndentSpacing     = 14.0f;
    s.ScrollbarSize     = 13.0f;
    s.GrabMinSize       = 10.0f;

    // ── Palette — Unity dark (Professional) ─────────────────────────────────
    const ImVec4 bg0   = ImVec4(0.13f, 0.13f, 0.13f, 1.00f); // deepest bg (#212121)
    const ImVec4 bg1   = ImVec4(0.22f, 0.22f, 0.22f, 1.00f); // window bg  (#383838)
    const ImVec4 bg2   = ImVec4(0.17f, 0.17f, 0.17f, 1.00f); // header / tab strip
    const ImVec4 bg3   = ImVec4(0.16f, 0.16f, 0.16f, 1.00f); // input fields (#2A2A2A)
    const ImVec4 bg4   = ImVec4(0.25f, 0.25f, 0.25f, 1.00f); // hovered
    const ImVec4 bg5   = ImVec4(0.30f, 0.30f, 0.30f, 1.00f); // active

    const ImVec4 border   = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
    const ImVec4 borderHv = ImVec4(0.35f, 0.35f, 0.35f, 1.00f);

    const ImVec4 textFull = ImVec4(0.83f, 0.83f, 0.83f, 1.00f); // #D2D2D2
    const ImVec4 textDim  = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);

    // Unity selection blue (#2C5D87 family)
    const ImVec4 accent    = ImVec4(0.17f, 0.36f, 0.53f, 1.00f);
    const ImVec4 accentHv  = ImVec4(0.22f, 0.44f, 0.64f, 1.00f);
    const ImVec4 accentAc  = ImVec4(0.14f, 0.30f, 0.45f, 1.00f);
    const ImVec4 accentDim = ImVec4(0.17f, 0.36f, 0.53f, 0.55f);

    // Tab active tint
    const ImVec4 tabAct   = ImVec4(0.26f, 0.26f, 0.26f, 1.00f);

    // ── Colors ───────────────────────────────────────────────────────────────
    ImVec4* c = s.Colors;

    c[ImGuiCol_Text]                  = textFull;
    c[ImGuiCol_TextDisabled]          = textDim;

    c[ImGuiCol_WindowBg]              = bg1;
    c[ImGuiCol_ChildBg]               = bg0;
    c[ImGuiCol_PopupBg]               = bg1;

    c[ImGuiCol_Border]                = border;
    c[ImGuiCol_BorderShadow]          = ImVec4(0,0,0,0);

    c[ImGuiCol_FrameBg]               = bg3;
    c[ImGuiCol_FrameBgHovered]        = bg4;
    c[ImGuiCol_FrameBgActive]         = bg5;

    c[ImGuiCol_TitleBg]               = bg0;
    c[ImGuiCol_TitleBgActive]         = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
    c[ImGuiCol_TitleBgCollapsed]      = bg0;

    c[ImGuiCol_MenuBarBg]             = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);

    c[ImGuiCol_ScrollbarBg]           = bg0;
    c[ImGuiCol_ScrollbarGrab]         = bg4;
    c[ImGuiCol_ScrollbarGrabHovered]  = bg5;
    c[ImGuiCol_ScrollbarGrabActive]   = accent;

    c[ImGuiCol_CheckMark]             = accent;
    c[ImGuiCol_SliderGrab]            = accent;
    c[ImGuiCol_SliderGrabActive]      = accentHv;

    // Unity buttons: mid-gray, slightly lighter on hover (no colour shift)
    c[ImGuiCol_Button]                = ImVec4(0.35f, 0.35f, 0.35f, 1.00f);
    c[ImGuiCol_ButtonHovered]         = ImVec4(0.42f, 0.42f, 0.42f, 1.00f);
    c[ImGuiCol_ButtonActive]          = ImVec4(0.28f, 0.28f, 0.28f, 1.00f);

    c[ImGuiCol_Header]                = accentDim;
    c[ImGuiCol_HeaderHovered]         = accentHv;
    c[ImGuiCol_HeaderActive]          = accentAc;

    c[ImGuiCol_Separator]             = border;
    c[ImGuiCol_SeparatorHovered]      = accent;
    c[ImGuiCol_SeparatorActive]       = accentHv;

    c[ImGuiCol_ResizeGrip]            = ImVec4(0,0,0,0);
    c[ImGuiCol_ResizeGripHovered]     = accentDim;
    c[ImGuiCol_ResizeGripActive]      = accent;

    c[ImGuiCol_Tab]                   = bg2;
    c[ImGuiCol_TabHovered]            = accentDim;
    c[ImGuiCol_TabSelected]           = tabAct;
    c[ImGuiCol_TabSelectedOverline]   = accent;
    c[ImGuiCol_TabDimmed]             = bg1;
    c[ImGuiCol_TabDimmedSelected]     = bg2;
    c[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(0,0,0,0);

    c[ImGuiCol_DockingPreview]        = accentDim;
    c[ImGuiCol_DockingEmptyBg]        = bg0;

    c[ImGuiCol_PlotLines]             = accent;
    c[ImGuiCol_PlotLinesHovered]      = accentHv;
    c[ImGuiCol_PlotHistogram]         = accent;
    c[ImGuiCol_PlotHistogramHovered]  = accentHv;

    c[ImGuiCol_TableHeaderBg]         = bg2;
    c[ImGuiCol_TableBorderStrong]     = border;
    c[ImGuiCol_TableBorderLight]      = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
    c[ImGuiCol_TableRowBg]            = ImVec4(0,0,0,0);
    c[ImGuiCol_TableRowBgAlt]         = ImVec4(1,1,1,0.03f);

    c[ImGuiCol_TextLink]              = accent;
    c[ImGuiCol_TextSelectedBg]        = accentDim;

    c[ImGuiCol_DragDropTarget]        = accentHv;
    c[ImGuiCol_NavCursor]             = accent;
    c[ImGuiCol_NavWindowingHighlight] = ImVec4(1,1,1,0.70f);
    c[ImGuiCol_NavWindowingDimBg]     = ImVec4(0,0,0,0.50f);
    c[ImGuiCol_ModalWindowDimBg]      = ImVec4(0,0,0,0.60f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

void Editor::Init(SDL_Window* window)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ApplyTheme();
    ImGui_ImplSDL3_InitForVulkan(window);

    auto& vi = VulkanInstance::Get();
    auto& sc = VulkanSwapchain::Get();
    VkFormat colorFmt = (VkFormat)sc.GetFormat();

    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion          = VK_API_VERSION_1_3;
    info.Instance            = *vi.GetInstance();
    info.PhysicalDevice      = *vi.GetPhysicalDevice();
    info.Device              = *vi.GetDevice();
    info.QueueFamily         = vi.GetGraphicsIndex();
    info.Queue               = *vi.GetGraphicsQueue();
    info.DescriptorPoolSize  = 100;
    info.MinImageCount       = 2;
    info.ImageCount          = static_cast<uint32_t>(sc.GetImages().size());
    info.UseDynamicRendering = true;
    info.PipelineInfoMain.PipelineRenderingCreateInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
    info.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount    = 1;
    info.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &colorFmt;
    ImGui_ImplVulkan_Init(&info);

    // Wire AssetBrowser double-click callbacks
    assetBrowser.SetSelectCallback(
        [this](uint64_t guid, const Krayon::AssetMeta& meta)
        {
            if (meta.type == Krayon::AssetType::Material)
            {
                materialEditor.Open(guid);
                materialEditor.open = true;
            }
            else if (meta.type == Krayon::AssetType::Scene)
            {
                // Open scene — forward to Application via the registered callback
                if (m_onOpenScene)
                {
                    const std::string absPath =
                        Krayon::AssetManager::Get().GetWorkDir() + "/" + meta.path;
                    m_onOpenScene(absPath);
                    m_currentSceneName = meta.name;
                }
            }
            else if (meta.type == Krayon::AssetType::Prefab)
            {
                const std::string absPath =
                    Krayon::AssetManager::Get().GetWorkDir() + "/" + meta.path;
                OpenPrefab(absPath);
            }
        });

    // Wire hierarchy "Save as Prefab" callback
    hierarchy.SetSavePrefabCallback([this](Entity e) {
        m_savePrefabEntity    = e;
        m_savePrefabModalOpen = true;
        // Default filename from entity tag
        std::string def = "Prefab";
        Registry* activeReg = m_prefabMode ? &m_prefabRegistry : m_lastSceneRegistry;
        if (activeReg && activeReg->Has<TagComponent>(e))
            def = activeReg->Get<TagComponent>(e).name;
        std::strncpy(m_savePrefabAsName, def.c_str(), sizeof(m_savePrefabAsName) - 1);
        m_savePrefabAsName[sizeof(m_savePrefabAsName) - 1] = '\0';
    });
}

void Editor::Shutdown()
{
    sceneView.ReleaseTexture();
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
}

// ─────────────────────────────────────────────────────────────────────────────
// Frame
// ─────────────────────────────────────────────────────────────────────────────

void Editor::ProcessEvent(const SDL_Event& event)
{
    ImGui_ImplSDL3_ProcessEvent(&event);
}

void Editor::BeginFrame()
{
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    ImGuizmo::BeginFrame();
}

void Editor::EndFrame()
{
    ImGui::EndFrame();
}

void Editor::Draw(Camera& cam, Registry& registry, float dt)
{
    m_lastSceneRegistry = &registry;
    stats      .Update(dt);
    cameraPanel.SetCamera(&cam);

    // In prefab mode, panels target the isolated prefab registry
    if (m_prefabMode)
    {
        hierarchy.SetRegistry(&m_prefabRegistry);
        inspector.SetRegistry(&m_prefabRegistry);
    }
    else
    {
        hierarchy.SetRegistry(&registry);
        inspector.SetRegistry(&registry);
    }
    inspector  .SetSelected(hierarchy.GetSelected());

    HandleSceneShortcuts(cam, registry);
    DrawMenuBar();
    DrawToolbar();
    DrawDockspace();

    if (stats       .open) stats       .OnDraw();
    if (cameraPanel .open) cameraPanel .OnDraw();
    if (hierarchy   .open) hierarchy   .OnDraw();
    if (inspector   .open) inspector   .OnDraw();
    if (assetBrowser.open)  assetBrowser.OnDraw();
    if (materialEditor.open) materialEditor.OnDraw();
    if (postProcess.open)   postProcess.OnDraw();
    if (sceneView  .open)
    {
        ImVec2 svSize   = sceneView.GetDesiredSize();
        float  svAspect = (svSize.y > 0.0f) ? svSize.x / svSize.y : 1.0f;
        sceneView.OnDraw(gizmoMode, snapEnabled, snapValue,
                         cam.GetView(), cam.GetProjection(svAspect),
                         &registry, hierarchy.GetSelected());

        // ── Viewport click → selection ──────────────────────────────────
        if (sceneView.HasPick())
        {
            Entity picked = sceneView.ConsumePick();
            hierarchy.SetSelected(picked);
            EventBus<EntitySelectedEvent>::Emit({ picked });
        }
    }
    if (showDemo)         ImGui::ShowDemoWindow(&showDemo);
    DrawBuildGameModal();
    DrawPrefabBanner();
    DrawSavePrefabModal();
}

void Editor::Render(VkCommandBuffer cmd)
{
    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
}

// ─────────────────────────────────────────────────────────────────────────────
// SceneView helpers
// ─────────────────────────────────────────────────────────────────────────────

void Editor::SetSceneViewFramebuffer(VulkanFramebuffer* fb)
{
    sceneView.SetFramebuffer(fb);
}

void Editor::ReleaseSceneViewFramebuffer()
{
    sceneView.ReleaseTexture();
}

void Editor::GetSceneViewSize(uint32_t& w, uint32_t& h) const
{
    ImVec2 s = sceneView.GetDesiredSize();
    w = static_cast<uint32_t>(s.x);
    h = static_cast<uint32_t>(s.y);
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

bool Editor::WantCaptureMouse()    const { return ImGui::GetIO().WantCaptureMouse; }
bool Editor::WantCaptureKeyboard() const { return ImGui::GetIO().WantCaptureKeyboard; }

// ──────────────────────────────────────────────────────────────────────────────
void Editor::HandleSceneShortcuts(Camera& cam, Registry& registry)
{
    const ImGuiIO& io    = ImGui::GetIO();
    const bool     ctrl  = io.KeyCtrl;
    const bool     shift = io.KeyShift;

    // ── Global shortcuts (always active, even when viewport has focus) ────────
    if (ctrl && !shift && ImGui::IsKeyPressed(ImGuiKey_S, false))
    {
        if (m_onSaveScene) m_onSaveScene();
    }
    if (ctrl && shift && ImGui::IsKeyPressed(ImGuiKey_S, false))
    {
        m_saveSceneAsOpen = true;
        std::strncpy(m_saveSceneAsName, m_currentSceneName.c_str(), sizeof(m_saveSceneAsName) - 1);
    }
    if (ctrl && !shift && ImGui::IsKeyPressed(ImGuiKey_N, false))
    {
        if (m_onNewScene) m_onNewScene();
    }
    if (ctrl && !shift && ImGui::IsKeyPressed(ImGuiKey_O, false))
    {
        m_openScenePickerOpen = true;
    }

    // ── Context shortcuts (only when ImGui has keyboard focus) ────────────────
    if (!io.WantCaptureKeyboard) return;

    if (ctrl && !shift && ImGui::IsKeyPressed(ImGuiKey_D, false))
    {
        hierarchy.DuplicateSelected();
    }

    // Delete selected entity (Del) — no modifier, and not while typing in a field.
    if (!ctrl && !shift && !io.WantTextInput &&
        (ImGui::IsKeyPressed(ImGuiKey_Delete, false)))
    {
        hierarchy.DeleteSelected();
    }

    // Gizmo mode (W/E/R) and focus (F). These letters double as camera controls,
    // but WASD only moves the camera while the right mouse button is held, so we
    // only switch modes when RMB is up and no text field is active.
    const bool rmbHeld = ImGui::IsMouseDown(ImGuiMouseButton_Right);
    if (!ctrl && !shift && !rmbHeld && !io.WantTextInput)
    {
        if (ImGui::IsKeyPressed(ImGuiKey_W, false)) gizmoMode = GizmoMode::Translate;
        if (ImGui::IsKeyPressed(ImGuiKey_E, false)) gizmoMode = GizmoMode::Rotate;
        if (ImGui::IsKeyPressed(ImGuiKey_R, false)) gizmoMode = GizmoMode::Scale;
        if (ImGui::IsKeyPressed(ImGuiKey_F, false)) FocusSelected(cam, registry);
    }

    // Build
    if (ctrl && !shift && ImGui::IsKeyPressed(ImGuiKey_B, false))
    {
        if (!m_buildRunning)
        {
            m_buildLog.clear();
            m_buildModalOpen = true;
        }
    }

    // Play / Stop (F5 / F7)
    if (!ctrl && !shift && ImGui::IsKeyPressed(ImGuiKey_F5, false))
    {
        if (playState == PlayState::Stopped || playState == PlayState::Paused)
        {
            playState = PlayState::Playing;
            if (m_onPlay) m_onPlay();
        }
    }
    if (!ctrl && !shift && ImGui::IsKeyPressed(ImGuiKey_F7, false))
    {
        if (playState != PlayState::Stopped)
        {
            playState = PlayState::Stopped;
            if (m_onStop) m_onStop();
        }
    }
}

// Frame the editor camera on the current selection: keep the current view
// direction and move the camera back far enough to fit the object's bounds.
void Editor::FocusSelected(Camera& cam, Registry& registry)
{
    Entity sel = hierarchy.GetSelected();
    if (sel == NULL_ENTITY || !registry.Has<TransformComponent>(sel)) return;

    glm::mat4 world = TransformSystem::GetWorldMatrix(sel, registry);

    // Object bounds → world-space center + radius (unit-cube fallback if no mesh).
    glm::vec3 bmin(-0.5f), bmax(0.5f);
    if (registry.Has<MeshRendererComponent>(sel))
    {
        const auto& mr = registry.Get<MeshRendererComponent>(sel);
        if (!mr.meshPath.empty() && MeshManager::Get().Has(mr.meshPath))
        {
            VulkanMesh& mesh = MeshManager::Get().Load(mr.meshPath);
            bmin = mesh.GetBoundsMin();
            bmax = mesh.GetBoundsMax();
        }
    }
    glm::vec3 localCenter = 0.5f * (bmin + bmax);
    glm::vec3 center = glm::vec3(world * glm::vec4(localCenter, 1.0f));

    float sx = glm::length(glm::vec3(world[0]));
    float sy = glm::length(glm::vec3(world[1]));
    float sz = glm::length(glm::vec3(world[2]));
    float radius = glm::length(0.5f * (bmax - bmin)) * std::max(sx, std::max(sy, sz));
    if (radius < 0.01f) radius = 0.5f;

    // Distance so the sphere fits vertically in the view.
    float halfFov = glm::radians(cam.fov * 0.5f);
    float dist = radius / std::max(std::sin(halfFov), 0.05f);
    dist = std::max(dist, radius + cam.nearPlane + 0.1f);

    cam.fpsPos = center - cam.GetForward() * dist;
}

// ──────────────────────────────────────────────────────────────────────────────
void Editor::DrawSaveSceneAsModal()
{
    if (m_saveSceneAsOpen)
        ImGui::OpenPopup("Save Scene As##dlg");

    ImGui::SetNextWindowSize(ImVec2(360, 110), ImGuiCond_Always);
    if (!ImGui::BeginPopupModal("Save Scene As##dlg",
                                &m_saveSceneAsOpen,
                                ImGuiWindowFlags_NoResize))
        return;

    ImGui::Text("Scene name:");
    ImGui::SetNextItemWidth(-1);
    bool confirm = ImGui::InputText("##sasname", m_saveSceneAsName,
                                    sizeof(m_saveSceneAsName),
                                    ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::Spacing();
    if (ImGui::Button("Save", ImVec2(120, 0)) || confirm)
    {
        const std::string name = m_saveSceneAsName;
        if (!name.empty())
        {
            if (m_onSaveAs) m_onSaveAs(name);
            m_currentSceneName = name;
            m_saveSceneAsOpen  = false;
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0)))
    {
        m_saveSceneAsOpen = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// ──────────────────────────────────────────────────────────────────────────────
void Editor::DrawOpenScenePicker()
{
    if (m_openScenePickerOpen)
        ImGui::OpenPopup("##openScene");

    ImGui::SetNextWindowSize(ImVec2(440, 340), ImGuiCond_Always);
    if (!ImGui::BeginPopupModal("##openScene",
                                &m_openScenePickerOpen,
                                ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar))
        return;

    ImGui::TextColored(ImVec4(0.65f, 0.85f, 1.0f, 1.0f), "Open Scene");
    ImGui::Separator();
    ImGui::Spacing();

    const auto& all = Krayon::AssetManager::Get().GetAll();
    bool closePicker = false;

    if (all.empty())
    {
        ImGui::TextDisabled("No scenes found in asset registry.");
    }
    else
    {
        ImGui::BeginChild("##scenelist", ImVec2(0, 260), false);
        for (const auto& [guid, meta] : all)
        {
            if (meta.type != Krayon::AssetType::Scene) continue;

            std::string label = "[Scene]  " + meta.name;
            if (ImGui::Selectable(label.c_str(), false,
                                  ImGuiSelectableFlags_AllowDoubleClick))
            {
                if (ImGui::IsMouseDoubleClicked(0))
                {
                    const std::string absPath =
                        Krayon::AssetManager::Get().GetWorkDir() + "/" + meta.path;
                    if (m_onOpenScene) m_onOpenScene(absPath);
                    m_currentSceneName = meta.name;
                    m_openScenePickerOpen = false;
                    closePicker = true;
                }
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", meta.path.c_str());
        }
        ImGui::EndChild();
    }

    ImGui::Spacing();
    if (ImGui::Button("Cancel", ImVec2(120, 0)) || closePicker)
    {
        m_openScenePickerOpen = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// ─────────────────────────────────────────────────────────────────────────────
//  DrawMenuBar
// ─────────────────────────────────────────────────────────────────────────────

void Editor::DrawMenuBar()
{
    if (!ImGui::BeginMainMenuBar()) return;

    if (ImGui::BeginMenu("File"))
    {
        if (ImGui::MenuItem("New Scene",   "Ctrl+N"))
        {
            if (m_onNewScene) m_onNewScene();
        }
        if (ImGui::MenuItem("Open Scene...",  "Ctrl+O"))
        {
            m_openScenePickerOpen = true;
            ImGui::OpenPopup("##openScene");
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Save",        "Ctrl+S"))
        {
            m_saveSceneAsOpen = true;
            std::strncpy(m_saveSceneAsName, m_currentSceneName.c_str(), sizeof(m_saveSceneAsName) - 1);
        }
        if (ImGui::MenuItem("Save As...",  "Ctrl+Shift+S"))
        {
            m_saveSceneAsOpen = true;
            std::strncpy(m_saveSceneAsName, m_currentSceneName.c_str(), sizeof(m_saveSceneAsName) - 1);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Exit",        "Alt+F4")) {}
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit"))
    {
        ImGui::BeginDisabled();
        ImGui::MenuItem("Undo",  "Ctrl+Z");
        ImGui::MenuItem("Redo",  "Ctrl+Y");
        ImGui::EndDisabled();
        ImGui::Separator();
        ImGui::BeginDisabled();
        ImGui::MenuItem("Cut",   "Ctrl+X");
        ImGui::MenuItem("Copy",  "Ctrl+C");
        ImGui::MenuItem("Paste", "Ctrl+V");
        ImGui::EndDisabled();
        ImGui::Separator();
        if (ImGui::BeginMenu("Editor Preferences"))
        {
            ImGui::MenuItem("Snap", nullptr, &snapEnabled);
            ImGui::SetNextItemWidth(80);
            ImGui::DragFloat("Snap Value", &snapValue, 1.0f, 0.25f, 100.0f, "%.2f");
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Window"))
    {
        ImGui::MenuItem("World Outliner", nullptr, &hierarchy   .open);
        ImGui::MenuItem("Details",        nullptr, &inspector   .open);
        ImGui::MenuItem("Viewport",       nullptr, &sceneView   .open);
        ImGui::MenuItem("Camera",         nullptr, &cameraPanel .open);
        ImGui::MenuItem("Stats",          nullptr, &stats       .open);
        ImGui::MenuItem("Asset Browser",    nullptr, &assetBrowser  .open);
        ImGui::MenuItem("Material Editor",  nullptr, &materialEditor.open);
        ImGui::MenuItem("Post Process",     nullptr, &postProcess   .open);
        ImGui::Separator();
        ImGui::MenuItem("Deferred Rendering (+SSAO)", nullptr, &deferredRendering);
        if (ImGui::MenuItem("Reset Layout"))
            m_resetLayout = true;
        ImGui::Separator();
        ImGui::MenuItem("ImGui Demo",     nullptr, &showDemo);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Build"))
    {
        if (ImGui::MenuItem("Build Game...", "Ctrl+B"))
        {
            if (!m_buildRunning)
            {
                m_buildLog.clear();
                m_buildModalOpen = true;
            }
        }
        ImGui::Separator();
        ImGui::BeginDisabled();
        ImGui::MenuItem("Build Lighting Only");
        ImGui::EndDisabled();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Help"))
    {
        if (ImGui::MenuItem("About EderEngine"))
            ImGui::OpenPopup("##about");
        ImGui::EndMenu();
    }

    // Scene name centred in menu bar
    ImGui::SetCursorPosX((ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(m_currentSceneName.c_str()).x) * 0.5f);
    ImGui::TextDisabled("%s", m_currentSceneName.c_str());

    // Framerate on the right
    float fps = ImGui::GetIO().Framerate;
    char frStr[32];
    snprintf(frStr, sizeof(frStr), "%.0f FPS", fps);
    ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(frStr).x - 8.0f);
    ImGui::TextDisabled("%s", frStr);

    ImGui::EndMainMenuBar();

    // ── Save Scene As modal ─────────────────────────────────────────────────────
    DrawSaveSceneAsModal();

    // ── Open Scene picker modal ───────────────────────────────────────────────────
    DrawOpenScenePicker();
}

// Load the playback PNG icons once from <workdir>/../../Icons (build/Icons).
// Textures are intentionally leaked (3 tiny editor-only images) to dodge
// Vulkan teardown-order issues at shutdown.
void Editor::EnsureToolbarIcons()
{
    if (m_toolbarIconsLoaded) return;
    const std::string wd = Krayon::AssetManager::Get().GetWorkDir();
    if (wd.empty()) return;   // retry next frame
    m_toolbarIconsLoaded = true;

    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path dir = fs::path(wd).parent_path().parent_path() / "Icons";
    if (!fs::exists(dir, ec)) dir = "Icons";

    auto load = [&](void** out, const char* file)
    {
        fs::path p = dir / file;
        std::error_code e2;
        if (!fs::exists(p, e2)) return;
        auto* tex = new VulkanTexture();
        try { tex->Load(p.string()); }
        catch (...) { delete tex; return; }
        *out = (void*)ImGui_ImplVulkan_AddTexture(
            (VkSampler)tex->GetSampler(), (VkImageView)tex->GetImageView(),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    };
    load(&m_icoPlay,  "playmode.png");
    load(&m_icoPause, "pausemode.png");
    load(&m_icoStop,  "stopmode.png");
}

void Editor::DrawToolbar()
{
    ImGuiViewport* vp     = ImGui::GetMainViewport();
    float          menuH  = ImGui::GetFrameHeight();
    const float    toolH  = 36.0f;

    ImGui::SetNextWindowPos (ImVec2(vp->Pos.x, vp->Pos.y + menuH));
    ImGui::SetNextWindowSize(ImVec2(vp->Size.x, toolH));
    ImGui::SetNextWindowViewport(vp->ID);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDocking    | ImGuiWindowFlags_NoTitleBar  |
        ImGuiWindowFlags_NoCollapse   | ImGuiWindowFlags_NoResize    |
        ImGuiWindowFlags_NoMove       | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings;

    ImGui::PushStyleVar  (ImGuiStyleVar_WindowRounding,  0.0f);
    ImGui::PushStyleVar  (ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar  (ImGuiStyleVar_WindowPadding,   ImVec2(6.0f, 4.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.10f, 0.10f, 0.10f, 1.0f));
    ImGui::Begin("##Toolbar", nullptr, flags);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);

    // ── Gizmo mode buttons (left) ────────────────────────────────────────────
    auto GizmoBtn = [&](const char* label, GizmoMode mode, const char* tooltip)
    {
        bool active = (gizmoMode == mode);
        if (active)
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.80f, 0.10f, 0.10f, 1.0f));
        if (ImGui::Button(label, ImVec2(28, 24)))
            gizmoMode = mode;
        if (active)
            ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", tooltip);
        ImGui::SameLine();
    };

    GizmoBtn("T", GizmoMode::Translate, "Translate  [W]");
    GizmoBtn("R", GizmoMode::Rotate,    "Rotate     [E]");
    GizmoBtn("S", GizmoMode::Scale,     "Scale      [R]");

    // Snap toggle
    ImGui::SameLine();
    {
        bool snap = snapEnabled;
        if (snap) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.80f, 0.10f, 0.10f, 1.0f));
        if (ImGui::Button("SNAP", ImVec2(44, 24))) snapEnabled = !snapEnabled;
        if (snap) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle Snap");
    }

    // ── Gizmo visibility ─────────────────────────────────────────────────────
    ImGui::SameLine();
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();
    {
        auto VizBtn = [&](const char* label, GizmoVisibility v, const char* tip)
        {
            bool active = (gizmoVisibility == v);
            if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.45f, 0.80f, 1.0f));
            if (ImGui::Button(label, ImVec2(28, 24))) gizmoVisibility = v;
            if (active) ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
            ImGui::SameLine();
        };
        VizBtn("GA", GizmoVisibility::All,          "Gizmos: Show All");
        VizBtn("GS", GizmoVisibility::SelectedOnly, "Gizmos: Selected Only");
        VizBtn("GN", GizmoVisibility::None,         "Gizmos: Hidden");
    }

    // ── Play / Pause / Stop (center, icon buttons) ───────────────────────────
    EnsureToolbarIcons();
    const float pbW = 22.0f + ImGui::GetStyle().FramePadding.x * 2.0f;
    float centerX = (vp->Size.x - (pbW * 3.0f + 20.0f + 4.0f * 4.0f)) * 0.5f;
    ImGui::SameLine(centerX);

    // Icon button with text fallback (used if the PNG is missing).
    auto PlaybackBtn = [&](const char* id, void* icon, const char* fallback,
                           bool tint, ImVec4 tintCol) -> bool
    {
        bool clicked;
        if (tint) ImGui::PushStyleColor(ImGuiCol_Button, tintCol);
        if (icon)
            clicked = ImGui::ImageButton(id,
                ImTextureRef((ImTextureID)(uint64_t)icon), ImVec2(22, 22));
        else
            clicked = ImGui::Button(fallback, ImVec2(pbW, 28));
        if (tint) ImGui::PopStyleColor();
        return clicked;
    };

    // Play
    {
        bool playing = (playState == PlayState::Playing);
        if (PlaybackBtn("##tb_play", m_icoPlay, ">", playing, ImVec4(0.10f, 0.55f, 0.10f, 1.0f)))
        {
            if (playState == PlayState::Stopped || playState == PlayState::Paused)
            {
                playState = PlayState::Playing;
                if (m_onPlay) m_onPlay();
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Play  [F5]");
    }
    ImGui::SameLine();

    // Pause
    {
        bool paused = (playState == PlayState::Paused);
        ImGui::BeginDisabled(playState == PlayState::Stopped);
        if (PlaybackBtn("##tb_pause", m_icoPause, "||", paused, ImVec4(0.65f, 0.50f, 0.05f, 1.0f)))
        {
            if (playState == PlayState::Playing)      playState = PlayState::Paused;
            else if (playState == PlayState::Paused)  playState = PlayState::Playing;
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pause  [F6]");
    }
    ImGui::SameLine();

    // Stop
    {
        ImGui::BeginDisabled(playState == PlayState::Stopped);
        if (PlaybackBtn("##tb_stop", m_icoStop, "[]", false, ImVec4(0, 0, 0, 0)))
        {
            playState = PlayState::Stopped;
            if (m_onStop) m_onStop();
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Stop  [F7]");
    }
    ImGui::SameLine();

    // ── Play-target dropdown (▼) ──────────────────────────────────────────────
    {
        const bool embedded = (m_playTarget == PlayTarget::Embedded);
        // Tint the button to indicate the active mode
        ImGui::PushStyleColor(ImGuiCol_Button,
            embedded ? ImVec4(0.15f, 0.35f, 0.65f, 1.0f)
                     : ImVec4(0.40f, 0.20f, 0.60f, 1.0f));

        ImGui::BeginDisabled(playState != PlayState::Stopped);
        if (ImGui::Button(embedded ? "v" : "^", ImVec2(20, 24)))
            ImGui::OpenPopup("##PlayTargetMenu");
        ImGui::EndDisabled();

        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(embedded
                ? "Play target: Embedded in Viewport\nClick to change"
                : "Play target: Standalone Window\nClick to change");

        if (ImGui::BeginPopup("##PlayTargetMenu"))
        {
            ImGui::TextDisabled("Play Target");
            ImGui::Separator();

            const bool selEmbed = (m_playTarget == PlayTarget::Embedded);
            const bool selSolo  = (m_playTarget == PlayTarget::Standalone);

            if (ImGui::MenuItem("  Embedded (Viewport)", nullptr, selEmbed))
                m_playTarget = PlayTarget::Embedded;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("EderPlayer renders inside the Viewport panel.\nLike Unreal 'Simulate in Editor'.");

            if (ImGui::MenuItem("  Standalone (New Window)", nullptr, selSolo))
                m_playTarget = PlayTarget::Standalone;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("EderPlayer opens as a separate OS window.\nGood for multi-monitor or full-screen testing.");

            ImGui::EndPopup();
        }
    }

    ImGui::End();
}

void Editor::DrawDockspace()
{
    ImGuiViewport* vp = ImGui::GetMainViewport();

    // Menu bar + toolbar
    float menuH   = ImGui::GetFrameHeight();
    float toolH   = 36.0f;
    float offsetY = menuH + toolH;

    ImGui::SetNextWindowPos (ImVec2(vp->Pos.x,  vp->Pos.y  + offsetY));
    ImGui::SetNextWindowSize(ImVec2(vp->Size.x, vp->Size.y - offsetY));
    ImGui::SetNextWindowViewport(vp->ID);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDocking             |
        ImGuiWindowFlags_NoTitleBar            |
        ImGuiWindowFlags_NoCollapse            |
        ImGuiWindowFlags_NoResize              |
        ImGuiWindowFlags_NoMove                |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus            |
        ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,   0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,    ImVec2(0.0f, 0.0f));
    ImGui::Begin("##Dockspace", nullptr, flags);
    ImGui::PopStyleVar(3);

    ImGuiID dockId = ImGui::GetID("MainDock");
    ImGui::DockSpace(dockId, ImVec2(0, 0), ImGuiDockNodeFlags_PassthruCentralNode);

    // ── Default layout ───────────────────────────────────────────────────────
    // firstLayout: only auto-build when there is no saved layout (imgui.ini).
    // m_resetLayout: user asked to reset — always rebuild from scratch.
    if (firstLayout && ImGui::DockBuilderGetNode(dockId) == nullptr)
    {
        firstLayout = false;
        BuildDefaultLayout(dockId, vp->Size.x, vp->Size.y - offsetY);
    }
    else if (m_resetLayout)
    {
        m_resetLayout = false;
        BuildDefaultLayout(dockId, vp->Size.x, vp->Size.y - offsetY);
    }

    ImGui::End();
}

void Editor::BuildDefaultLayout(unsigned int dockId, float availW, float availH)
{
    ImGui::DockBuilderRemoveNode(dockId);
    ImGui::DockBuilderAddNode  (dockId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockId, ImVec2(availW, availH));

    // Split: left panel (Outliner) | center (Viewport) | right panel (Details)
    ImGuiID left, center, right;
    ImGui::DockBuilderSplitNode(dockId,  ImGuiDir_Left,  0.17f, &left,   &center);
    ImGui::DockBuilderSplitNode(center,  ImGuiDir_Right, 0.24f, &right,  &center);

    // Split center bottom for Asset Browser
    ImGuiID centerTop, bottom;
    ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.26f, &bottom, &centerTop);

    // Split left panel vertically: Outliner (big) top | Camera (small) bottom
    ImGuiID leftTop, leftBot;
    ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.28f, &leftBot, &leftTop);

    ImGui::DockBuilderDockWindow("World Outliner", leftTop);
    ImGui::DockBuilderDockWindow("Camera",         leftBot);
    ImGui::DockBuilderDockWindow("Viewport",       centerTop);
    ImGui::DockBuilderDockWindow("Details",        right);
    ImGui::DockBuilderDockWindow("Stats",          right);  // stacked with Details
    ImGui::DockBuilderDockWindow("Asset Browser",  bottom);

    ImGui::DockBuilderFinish(dockId);
}

// ─────────────────────────────────────────────────────────────────────────────
//  DrawBuildGameModal
//  Shown when the user clicks Build > Build Game... or presses Ctrl+B.
//  Lets the user set game name, output .pak path, and initial scene, then
//  triggers Application::BuildPak via m_onBuildPak.
// ─────────────────────────────────────────────────────────────────────────────

void Editor::DrawBuildGameModal()
{
    // Drain background-thread log lines into the main-thread string
    {
        std::lock_guard<std::mutex> lk(m_buildLogMutex);
        if (!m_pendingLogLines.empty())
        {
            m_buildLog      += m_pendingLogLines;
            m_pendingLogLines.clear();
            m_buildLogDirty  = true;
        }
    }

    // OpenPopup must be called in the same window-stack context as BeginPopupModal.
    if (m_buildModalOpen)
    {
        ImGui::OpenPopup("Build Game##dlg");
        m_buildModalOpen = false;
    }

    ImGui::SetNextWindowSize(ImVec2(640, 700), ImGuiCond_Always);
    if (!ImGui::BeginPopupModal("Build Game##dlg",
                                nullptr,
                                ImGuiWindowFlags_NoResize))
        return;

    // ── Game Name ─────────────────────────────────────────────────────────
    ImGui::Text("Game Name:");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##bgname", m_buildGameName, sizeof(m_buildGameName));

    ImGui::Spacing();

    // ── Computed output path (read-only) ────────────────────────────────
    {
        namespace fs = std::filesystem;
        const std::string& wdir = Krayon::AssetManager::Get().GetWorkDir();
        const fs::path outPath  = fs::path(wdir.empty() ? "." : wdir).parent_path()
                                  / m_buildGameName / "Game.pak";
        ImGui::TextDisabled("Output: %s", outPath.string().c_str());
    }

    ImGui::Spacing();

    // ── Initial Scene picker ───────────────────────────────────────────────
    ImGui::Text("Initial Scene:");
    ImGui::SetNextItemWidth(-1);

    // Collect all registered .scene assets for the combo
    const auto& all = Krayon::AssetManager::Get().GetAll();
    std::vector<const Krayon::AssetMeta*> scenes;
    for (const auto& [guid, meta] : all)
        if (meta.type == Krayon::AssetType::Scene)
            scenes.push_back(&meta);
    std::sort(scenes.begin(), scenes.end(),
              [](const Krayon::AssetMeta* a, const Krayon::AssetMeta* b){
                  return a->path < b->path; });

    // Display combo
    const char* previewStr = (m_buildInitialScene[0] != '\0') ? m_buildInitialScene : "<None>";
    if (ImGui::BeginCombo("##bgscene", previewStr))
    {
        for (const auto* meta : scenes)
        {
            const bool sel = (meta->path == m_buildInitialScene);
            if (ImGui::Selectable(meta->path.c_str(), sel))
                std::strncpy(m_buildInitialScene, meta->path.c_str(), sizeof(m_buildInitialScene) - 1);
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ── Log ───────────────────────────────────────────────────────────────
    ImGui::Text("Build Log:");
    const float logH = ImGui::GetContentRegionAvail().y - 38.0f;
    ImGui::BeginChild("##buildlog", ImVec2(0, logH), true,
                      ImGuiWindowFlags_HorizontalScrollbar);
    if (!m_buildLog.empty())
        ImGui::TextUnformatted(m_buildLog.c_str());
    if (m_buildLogDirty)
    {
        ImGui::SetScrollHereY(1.0f);
        m_buildLogDirty = false;
    }
    ImGui::EndChild();

    ImGui::Spacing();

    // ── Buttons ───────────────────────────────────────────────────────────
    ImGui::BeginDisabled(m_buildRunning);

    if (ImGui::Button("Build", ImVec2(120, 0)))
    {
        if (m_onBuildPak)
        {
            m_buildRunning  = true;
            m_buildLog.clear();
            m_onBuildPak(m_buildOutPath, m_buildInitialScene, m_buildGameName);
            m_buildRunning = false;
        }
    }

    ImGui::EndDisabled();

    ImGui::SameLine();

    if (ImGui::Button("Close", ImVec2(120, 0)))
    {
        m_buildModalOpen = false;
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Prefab Editor
// ─────────────────────────────────────────────────────────────────────────────

void Editor::OpenPrefab(const std::string& absPath)
{
    m_prefabRegistry.Clear();
    Krayon::SceneSerializer::Load(absPath, m_prefabRegistry);
    m_prefabPath = absPath;
    m_prefabMode = true;
    hierarchy.SetSelected(NULL_ENTITY);
}

void Editor::ClosePrefab(bool save)
{
    if (save && !m_prefabPath.empty())
        Krayon::SceneSerializer::Save(m_prefabPath, m_prefabRegistry,
                                      std::filesystem::path(m_prefabPath).stem().string());
    m_prefabRegistry.Clear();
    m_prefabMode = false;
    m_prefabPath.clear();
    hierarchy.SetSelected(NULL_ENTITY);
}

void Editor::DrawPrefabBanner()
{
    if (!m_prefabMode) return;
    namespace fs = std::filesystem;
    const std::string stem = fs::path(m_prefabPath).filename().string();

    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetIO().DisplaySize.x, 36), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.92f);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration
                           | ImGuiWindowFlags_NoMove
                           | ImGuiWindowFlags_NoSavedSettings
                           | ImGuiWindowFlags_NoDocking
                           | ImGuiWindowFlags_NoNav;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.10f, 0.22f, 0.10f, 1.0f));
    if (ImGui::Begin("##prefab_banner", nullptr, flags))
    {
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3.0f);
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "PREFAB:");
        ImGui::SameLine();
        ImGui::Text("%s", stem.c_str());
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 230);
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.20f, 0.55f, 0.20f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.70f, 0.30f, 1.0f));
        if (ImGui::Button("Save Prefab", ImVec2(110, 0))) ClosePrefab(true);
        ImGui::PopStyleColor(2);
        ImGui::SameLine();
        if (ImGui::Button("<- Back to Scene", ImVec2(115, 0))) ClosePrefab(false);
    }
    ImGui::End();
    ImGui::PopStyleColor();
}

void Editor::DrawSavePrefabModal()
{
    if (!m_savePrefabModalOpen) return;
    ImGui::OpenPopup("Save as Prefab##modal");
    m_savePrefabModalOpen = false;

    if (ImGui::BeginPopupModal("Save as Prefab##modal", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Prefab name (saved into Content/prefabs/):");
        ImGui::SetNextItemWidth(320);
        ImGui::InputText("##pfbname", m_savePrefabAsName, sizeof(m_savePrefabAsName));

        ImGui::Spacing();
        if (ImGui::Button("Save", ImVec2(120, 0)))
        {
            const std::string wd = Krayon::AssetManager::Get().GetWorkDir();
            Registry* activeReg = m_prefabMode ? &m_prefabRegistry : m_lastSceneRegistry;
            if (!wd.empty() && m_savePrefabEntity != NULL_ENTITY && activeReg)
            {
                namespace fs = std::filesystem;
                fs::path prefabDir = fs::path(wd) / "prefabs";
                fs::create_directories(prefabDir);
                std::string stem = m_savePrefabAsName;
                if (stem.empty()) stem = "Prefab";
                fs::path outPath = prefabDir / (stem + ".prefab");
                Krayon::SceneSerializer::SavePrefab(m_savePrefabEntity, *activeReg, outPath.string());
                // Register the new .prefab with AssetManager (relative path)
                std::error_code errc;
                fs::path relPath = fs::relative(outPath, fs::path(wd), errc);
                if (!errc) {
                    std::string relStr = relPath.string();
                    std::replace(relStr.begin(), relStr.end(), '\\', '/');
                    Krayon::AssetManager::Get().Register(relStr);
                }
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0)))
            ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }
}

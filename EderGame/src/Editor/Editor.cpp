#include "Editor.h"
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imgui/imgui_impl_sdl3.h>
#include <imgui/imgui_impl_vulkan.h>
#include <imgui/ImGuizmo.h>
#include "Renderer/Vulkan/VulkanInstance.h"
#include "Renderer/Vulkan/VulkanSwapchain.h"

// ─────────────────────────────────────────────────────────────────────────────
// Theme
// ─────────────────────────────────────────────────────────────────────────────

void Editor::ApplyTheme()
{
    ImGuiStyle& s = ImGui::GetStyle();

    // ── Shape ────────────────────────────────────────────────────────────────
    s.WindowRounding    = 4.0f;
    s.ChildRounding     = 4.0f;
    s.FrameRounding     = 3.0f;
    s.PopupRounding     = 4.0f;
    s.ScrollbarRounding = 3.0f;
    s.GrabRounding      = 3.0f;
    s.TabRounding       = 4.0f;

    s.WindowBorderSize  = 1.0f;
    s.FrameBorderSize   = 0.0f;
    s.PopupBorderSize   = 1.0f;

    s.WindowPadding     = ImVec2(10.0f, 8.0f);
    s.FramePadding      = ImVec2(6.0f,  4.0f);
    s.ItemSpacing       = ImVec2(8.0f,  5.0f);
    s.ItemInnerSpacing  = ImVec2(5.0f,  4.0f);
    s.IndentSpacing     = 18.0f;
    s.ScrollbarSize     = 12.0f;
    s.GrabMinSize       = 8.0f;

    // ── Palette ──────────────────────────────────────────────────────────────
    // Blacks / dark grays
    const ImVec4 bg0   = ImVec4(0.08f, 0.08f, 0.08f, 1.00f); // deepest bg
    const ImVec4 bg1   = ImVec4(0.12f, 0.12f, 0.12f, 1.00f); // window bg
    const ImVec4 bg2   = ImVec4(0.16f, 0.16f, 0.16f, 1.00f); // child / header
    const ImVec4 bg3   = ImVec4(0.20f, 0.20f, 0.20f, 1.00f); // frame bg
    const ImVec4 bg4   = ImVec4(0.26f, 0.26f, 0.26f, 1.00f); // hovered frame
    const ImVec4 bg5   = ImVec4(0.32f, 0.32f, 0.32f, 1.00f); // active frame

    // Grays for borders / separators
    const ImVec4 border   = ImVec4(0.24f, 0.24f, 0.24f, 1.00f);
    const ImVec4 borderHv = ImVec4(0.40f, 0.40f, 0.40f, 1.00f);

    // Whites / text
    const ImVec4 textFull = ImVec4(0.92f, 0.92f, 0.92f, 1.00f);
    const ImVec4 textDim  = ImVec4(0.55f, 0.55f, 0.55f, 1.00f);

    // Red accent (Unreal-style)
    const ImVec4 accent   = ImVec4(0.80f, 0.10f, 0.10f, 1.00f);
    const ImVec4 accentHv = ImVec4(0.90f, 0.18f, 0.18f, 1.00f);
    const ImVec4 accentAc = ImVec4(0.65f, 0.06f, 0.06f, 1.00f);
    const ImVec4 accentDim = ImVec4(0.80f, 0.10f, 0.10f, 0.35f);

    // Tab active tint
    const ImVec4 tabAct   = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);

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

    c[ImGuiCol_Button]                = bg3;
    c[ImGuiCol_ButtonHovered]         = accentDim;
    c[ImGuiCol_ButtonActive]          = accent;

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
    stats      .Update(dt);
    cameraPanel.SetCamera(&cam);
    hierarchy  .SetRegistry(&registry);
    inspector  .SetRegistry(&registry);
    inspector  .SetSelected(hierarchy.GetSelected());

    DrawMenuBar();
    DrawToolbar();
    DrawDockspace();

    if (stats      .open) stats      .OnDraw();
    if (cameraPanel.open) cameraPanel.OnDraw();
    if (hierarchy  .open) hierarchy  .OnDraw();
    if (inspector  .open) inspector  .OnDraw();
    if (sceneView  .open)
    {
        ImVec2 svSize   = sceneView.GetDesiredSize();
        float  svAspect = (svSize.y > 0.0f) ? svSize.x / svSize.y : 1.0f;
        sceneView.OnDraw(gizmoMode, snapEnabled, snapValue,
                         cam.GetView(), cam.GetProjection(svAspect),
                         &registry, hierarchy.GetSelected());
    }
    if (showDemo)         ImGui::ShowDemoWindow(&showDemo);
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

void Editor::DrawMenuBar()
{
    if (!ImGui::BeginMainMenuBar()) return;

    if (ImGui::BeginMenu("File"))
    {
        if (ImGui::MenuItem("New Level",   "Ctrl+N")) {}
        if (ImGui::MenuItem("Open Level",  "Ctrl+O")) {}
        ImGui::Separator();
        if (ImGui::MenuItem("Save",        "Ctrl+S")) {}
        if (ImGui::MenuItem("Save As...",  "Ctrl+Shift+S")) {}
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
        ImGui::MenuItem("World Outliner", nullptr, &hierarchy  .open);
        ImGui::MenuItem("Details",        nullptr, &inspector  .open);
        ImGui::MenuItem("Viewport",       nullptr, &sceneView  .open);
        ImGui::MenuItem("Camera",         nullptr, &cameraPanel.open);
        ImGui::MenuItem("Stats",          nullptr, &stats      .open);
        ImGui::Separator();
        ImGui::MenuItem("ImGui Demo",     nullptr, &showDemo);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Build"))
    {
        ImGui::BeginDisabled();
        ImGui::MenuItem("Build All Levels");
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

    // Framerate on the right
    float fps = ImGui::GetIO().Framerate;
    char frStr[32];
    snprintf(frStr, sizeof(frStr), "%.0f FPS", fps);
    ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(frStr).x - 8.0f);
    ImGui::TextDisabled("%s", frStr);

    ImGui::EndMainMenuBar();
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

    // ── Play / Pause / Stop (center) ─────────────────────────────────────────
    float centerX = (vp->Size.x - 3.0f * (60.0f + 4.0f)) * 0.5f;
    ImGui::SameLine(centerX);

    // Play
    {
        bool playing = (playState == PlayState::Playing);
        if (playing) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.10f, 0.55f, 0.10f, 1.0f));
        if (ImGui::Button(playing ? "|| Play" : "> Play", ImVec2(60, 24)))
        {
            if (playState == PlayState::Stopped || playState == PlayState::Paused)
                playState = PlayState::Playing;
        }
        if (playing) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Play  [F5]");
    }
    ImGui::SameLine();

    // Pause
    {
        bool paused = (playState == PlayState::Paused);
        if (paused) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.65f, 0.50f, 0.05f, 1.0f));
        ImGui::BeginDisabled(playState == PlayState::Stopped);
        if (ImGui::Button("|| Pause", ImVec2(60, 24)))
        {
            if (playState == PlayState::Playing) playState = PlayState::Paused;
            else if (playState == PlayState::Paused) playState = PlayState::Playing;
        }
        ImGui::EndDisabled();
        if (paused) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pause  [F6]");
    }
    ImGui::SameLine();

    // Stop
    {
        ImGui::BeginDisabled(playState == PlayState::Stopped);
        if (ImGui::Button("[ ] Stop", ImVec2(60, 24)))
            playState = PlayState::Stopped;
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Stop  [F7]");
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

    // ── Default layout (runs only once on first startup) ─────────────────────
    if (firstLayout && ImGui::DockBuilderGetNode(dockId) == nullptr)
    {
        firstLayout = false;

        ImGui::DockBuilderRemoveNode(dockId);
        ImGui::DockBuilderAddNode  (dockId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockId, ImVec2(vp->Size.x, vp->Size.y - offsetY));

        // Split: left panel (Outliner) | center (Viewport) | right panel (Details)
        ImGuiID left, center, right;
        ImGui::DockBuilderSplitNode(dockId,  ImGuiDir_Left,  0.18f, &left,   &center);
        ImGui::DockBuilderSplitNode(center,  ImGuiDir_Right, 0.22f, &right,  &center);

        // Split left panel vertically: Outliner top | Camera bottom
        ImGuiID leftTop, leftBot;
        ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.35f, &leftBot, &leftTop);

        ImGui::DockBuilderDockWindow("World Outliner", leftTop);
        ImGui::DockBuilderDockWindow("Camera",         leftBot);
        ImGui::DockBuilderDockWindow("Viewport",       center);
        ImGui::DockBuilderDockWindow("Details",        right);
        ImGui::DockBuilderDockWindow("Stats",          right); // stacked with Details

        ImGui::DockBuilderFinish(dockId);
    }

    ImGui::End();
}

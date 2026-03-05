#include "HubApp.h"
#include "imgui_impl_sdlrenderer3.h"

#include <imgui/imgui.h>
#include <imgui/imgui_impl_sdl3.h>

#include <SDL3/SDL.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <chrono>
#include <ctime>
#include <algorithm>
#include <iostream>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#  include <shobjidl.h>
#  include <shlobj.h>
#endif

namespace fs = std::filesystem;

// ─────────────────────────────────────────────────────────────────────────────
//  Entry point
// ─────────────────────────────────────────────────────────────────────────────
int HubApp::Run()
{
    if (!Init()) return -1;
    MainLoop();
    Shutdown();
    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Init
// ─────────────────────────────────────────────────────────────────────────────
bool HubApp::Init()
{
#ifdef _WIN32
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
#endif

    SDL_Init(SDL_INIT_VIDEO);

    m_window = SDL_CreateWindow(
        "EderEngine Hub",
        960, 640,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!m_window) { std::cerr << "SDL_CreateWindow: " << SDL_GetError() << "\n"; return false; }

    m_renderer = SDL_CreateRenderer(m_window, nullptr);
    if (!m_renderer) { std::cerr << "SDL_CreateRenderer: " << SDL_GetError() << "\n"; return false; }

    SDL_SetRenderVSync(m_renderer, 1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;  // don't save imgui.ini

    ApplyTheme();

    ImGui_ImplSDL3_InitForSDLRenderer(m_window, m_renderer);
    ImGui_ImplSDLRenderer3_Init(m_renderer);

    LoadRegistry();

    // Populate default location for new projects (Documents)
    const char* home = getenv("USERPROFILE");
    if (!home) home = getenv("HOME");
    if (!home) home = "C:";
    std::string docs = std::string(home) + "/Documents/EderProjects";
    strncpy(m_newLocation, docs.c_str(), sizeof(m_newLocation) - 1);

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Shutdown
// ─────────────────────────────────────────────────────────────────────────────
void HubApp::Shutdown()
{
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(m_renderer);
    SDL_DestroyWindow(m_window);
    SDL_Quit();
#ifdef _WIN32
    CoUninitialize();
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
//  Main loop
// ─────────────────────────────────────────────────────────────────────────────
void HubApp::MainLoop()
{
    while (m_running)
    {
        SDL_Event ev;
        while (SDL_PollEvent(&ev))
        {
            ImGui_ImplSDL3_ProcessEvent(&ev);
            if (ev.type == SDL_EVENT_QUIT) m_running = false;
        }

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        DrawUI();

        ImGui::Render();
        SDL_SetRenderDrawColor(m_renderer, 14, 14, 14, 255);
        SDL_RenderClear(m_renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), m_renderer);
        SDL_RenderPresent(m_renderer);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Theme — dark red-accent matching the editor palette
// ─────────────────────────────────────────────────────────────────────────────
void HubApp::ApplyTheme()
{
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding   = 6.f;
    s.ChildRounding    = 6.f;
    s.FrameRounding    = 4.f;
    s.PopupRounding    = 6.f;
    s.GrabRounding     = 4.f;
    s.TabRounding      = 4.f;
    s.WindowBorderSize = 1.f;
    s.FrameBorderSize  = 0.f;
    s.PopupBorderSize  = 1.f;
    s.WindowPadding    = { 14.f, 12.f };
    s.FramePadding     = {  8.f,  5.f };
    s.ItemSpacing      = {  8.f,  6.f };
    s.ScrollbarSize    = 12.f;
    s.GrabMinSize      = 8.f;

    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg]          = { 0.09f, 0.09f, 0.09f, 1.f };
    c[ImGuiCol_ChildBg]           = { 0.12f, 0.12f, 0.12f, 1.f };
    c[ImGuiCol_PopupBg]           = { 0.11f, 0.11f, 0.11f, 0.98f };
    c[ImGuiCol_Border]            = { 0.24f, 0.24f, 0.24f, 1.f };
    c[ImGuiCol_FrameBg]           = { 0.17f, 0.17f, 0.17f, 1.f };
    c[ImGuiCol_FrameBgHovered]    = { 0.22f, 0.22f, 0.22f, 1.f };
    c[ImGuiCol_FrameBgActive]     = { 0.27f, 0.27f, 0.27f, 1.f };
    c[ImGuiCol_TitleBg]           = { 0.07f, 0.07f, 0.07f, 1.f };
    c[ImGuiCol_TitleBgActive]     = { 0.07f, 0.07f, 0.07f, 1.f };
    c[ImGuiCol_MenuBarBg]         = { 0.08f, 0.08f, 0.08f, 1.f };
    c[ImGuiCol_ScrollbarBg]       = { 0.09f, 0.09f, 0.09f, 1.f };
    c[ImGuiCol_ScrollbarGrab]     = { 0.30f, 0.30f, 0.30f, 1.f };
    c[ImGuiCol_ScrollbarGrabHovered]={ 0.40f, 0.40f, 0.40f, 1.f };
    c[ImGuiCol_ScrollbarGrabActive] = { 0.50f, 0.50f, 0.50f, 1.f };
    c[ImGuiCol_Button]            = { 0.20f, 0.20f, 0.20f, 1.f };
    c[ImGuiCol_ButtonHovered]     = { 0.78f, 0.17f, 0.17f, 1.f };
    c[ImGuiCol_ButtonActive]      = { 0.60f, 0.10f, 0.10f, 1.f };
    c[ImGuiCol_Header]            = { 0.78f, 0.17f, 0.17f, 0.35f };
    c[ImGuiCol_HeaderHovered]     = { 0.78f, 0.17f, 0.17f, 0.55f };
    c[ImGuiCol_HeaderActive]      = { 0.78f, 0.17f, 0.17f, 0.85f };
    c[ImGuiCol_Separator]         = { 0.24f, 0.24f, 0.24f, 1.f };
    c[ImGuiCol_SeparatorHovered]  = { 0.60f, 0.14f, 0.14f, 1.f };
    c[ImGuiCol_SeparatorActive]   = { 0.78f, 0.17f, 0.17f, 1.f };
    c[ImGuiCol_Tab]               = { 0.14f, 0.14f, 0.14f, 1.f };
    c[ImGuiCol_TabHovered]        = { 0.78f, 0.17f, 0.17f, 0.8f };
    c[ImGuiCol_TabSelected]       = { 0.78f, 0.17f, 0.17f, 1.f };
    c[ImGuiCol_Text]              = { 0.92f, 0.92f, 0.92f, 1.f };
    c[ImGuiCol_TextDisabled]      = { 0.45f, 0.45f, 0.45f, 1.f };
    c[ImGuiCol_ModalWindowDimBg]  = { 0.00f, 0.00f, 0.00f, 0.60f };
}

// ─────────────────────────────────────────────────────────────────────────────
//  Master draw
// ─────────────────────────────────────────────────────────────────────────────
void HubApp::DrawUI()
{
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({ 0.f, 0.f });
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("##hub_root", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoScrollbar);

    DrawTopBar();
    ImGui::Separator();

    DrawProjectGrid();

    // ── Open pending modals at root level (not inside BeginChild) ──────────
    if (m_showDeleteConfirm)
    {
        ImGui::OpenPopup("Confirmar eliminacion");
        m_showDeleteConfirm = false;
    }

    DrawNewProjectModal();
    DrawAddExistingModal();
    DrawDeleteConfirmModal();

    DrawStatusBar();

    ImGui::End();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Top bar
// ─────────────────────────────────────────────────────────────────────────────
void HubApp::DrawTopBar()
{
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.20f, 0.20f, 1.f));
    ImGui::SetWindowFontScale(1.4f);
    ImGui::Text("EDER");
    ImGui::SetWindowFontScale(1.f);
    ImGui::PopStyleColor();
    ImGui::SameLine(0.f, 4.f);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.f);
    ImGui::SetWindowFontScale(1.4f);
    ImGui::TextUnformatted("ENGINE");
    ImGui::SetWindowFontScale(1.f);
    ImGui::SameLine(0.f, 10.f);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 7.f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.40f, 0.40f, 0.40f, 1.f));
    ImGui::TextUnformatted("HUB");
    ImGui::PopStyleColor();

    // Right-aligned action buttons
    const float btnW  = 140.f;
    const float btnH  = 28.f;
    const float btnGap = 8.f;
    float rightX = ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX()
                   - btnW * 2.f - btnGap;
    ImGui::SameLine(rightX);
    ImGui::SetCursorPosY(4.f);

    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.78f, 0.17f, 0.17f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.00f, 0.30f, 0.30f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.55f, 0.10f, 0.10f, 1.f));
    if (ImGui::Button("+  Nuevo Proyecto", { btnW, btnH }))
    {
        memset(m_newName, 0, sizeof(m_newName));
        m_showNew = true;
        ImGui::OpenPopup("New Project");
    }
    ImGui::PopStyleColor(3);

    ImGui::SameLine(0.f, btnGap);
    ImGui::SetCursorPosY(4.f);
    if (ImGui::Button("Agregar Existente", { btnW, btnH }))
    {
        memset(m_existingPath, 0, sizeof(m_existingPath));
        memset(m_existingName, 0, sizeof(m_existingName));
        m_showAddExisting = true;
        ImGui::OpenPopup("Add Existing Project");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Project grid
// ─────────────────────────────────────────────────────────────────────────────
void HubApp::DrawProjectGrid()
{
    ImGui::BeginChild("##grid", { 0.f, -26.f }, false);

    if (m_projects.empty())
    {
        ImVec2 avail = ImGui::GetContentRegionAvail();
        float textW = 420.f;
        ImGui::SetCursorPos({ (avail.x - textW) * 0.5f, avail.y * 0.38f });
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.35f, 0.35f, 0.35f, 1.f));
        ImGui::SetWindowFontScale(1.1f);
        ImGui::TextUnformatted("No hay proyectos todavia.");
        ImGui::SetWindowFontScale(1.f);
        ImGui::SetCursorPos({ (avail.x - textW) * 0.5f, ImGui::GetCursorPosY() + 8.f });
        ImGui::TextUnformatted("Usa  [ +  Nuevo Proyecto ]  para empezar.");
        ImGui::PopStyleColor();
        ImGui::EndChild();
        return;
    }

    const float cardW   = 262.f;
    const float cardH   = 152.f;
    const float padding = 16.f;
    const float avail_w = ImGui::GetContentRegionAvail().x;
    int cols = std::max(1, static_cast<int>((avail_w + padding) / (cardW + padding)));

    int openIdx   = -1;  // deferred — must be executed outside BeginChild
    int deleteIdx = -1;

    int col = 0;
    for (int i = 0; i < static_cast<int>(m_projects.size()); ++i)
    {
        const ProjectEntry& p = m_projects[i];
        ImGui::PushID(i);

        if (col > 0) ImGui::SameLine(0.f, padding);

        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.14f, 0.14f, 0.14f, 1.f));
        ImGui::BeginChild("##card", { cardW, cardH }, true, ImGuiWindowFlags_NoScrollbar);

        bool cardHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);

        // ── Coloured top stripe ───────────────────────────────────────────
        {
            ImDrawList* dl  = ImGui::GetWindowDrawList();
            ImVec2      wp  = ImGui::GetWindowPos();
            dl->AddRectFilled(wp, { wp.x + cardW, wp.y + 5.f },
                IM_COL32(200, 44, 44, 255));
            // hover border
            if (cardHovered)
                dl->AddRect(wp, { wp.x + cardW, wp.y + cardH },
                    IM_COL32(200, 44, 44, 160), 6.f, 0, 1.5f);
        }

        // ── Delete "x" button — top-right ────────────────────────────────
        ImGui::SetCursorPos({ cardW - 24.f, 7.f });
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.f,   0.f,   0.f,   0.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.70f, 0.13f, 0.13f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.45f, 0.07f, 0.07f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(0.55f, 0.55f, 0.55f, 1.f));
        if (ImGui::Button("x##del", { 19.f, 17.f }))
            deleteIdx = i;
        ImGui::PopStyleColor(4);

        // ── Project name ──────────────────────────────────────────────────
        ImGui::SetCursorPos({ 10.f, 14.f });
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.95f, 0.95f, 1.f));
        ImGui::SetWindowFontScale(1.10f);
        ImGui::PushTextWrapPos(cardW - 30.f);
        ImGui::TextUnformatted(p.name.c_str());
        ImGui::PopTextWrapPos();
        ImGui::SetWindowFontScale(1.f);
        ImGui::PopStyleColor();

        // ── Path ─────────────────────────────────────────────────────────
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3.f);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.46f, 0.46f, 0.46f, 1.f));
        const std::string& cp = p.contentPath;
        std::string display = cp.size() > 36 ? "..." + cp.substr(cp.size() - 33) : cp;
        ImGui::TextUnformatted(display.c_str());
        ImGui::PopStyleColor();

        // ── Last opened ───────────────────────────────────────────────────
        if (!p.lastOpened.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.34f, 0.34f, 0.34f, 1.f));
            ImGui::Text("Modificado: %s", p.lastOpened.c_str());
            ImGui::PopStyleColor();
        }

        // ── Open button — full width at bottom ────────────────────────────
        ImGui::SetCursorPos({ 10.f, cardH - 36.f });
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.78f, 0.17f, 0.17f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f,  0.30f, 0.30f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.55f, 0.10f, 0.10f, 1.f));
        if (ImGui::Button("Abrir proyecto  >", { cardW - 20.f, 28.f }))
            openIdx = i;
        ImGui::PopStyleColor(3);

        ImGui::EndChild();
        ImGui::PopStyleColor(); // ChildBg
        ImGui::PopID();

        ++col;
        if (col >= cols) col = 0;
    }

    ImGui::EndChild();

    // ── Deferred actions — executed outside any BeginChild ────────────────
    if (openIdx >= 0)
        OpenProject(openIdx);

    if (deleteIdx >= 0)
    {
        m_selectedIdx       = deleteIdx;
        m_showDeleteConfirm = true;   // DrawUI will call OpenPopup on next frame
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  New Project modal
// ─────────────────────────────────────────────────────────────────────────────
void HubApp::DrawNewProjectModal()
{
    ImGui::SetNextWindowSize({ 460.f, 0.f }, ImGuiCond_Always);
    if (!ImGui::BeginPopupModal("New Project", nullptr,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove))
        return;

    ImGui::Text("Nombre del proyecto");
    ImGui::SetNextItemWidth(-1.f);
    ImGui::InputText("##pname", m_newName, sizeof(m_newName));

    ImGui::Spacing();
    ImGui::Text("Ubicacion (carpeta padre)");
    ImGui::SetNextItemWidth(-90.f);
    ImGui::InputText("##ploc", m_newLocation, sizeof(m_newLocation));
    ImGui::SameLine();
    if (ImGui::Button("Buscar##loc", { 80.f, 0.f }))
    {
        std::string chosen = BrowseFolder("Selecciona la carpeta del proyecto");
        if (!chosen.empty())
            strncpy(m_newLocation, chosen.c_str(), sizeof(m_newLocation) - 1);
    }

    // Preview full path
    if (m_newName[0] != '\0' && m_newLocation[0] != '\0')
    {
        std::string preview = (fs::path(m_newLocation) / m_newName / "Content").string();
        ImGui::Spacing();
        ImGui::TextDisabled("Content: %s", preview.c_str());
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    bool canCreate = m_newName[0] != '\0' && m_newLocation[0] != '\0';
    if (!canCreate) ImGui::BeginDisabled();
    if (ImGui::Button("Crear", { 100.f, 0.f }))
    {
        CreateProject();
        ImGui::CloseCurrentPopup();
    }
    if (!canCreate) ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancelar", { 100.f, 0.f }))
        ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Add Existing Project modal
// ─────────────────────────────────────────────────────────────────────────────
void HubApp::DrawAddExistingModal()
{
    ImGui::SetNextWindowSize({ 460.f, 0.f }, ImGuiCond_Always);
    if (!ImGui::BeginPopupModal("Add Existing Project", nullptr,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove))
        return;

    ImGui::Text("Nombre para mostrar");
    ImGui::SetNextItemWidth(-1.f);
    ImGui::InputText("##ename", m_existingName, sizeof(m_existingName));

    ImGui::Spacing();
    ImGui::Text("Ruta al Content/ del proyecto");
    ImGui::SetNextItemWidth(-90.f);
    ImGui::InputText("##epath", m_existingPath, sizeof(m_existingPath));
    ImGui::SameLine();
    if (ImGui::Button("Buscar##epath", { 80.f, 0.f }))
    {
        std::string chosen = BrowseFolder("Selecciona la carpeta Content del proyecto");
        if (!chosen.empty())
            strncpy(m_existingPath, chosen.c_str(), sizeof(m_existingPath) - 1);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    bool canAdd = m_existingName[0] != '\0' && m_existingPath[0] != '\0';
    if (!canAdd) ImGui::BeginDisabled();
    if (ImGui::Button("Agregar", { 100.f, 0.f }))
    {
        AddExistingProject();
        ImGui::CloseCurrentPopup();
    }
    if (!canAdd) ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancelar", { 100.f, 0.f }))
        ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Delete confirmation modal
// ─────────────────────────────────────────────────────────────────────────────
void HubApp::DrawDeleteConfirmModal()
{
    ImGui::SetNextWindowSize({ 360.f, 0.f }, ImGuiCond_Always);
    if (!ImGui::BeginPopupModal("Confirmar eliminacion", nullptr,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove))
        return;

    if (m_selectedIdx >= 0 && m_selectedIdx < static_cast<int>(m_projects.size()))
        ImGui::Text("Eliminar \"%s\" de la lista?\n(Los archivos NO se borran del disco)",
            m_projects[m_selectedIdx].name.c_str());

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.78f, 0.17f, 0.17f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f,  0.30f, 0.30f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.55f, 0.10f, 0.10f, 1.f));
    if (ImGui::Button("Eliminar", { 100.f, 0.f }))
    {
        DeleteProject(m_selectedIdx);
        ImGui::CloseCurrentPopup();
    }
    ImGui::PopStyleColor(3);
    ImGui::SameLine();
    if (ImGui::Button("Cancelar", { 100.f, 0.f }))
        ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Status bar
// ─────────────────────────────────────────────────────────────────────────────
void HubApp::DrawStatusBar()
{
    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 22.f - ImGui::GetStyle().WindowPadding.y);
    ImGui::Separator();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.38f, 0.38f, 0.38f, 1.f));
    if (!m_status.empty())
    {
        ImGui::TextUnformatted(m_status.c_str());
        // Auto-clear after ~3 seconds (approximate via frame check isn't ideal;
        // we just leave the last message — user can always create/open to update)
    }
    else
    {
        ImGui::Text("%zu proyecto(s) registrado(s)", m_projects.size());
    }
    ImGui::PopStyleColor();
    // Version tag right-aligned
    const char* ver = "v0.1";
    float verW = ImGui::CalcTextSize(ver).x;
    ImGui::SameLine(ImGui::GetWindowWidth() - verW - ImGui::GetStyle().WindowPadding.x * 2.f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.25f, 0.25f, 0.25f, 1.f));
    ImGui::TextUnformatted(ver);
    ImGui::PopStyleColor();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Project operations
// ─────────────────────────────────────────────────────────────────────────────
void HubApp::CreateProject()
{
    std::string name    = m_newName;
    std::string rootDir = (fs::path(m_newLocation) / name).string();
    std::string content = (fs::path(rootDir) / "Content").string();

    // Create folder structure
    try
    {
        fs::create_directories(content + "/scenes");
        fs::create_directories(content + "/scripts");
        fs::create_directories(content + "/materials");
        fs::create_directories(content + "/textures");
        fs::create_directories(content + "/meshes");
        fs::create_directories(content + "/shaders");
    }
    catch (const std::exception& e)
    {
        m_status = std::string("Error al crear carpetas: ") + e.what();
        return;
    }

    // Copy engine shaders + default assets into the new project
    const fs::path gameContent = fs::current_path() / "Game" / "Content";
    const struct { fs::path src; fs::path dst; } copies[] = {
        { gameContent / "shaders", fs::path(content) / "shaders" },
        { gameContent / "meshes",  fs::path(content) / "meshes"  },
    };
    for (const auto& c : copies)
    {
        try
        {
            if (fs::exists(c.src))
                fs::copy(c.src, c.dst,
                         fs::copy_options::recursive | fs::copy_options::overwrite_existing);
        }
        catch (const std::exception& e)
        {
            // Non-fatal
            std::cerr << "[Hub] Advertencia al copiar '" << c.src.string() << "': " << e.what() << "\n";
        }
    }

    ProjectEntry p;
    p.name        = name;
    p.contentPath = content;
    p.lastOpened  = CurrentDateStr();
    m_projects.push_back(p);
    SaveRegistry();

    m_status = "Proyecto '" + name + "' creado.";

    // Open immediately
    LaunchEditor(content, name);
}

void HubApp::OpenProject(int idx)
{
    if (idx < 0 || idx >= static_cast<int>(m_projects.size())) return;
    m_projects[idx].lastOpened = CurrentDateStr();
    SaveRegistry();
    LaunchEditor(m_projects[idx].contentPath, m_projects[idx].name);
    m_status = "Abriendo: " + m_projects[idx].name;
}

void HubApp::DeleteProject(int idx)
{
    if (idx < 0 || idx >= static_cast<int>(m_projects.size())) return;
    m_status = "'" + m_projects[idx].name + "' eliminado de la lista.";
    m_projects.erase(m_projects.begin() + idx);
    m_selectedIdx = -1;
    SaveRegistry();
}

void HubApp::AddExistingProject()
{
    ProjectEntry p;
    p.name        = m_existingName;
    p.contentPath = m_existingPath;
    m_projects.push_back(p);
    SaveRegistry();
    m_status = "Proyecto '" + p.name + "' agregado.";
}

// ─────────────────────────────────────────────────────────────────────────────
//  Registry persistence  (APPDATA/EderEngine/projects.txt)
//  Format per line:  name|contentPath|lastOpened
// ─────────────────────────────────────────────────────────────────────────────
std::string HubApp::GetRegistryPath() const
{
    std::string base;
#ifdef _WIN32
    const char* appdata = getenv("APPDATA");
    base = appdata ? std::string(appdata) + "/EderEngine" : ".";
#else
    const char* home = getenv("HOME");
    base = home ? std::string(home) + "/.edengine" : ".";
#endif
    fs::create_directories(base);
    return base + "/projects.txt";
}

void HubApp::LoadRegistry()
{
    m_projects.clear();
    std::ifstream f(GetRegistryPath());
    if (!f) return;
    std::string line;
    while (std::getline(f, line))
    {
        if (line.empty()) continue;
        auto p1 = line.find('|');
        if (p1 == std::string::npos) continue;
        auto p2 = line.find('|', p1 + 1);

        ProjectEntry e;
        e.name        = line.substr(0, p1);
        e.contentPath = (p2 == std::string::npos)
                        ? line.substr(p1 + 1)
                        : line.substr(p1 + 1, p2 - p1 - 1);
        if (p2 != std::string::npos)
            e.lastOpened = line.substr(p2 + 1);
        m_projects.push_back(e);
    }
}

void HubApp::SaveRegistry()
{
    std::ofstream f(GetRegistryPath(), std::ios::trunc);
    for (const auto& p : m_projects)
        f << p.name << '|' << p.contentPath << '|' << p.lastOpened << '\n';
}

// ─────────────────────────────────────────────────────────────────────────────
//  Helpers
// ─────────────────────────────────────────────────────────────────────────────
std::string HubApp::GetEditorExePath() const
{
#ifdef _WIN32
    char exeBuf[MAX_PATH];
    GetModuleFileNameA(nullptr, exeBuf, MAX_PATH);
    fs::path exeDir = fs::path(exeBuf).parent_path();
#else
    fs::path exeDir = fs::current_path();
#endif
    // Development layout: EderHub/build/ → EderGame/build/EderGame.exe
    auto devPath = (exeDir / ".." / ".." / "EderGame" / "build" / "EderGame.exe")
                   .lexically_normal();
    if (fs::exists(devPath)) return devPath.string();

    // Installed layout: same directory
    auto samePath = exeDir / "EderGame.exe";
    if (fs::exists(samePath)) return samePath.string();

    return {};
}

void HubApp::LaunchEditor(const std::string& contentPath,
                          const std::string& projectName) const
{
    std::string editorExe = GetEditorExePath();
    if (editorExe.empty())
    {
        // fallback: try same dir
        editorExe = "EderGame.exe";
    }

#ifdef _WIN32
    std::string cmd = "\"" + editorExe + "\""
                    + " --workdir \"" + contentPath + "\""
                    + " --project \"" + projectName + "\"";

    STARTUPINFOA        si = {};  si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    CreateProcessA(nullptr, cmd.data(), nullptr, nullptr,
                   FALSE, 0, nullptr, nullptr, &si, &pi);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
#else
    std::string cmd = "\"" + editorExe + "\""
                    + " --workdir \"" + contentPath + "\""
                    + " --project \"" + projectName + "\" &";
    system(cmd.c_str());
#endif
}

std::string HubApp::BrowseFolder(const char* /*title*/) const
{
#ifdef _WIN32
    std::string result;
    IFileOpenDialog* pfd = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr,
            CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd))))
    {
        DWORD opts; pfd->GetOptions(&opts);
        pfd->SetOptions(opts | FOS_PICKFOLDERS | FOS_PATHMUSTEXIST);
        if (SUCCEEDED(pfd->Show(nullptr)))
        {
            IShellItem* item = nullptr;
            if (SUCCEEDED(pfd->GetResult(&item)))
            {
                PWSTR wpath = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &wpath)))
                {
                    int len = WideCharToMultiByte(CP_UTF8, 0, wpath, -1,
                                                  nullptr, 0, nullptr, nullptr);
                    result.resize(len - 1);
                    WideCharToMultiByte(CP_UTF8, 0, wpath, -1,
                                        result.data(), len, nullptr, nullptr);
                    CoTaskMemFree(wpath);
                }
                item->Release();
            }
        }
        pfd->Release();
    }
    return result;
#else
    return {};
#endif
}

std::string HubApp::CurrentDateStr() const
{
    auto  now = std::chrono::system_clock::now();
    time_t t  = std::chrono::system_clock::to_time_t(now);
    struct tm tm_info = {};
#ifdef _WIN32
    localtime_s(&tm_info, &t);
#else
    localtime_r(&t, &tm_info);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm_info);
    return buf;
}

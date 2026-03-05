#pragma once
#include <string>
#include <vector>
#include <SDL3/SDL.h>

// ─────────────────────────────────────────────────────────────────────────────
//  Project entry — one entry in the hub registry
// ─────────────────────────────────────────────────────────────────────────────
struct ProjectEntry
{
    std::string name;         // display name
    std::string contentPath;  // absolute path to the project Content/ folder
    std::string lastOpened;   // ISO date string, empty if never opened
};

// ─────────────────────────────────────────────────────────────────────────────
//  HubApp  — project manager window
// ─────────────────────────────────────────────────────────────────────────────
class HubApp
{
public:
    int Run();

private:
    // ── Lifecycle ────────────────────────────────────────────────────────────
    bool Init();
    void Shutdown();
    void MainLoop();

    // ── UI ───────────────────────────────────────────────────────────────────
    void ApplyTheme();
    void DrawUI();
    void DrawTopBar();
    void DrawProjectGrid();
    void DrawNewProjectModal();
    void DrawAddExistingModal();
    void DrawDeleteConfirmModal();
    void DrawStatusBar();

    // ── Project operations ───────────────────────────────────────────────────
    void LoadRegistry();
    void SaveRegistry();
    void CreateProject();
    void OpenProject(int idx);
    void DeleteProject(int idx);
    void AddExistingProject();

    // ── Helpers ──────────────────────────────────────────────────────────────
    std::string GetRegistryPath() const;
    std::string GetEditorExePath() const;
    std::string BrowseFolder(const char* title) const;
    std::string CurrentDateStr() const;
    void        LaunchEditor(const std::string& contentPath,
                             const std::string& projectName) const;

    // ── SDL / ImGui state ────────────────────────────────────────────────────
    SDL_Window*   m_window   = nullptr;
    SDL_Renderer* m_renderer = nullptr;
    bool          m_running  = true;

    // ── Project list ─────────────────────────────────────────────────────────
    std::vector<ProjectEntry> m_projects;
    int  m_selectedIdx        = -1;  // for delete confirmation

    // ── New-project dialog ───────────────────────────────────────────────────
    bool m_showNew    = false;
    char m_newName[256]  = {};
    char m_newLocation[512] = {};

    // ── Add-existing dialog ──────────────────────────────────────────────────
    bool m_showAddExisting  = false;
    char m_existingPath[512] = {};
    char m_existingName[256] = {};

    // ── Delete confirm dialog ────────────────────────────────────────────────
    bool m_showDeleteConfirm = false;

    // ── Status bar ───────────────────────────────────────────────────────────
    std::string m_status;
};

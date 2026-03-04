#pragma once
#include "Panel.h"
#include <IO/AssetManager.h>
#include <string>
#include <vector>
#include <functional>
#include <filesystem>

// ─────────────────────────────────────────────────────────────────────────────
//  AssetBrowserPanel
//
//  Left  : Folder tree  (all subdirs of workDir, collapsible)
//  Right : Content grid (folders + assets in the selected directory)
//
//  Operations available via right-click context menus:
//    Folders : Create Sub-folder | Rename | Delete (empty only, or force)
//    Files   : Rename | Delete | [future: Drag-to-move]
//
//  All mutations go through AssetManager so GUIDs are preserved across
//  renames and moves (Unity-style sidecar .data files travel with the asset).
// ─────────────────────────────────────────────────────────────────────────────

class AssetBrowserPanel : public Panel
{
public:
    // ── Panel interface ───────────────────────────────────────────
    const char* Title() const override { return "Asset Browser"; }
    void        OnDraw()      override;

    // ── Optional callback: user double-clicked an asset ───────────
    // Signature: void(uint64_t guid, const Krayon::AssetMeta&)
    using SelectCallback = std::function<void(uint64_t, const Krayon::AssetMeta&)>;
    void SetSelectCallback(SelectCallback cb) { m_onSelect = std::move(cb); }

private:
    // ── Drawing helpers ───────────────────────────────────────────
    void DrawTree();
    void DrawContent();
    void DrawBreadcrumb();

    void DrawFolderNode(const std::filesystem::path& absDir, int depth);
    void DrawContextMenuFolder(const std::string& relDir);
    void DrawContextMenuFile(uint64_t guid, const Krayon::AssetMeta& meta);

    // Inline rename popup
    void OpenRename(uint64_t guid, const std::string& currentStem);
    void OpenFolderRename(const std::string& relDir);
    void DrawRenamePopup();

    // New-folder modal
    void OpenNewFolder(const std::string& parentRelDir);
    void DrawNewFolderPopup();

    // ── State ─────────────────────────────────────────────────────
    std::string m_selectedDir;          // relative to workDir (empty = root)

    // Rename state
    bool        m_renameOpen        = false;
    bool        m_renamingFolder    = false;
    uint64_t    m_renameGuid        = 0;
    std::string m_renameFolderPath;     // for folder renames
    char        m_renameBuffer[256]  = {};

    // New-folder state
    bool        m_newFolderOpen     = false;
    std::string m_newFolderParent;
    char        m_newFolderBuffer[128] = {};

    // Drag-and-drop
    uint64_t    m_dragGuid          = 0;

    // Confirm-delete state
    bool        m_confirmDeleteOpen = false;
    bool        m_deletingFolder    = false;
    uint64_t    m_deleteGuid        = 0;
    std::string m_deleteFolderPath;

    SelectCallback m_onSelect;

    // ── Helpers ───────────────────────────────────────────────────
    static const char* IconForType(Krayon::AssetType t);
    std::string        WorkDir() const;
    // Collect immediate subdirs of a directory
    std::vector<std::filesystem::path> SubDirs(const std::filesystem::path& dir) const;
    // Collect items (dirs + asset files) inside a directory for the content pane
    struct ContentItem {
        bool        isDir  = false;
        uint64_t    guid   = 0;
        std::string name;
        std::string relPath;    // relative to workDir
        std::string ext;        // lowercase extension with dot (e.g. ".fbx")
        Krayon::AssetType type = Krayon::AssetType::Unknown;
    };
    std::vector<ContentItem> ContentOf(const std::string& relDir) const;
};

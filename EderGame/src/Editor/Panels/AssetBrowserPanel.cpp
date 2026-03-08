#include "AssetBrowserPanel.h"
#include <imgui/imgui.h>
#include <iostream>
#include <sstream>
#include <fstream>
#include <algorithm>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace fs = std::filesystem;
using namespace Krayon;

// ─────────────────────────────────────────────────────────────────────────────
//  Helpers
// ─────────────────────────────────────────────────────────────────────────────

std::string AssetBrowserPanel::WorkDir() const
{
    return AssetManager::Get().GetWorkDir();
}

const char* AssetBrowserPanel::IconForType(AssetType t)
{
    switch (t)
    {
        case AssetType::Mesh:     return "[Mesh]";
        case AssetType::Texture:  return "[Tex] ";
        case AssetType::Audio:    return "[Aud] ";
        case AssetType::Shader:   return "[Shd] ";
        case AssetType::Data:     return "[Dat] ";
        case AssetType::PAK:      return "[PAK] ";
        case AssetType::Material: return "[Mat] ";
        case AssetType::Scene:    return "[Scn] ";
        case AssetType::Script:   return "[Lua] ";
        default:                  return "[?]   ";
    }
}

std::vector<fs::path> AssetBrowserPanel::SubDirs(const fs::path& dir) const
{
    std::vector<fs::path> out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec))
        if (e.is_directory(ec))
            out.push_back(e.path());
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<AssetBrowserPanel::ContentItem>
AssetBrowserPanel::ContentOf(const std::string& relDir) const
{
    const std::string wd  = WorkDir();
    if (wd.empty()) return {};

    fs::path absDir = relDir.empty() ? fs::path(wd) : (fs::path(wd) / relDir);

    std::vector<ContentItem> out;
    std::error_code ec;

    for (const auto& e : fs::directory_iterator(absDir, ec))
    {
        ContentItem item;

        if (e.is_directory(ec))
        {
            item.isDir   = true;
            item.name    = e.path().filename().string();
            fs::path rel = fs::relative(e.path(), wd, ec);
            // normalize
            std::string r = rel.string();
            std::replace(r.begin(), r.end(), '\\', '/');
            item.relPath = r;
            out.push_back(item);
        }
        else if (e.is_regular_file(ec))
        {
            std::string ext = e.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
            if (ext == ".data") continue;   // skip sidecars
            AssetType t = DetectTypeByExtension(ext);
            if (t == AssetType::Unknown) continue;
            // Show only compiled shaders (.spv); skip source files
            if (t == AssetType::Shader && ext != ".spv") continue;

            fs::path rel = fs::relative(e.path(), wd, ec);
            std::string r = rel.string();
            std::replace(r.begin(), r.end(), '\\', '/');
            // lowercase for AM lookup
            std::string rn = r;
            std::transform(rn.begin(), rn.end(), rn.begin(),
                           [](unsigned char c){ return static_cast<char>(std::tolower(c)); });

            const AssetMeta* meta = AssetManager::Get().Find(rn);
            if (!meta)
            {
                // File exists on disk but isn't registered yet — auto-register it
                uint64_t guid = AssetManager::Get().Register(rn);
                meta = AssetManager::Get().FindByGuid(guid);
            }
            if (!meta) continue;

            item.isDir   = false;
            item.guid    = meta->guid;
            item.name    = e.path().stem().string();
            item.relPath = rn;
            item.ext     = ext;
            item.type    = meta->type;
            out.push_back(item);
        }
    }

    // Sort: dirs first, then files, alphabetical within each group
    std::sort(out.begin(), out.end(), [](const ContentItem& a, const ContentItem& b){
        if (a.isDir != b.isDir) return a.isDir > b.isDir;
        return a.name < b.name;
    });

    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
//  External drop: copy file into workDir and register it
// ─────────────────────────────────────────────────────────────────────────────

void AssetBrowserPanel::HandleExternalDrop(const std::string& absSourcePath,
                                           const std::string& targetRelDir)
{
    const std::string wd = WorkDir();
    if (wd.empty()) return;

    // SDL3 provides paths as UTF-8; use u8path so Windows handles non-ASCII correctly
    fs::path src = fs::u8path(absSourcePath);
    std::error_code ec;
    if (!fs::exists(src, ec))
    {
        std::cerr << "[AssetBrowser] Source not found: " << absSourcePath << "\n";
        return;
    }

    // Only accept known asset types
    std::string ext = src.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    if (DetectTypeByExtension(ext) == AssetType::Unknown) return;

    fs::path targetDir = targetRelDir.empty()
        ? fs::path(wd)
        : (fs::path(wd) / targetRelDir);

    fs::create_directories(targetDir, ec);

    // Use lowercased filename so the registered path is predictable
    std::string stemLower = src.stem().string();
    std::transform(stemLower.begin(), stemLower.end(), stemLower.begin(),
                   [](unsigned char c){ return static_cast<char>(std::tolower(c)); });

    fs::path dst = targetDir / (stemLower + ext);

    // Avoid overwriting: append _1, _2 … until unique
    if (fs::exists(dst, ec))
    {
        int n = 1;
        do {
            dst = targetDir / (stemLower + "_" + std::to_string(n++) + ext);
        } while (fs::exists(dst, ec));
    }

    fs::copy_file(src, dst, ec);
    if (ec)
    {
        std::cerr << "[AssetBrowser] Copy failed: " << ec.message() << "\n";
        std::cerr << "[AssetBrowser]   src = " << fs::absolute(src) << "\n";
        std::cerr << "[AssetBrowser]   dst = " << fs::absolute(dst) << "\n";
        return;
    }

    std::cout << "[AssetBrowser] Copied to: " << fs::absolute(dst) << "\n";

    // Build a clean lowercase relative path so Find() always matches
    fs::path rel = fs::relative(dst, fs::absolute(wd), ec);
    std::string relStr = rel.string();
    std::replace(relStr.begin(), relStr.end(), '\\', '/');
    std::transform(relStr.begin(), relStr.end(), relStr.begin(),
                   [](unsigned char c){ return static_cast<char>(std::tolower(c)); });

    AssetManager::Get().Register(relStr);
    std::cout << "[AssetBrowser] Imported: " << relStr << "\n";
}

void AssetBrowserPanel::QueueImport(const std::vector<std::string>& paths)
{
    // Capture the current folder at drop time so files always land where expected
    for (const auto& p : paths)
        m_importQueue.push_back({ p, m_selectedDir });
    m_importTotal += (int)paths.size();
}

void AssetBrowserPanel::DrawImportProgress()
{
    if (m_importTotal <= 0) return;

    ImGui::OpenPopup("##import_progress");
    ImGui::SetNextWindowSize(ImVec2(340, 100), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("##import_progress", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove))
    {
        ImGui::Text("Importing assets...");
        ImGui::Spacing();

        char label[32];
        snprintf(label, sizeof(label), "%d / %d", m_importDone, m_importTotal);
        float frac = m_importTotal > 0 ? (float)m_importDone / (float)m_importTotal : 0.f;
        ImGui::ProgressBar(frac, ImVec2(-1.f, 0.f), label);

        if (m_importDone >= m_importTotal)
        {
            ImGui::CloseCurrentPopup();
            m_importTotal = 0;
            m_importDone  = 0;
        }
        ImGui::EndPopup();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  File watcher  (Windows ReadDirectoryChangesW, background thread)
// ─────────────────────────────────────────────────────────────────────────────

void AssetBrowserPanel::StartFileWatcher()
{
    m_watchStop  = false;
    m_watchThread = std::thread([this]{ FileWatcherThread(); });
}

void AssetBrowserPanel::StopFileWatcher()
{
    m_watchStop = true;
    if (m_watchThread.joinable())
        m_watchThread.join();
}

void AssetBrowserPanel::FileWatcherThread()
{
#ifdef _WIN32
    const std::string wd = WorkDir();
    if (wd.empty()) return;

    fs::path wdPath(wd);
    HANDLE hDir = CreateFileW(
        wdPath.wstring().c_str(),
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
        nullptr
    );
    if (hDir == INVALID_HANDLE_VALUE) return;

    HANDLE hEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!hEvent) { CloseHandle(hDir); return; }

    OVERLAPPED ov{};
    ov.hEvent = hEvent;

    constexpr DWORD kBufSize = 8192;
    alignas(DWORD) char buf[kBufSize];

    // Issue first async request
    ReadDirectoryChangesW(
        hDir, buf, kBufSize, /*bWatchSubtree=*/TRUE,
        FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
        FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE,
        nullptr, &ov, nullptr
    );

    while (!m_watchStop)
    {
        DWORD wait = WaitForSingleObject(hEvent, 300);
        if (wait == WAIT_OBJECT_0)
        {
            DWORD bytes = 0;
            if (GetOverlappedResult(hDir, &ov, &bytes, FALSE) && bytes > 0)
                m_watchDirty = true;

            ResetEvent(hEvent);

            // Re-issue for next change
            ReadDirectoryChangesW(
                hDir, buf, kBufSize, TRUE,
                FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE,
                nullptr, &ov, nullptr
            );
        }
    }

    CancelIo(hDir);
    WaitForSingleObject(hEvent, 1000);
    CloseHandle(hEvent);
    CloseHandle(hDir);
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
//  Main draw
// ─────────────────────────────────────────────────────────────────────────────

void AssetBrowserPanel::OnDraw()
{
    if (!ImGui::Begin(Title(), &open))
    {
        ImGui::End();
        return;
    }

    const std::string wd = WorkDir();
    if (wd.empty())
    {
        ImGui::TextDisabled("AssetManager not initialized.");
        ImGui::End();
        return;
    }

    // ── File watcher: lazy start + dirty refresh ──────────────────
    if (!m_watchThread.joinable())
        StartFileWatcher();
    if (m_watchDirty.exchange(false))
        AssetManager::Get().Refresh();

    // ── Import queue: process one file per frame ──────────────────
    if (!m_importQueue.empty())
    {
        const auto& entry = m_importQueue.front();
        HandleExternalDrop(entry.srcPath, entry.targetDir);
        m_importQueue.erase(m_importQueue.begin());
        m_importDone++;
        if (m_importQueue.empty())
            AssetManager::Get().Refresh();
    }
    DrawImportProgress();

    // Breadcrumb bar at the top
    DrawBreadcrumb();
    ImGui::Separator();

    const float treeWidth = 200.0f;
    ImGui::BeginChild("##ab_tree", ImVec2(treeWidth, 0), false);
    DrawTree();
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("##ab_content", ImVec2(0, 0), false);
    DrawContent();
    ImGui::EndChild();

    // Popups (must be outside child windows for proper stacking)
    DrawRenamePopup();
    DrawNewFolderPopup();
    DrawNewScriptPopup();

    // ── Confirm delete popup ──────────────────────────────────────
    if (m_confirmDeleteOpen)
    {
        ImGui::OpenPopup("##confirm_delete");
        m_confirmDeleteOpen = false;
    }
    if (ImGui::BeginPopupModal("##confirm_delete", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar))
    {
        ImGui::Text("Delete permanently?");
        ImGui::Spacing();
        if (ImGui::Button("Delete", ImVec2(90, 0)))
        {
            if (m_deletingFolder)
            {
                // delete all assets inside first
                const std::string wd2 = WorkDir();
                fs::path absDir = fs::path(wd2) / m_deleteFolderPath;
                std::error_code ec;
                for (const auto& e : fs::recursive_directory_iterator(absDir, ec))
                {
                    if (!e.is_regular_file(ec)) continue;
                    std::string ext = e.path().extension().string();
                    if (ext == ".data") continue;
                    // find guid
                    std::string rel = fs::relative(e.path(), wd2, ec).string();
                    std::replace(rel.begin(), rel.end(), '\\', '/');
                    std::transform(rel.begin(), rel.end(), rel.begin(),
                                   [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
                    uint64_t g = AssetManager::Get().GetGuid(rel);
                    if (g) AssetManager::Get().DeleteAsset(g);
                }
                AssetManager::Get().DeleteFolder(m_deleteFolderPath, true);
                if (m_selectedDir == m_deleteFolderPath) m_selectedDir.clear();
            }
            else
            {
                AssetManager::Get().DeleteAsset(m_deleteGuid);
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(90, 0)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::End();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Breadcrumb
// ─────────────────────────────────────────────────────────────────────────────

void AssetBrowserPanel::DrawBreadcrumb()
{
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0,0,0,0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f,0.25f,0.25f,1));

    if (ImGui::SmallButton("Content"))
        m_selectedDir.clear();

    if (!m_selectedDir.empty())
    {
        // Split by '/'
        std::string accum;
        std::istringstream ss(m_selectedDir);
        std::string part;
        while (std::getline(ss, part, '/'))
        {
            ImGui::SameLine(0, 2);
            ImGui::TextDisabled(">");
            ImGui::SameLine(0, 2);
            accum = accum.empty() ? part : (accum + "/" + part);
            const std::string snap = accum;
            if (ImGui::SmallButton(part.c_str()))
                m_selectedDir = snap;
        }
    }

    ImGui::PopStyleColor(2);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Folder tree (left pane)
// ─────────────────────────────────────────────────────────────────────────────

void AssetBrowserPanel::DrawTree()
{
    const std::string wd = WorkDir();
    if (wd.empty()) return;

    ImGuiTreeNodeFlags rootFlags =
        ImGuiTreeNodeFlags_OpenOnArrow |
        ImGuiTreeNodeFlags_DefaultOpen |
        ImGuiTreeNodeFlags_SpanAvailWidth;

    if (m_selectedDir.empty())
        rootFlags |= ImGuiTreeNodeFlags_Selected;

    bool open = ImGui::TreeNodeEx("[Content]", rootFlags);

    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
        m_selectedDir.clear();

    DrawContextMenuFolder("");

    if (open)
    {
        for (const auto& sub : SubDirs(fs::path(wd)))
            DrawFolderNode(sub, 1);
        ImGui::TreePop();
    }
}

void AssetBrowserPanel::DrawFolderNode(const fs::path& absDir, int /*depth*/)
{
    const std::string wd = WorkDir();
    std::error_code ec;
    fs::path rel = fs::relative(absDir, wd, ec);
    std::string relStr = rel.string();
    std::replace(relStr.begin(), relStr.end(), '\\', '/');

    const bool hasSubs = !SubDirs(absDir).empty();

    ImGuiTreeNodeFlags flags =
        ImGuiTreeNodeFlags_OpenOnArrow |
        ImGuiTreeNodeFlags_SpanAvailWidth;
    if (!hasSubs)
        flags |= ImGuiTreeNodeFlags_Leaf;
    if (m_selectedDir == relStr)
        flags |= ImGuiTreeNodeFlags_Selected;

    const std::string label = relStr + "##tn";
    const std::string stem  = absDir.filename().string();
    bool open = ImGui::TreeNodeEx((stem + "##tn_" + relStr).c_str(), flags);

    // Click to navigate
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
        m_selectedDir = relStr;

    // Right-click context menu
    DrawContextMenuFolder(relStr);

    // Drag-drop target: drop a file onto this folder
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ASSET_GUID"))
        {
            uint64_t guid = *reinterpret_cast<const uint64_t*>(p->Data);
            AssetManager::Get().MoveAsset(guid, relStr);
        }
        ImGui::EndDragDropTarget();
    }

    if (open)
    {
        for (const auto& sub : SubDirs(absDir))
            DrawFolderNode(sub, 0);
        ImGui::TreePop();
    }
}

void AssetBrowserPanel::DrawContextMenuFolder(const std::string& relDir)
{
    if (!ImGui::BeginPopupContextItem(("##ctx_f_" + relDir).c_str()))
        return;

    if (ImGui::MenuItem("New Folder"))      OpenNewFolder(relDir);
    if (ImGui::MenuItem("New Material"))
    {
        const std::string targetDir = relDir.empty() ? "assets/materials" : relDir;
        uint64_t newGuid = AssetManager::Get().CreateMaterialAsset(targetDir, "NewMaterial");
        if (newGuid && m_onSelect)
        {
            const AssetMeta* m = AssetManager::Get().FindByGuid(newGuid);
            if (m) m_onSelect(newGuid, *m);
        }
    }
    if (ImGui::MenuItem("New Lua Script"))
        OpenNewScript(relDir);
    if (!relDir.empty())
    {
        ImGui::Separator();
        if (ImGui::MenuItem("Rename"))
            OpenFolderRename(relDir);
        if (ImGui::MenuItem("Delete"))
        {
            m_confirmDeleteOpen  = true;
            m_deletingFolder     = true;
            m_deleteFolderPath   = relDir;
        }
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Refresh"))
        AssetManager::Get().Refresh();

    ImGui::EndPopup();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Content pane (right)
// ─────────────────────────────────────────────────────────────────────────────

void AssetBrowserPanel::DrawContent()
{
    const float iconW  = 80.0f;
    const float iconH  = 70.0f;
    const float padX   = 8.0f;
    const float availW = ImGui::GetContentRegionAvail().x;
    const int   cols   = std::max(1, static_cast<int>(availW / (iconW + padX)));

    auto items = ContentOf(m_selectedDir);

    int col = 0;
    for (auto& item : items)
    {
        if (col > 0) ImGui::SameLine();
        if (col >= cols) { col = 0; ImGui::SameLine(0); ImGui::NewLine(); }

        ImGui::BeginGroup();

        // Color-code by type
        ImVec4 fileBg = ImVec4(0.20f, 0.20f, 0.22f, 0.7f);
        if (!item.isDir)
        {
            if (item.type == AssetType::Material) fileBg = ImVec4(0.18f, 0.30f, 0.18f, 0.80f);
            else if (item.type == AssetType::Scene)    fileBg = ImVec4(0.36f, 0.25f, 0.10f, 0.90f);
            else if (item.type == AssetType::Shader)  fileBg = ImVec4(0.28f, 0.22f, 0.10f, 0.85f);
            else if (item.type == AssetType::Texture)  fileBg = ImVec4(0.10f, 0.22f, 0.28f, 0.85f);
            else if (item.type == AssetType::Mesh)     fileBg = ImVec4(0.22f, 0.14f, 0.28f, 0.85f);
            else if (item.type == AssetType::Script)   fileBg = ImVec4(0.28f, 0.18f, 0.10f, 0.85f);
        }

        ImGui::PushStyleColor(ImGuiCol_Button, item.isDir
            ? ImVec4(0.25f, 0.40f, 0.65f, 0.6f)
            : fileBg);

        // Build icon label: show extension for files
        std::string iconLabel = item.isDir ? "[Dir]" : item.ext;
        if (iconLabel.empty()) iconLabel = IconForType(item.type);

        ImGui::PushID(item.isDir ? item.relPath.c_str()
                                 : reinterpret_cast<const void*>(item.guid));

        const bool clicked = ImGui::Button(iconLabel.c_str(), ImVec2(iconW, iconH - 20.0f));

        // Drag source (files only)
        if (!item.isDir && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
        {
            ImGui::SetDragDropPayload("ASSET_GUID", &item.guid, sizeof(uint64_t));
            ImGui::Text("Move: %s", item.name.c_str());
            ImGui::EndDragDropSource();
        }

        // Drop target on folder icon
        if (item.isDir && ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ASSET_GUID"))
            {
                uint64_t guid = *reinterpret_cast<const uint64_t*>(p->Data);
                AssetManager::Get().MoveAsset(guid, item.relPath);
            }
            ImGui::EndDragDropTarget();
        }

        ImGui::PopStyleColor();

        // Double-click to navigate into folder; single click to select file
        if (item.isDir)
        {
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                m_selectedDir = item.relPath;
        }
        else if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            if (item.type == AssetType::Script)
            {
                // Open in VS Code
                const std::string absPath = (fs::path(WorkDir()) / item.relPath).string();
                const std::string cmd = "code \"" + absPath + "\"";
                system(cmd.c_str());
            }
            else if (m_onSelect)
            {
                const AssetMeta* m = AssetManager::Get().FindByGuid(item.guid);
                if (m) m_onSelect(item.guid, *m);
            }
        }

        // Context menus
        if (item.isDir)
            DrawContextMenuFolder(item.relPath);
        else
        {
            const AssetMeta* m = AssetManager::Get().FindByGuid(item.guid);
            if (m) DrawContextMenuFile(item.guid, *m);
        }

        // Label (truncated)
        std::string label = item.name;
        if (label.size() > 10) label = label.substr(0, 9) + "~";
        ImGui::TextUnformatted(label.c_str());
        if (ImGui::IsItemHovered() && item.name.size() > 10)
            ImGui::SetTooltip("%s", item.name.c_str());

        ImGui::PopID();
        ImGui::EndGroup();
        ++col;
    }

    // Right-click on background (always active, even when not empty)
    if (ImGui::BeginPopupContextWindow("##ctx_content_bg", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
    {
        if (ImGui::MenuItem("New Folder"))      OpenNewFolder(m_selectedDir);
        if (ImGui::MenuItem("New Lua Script"))  OpenNewScript(m_selectedDir);
        if (ImGui::MenuItem("Refresh"))         AssetManager::Get().Refresh();
        ImGui::EndPopup();
    }

    if (items.empty())
        ImGui::TextDisabled("(empty)");
}

void AssetBrowserPanel::DrawContextMenuFile(uint64_t guid, const AssetMeta& meta)
{
    if (!ImGui::BeginPopupContextItem(("##ctx_" + meta.path).c_str()))
        return;

    ImGui::TextDisabled("%s", meta.path.c_str());
    ImGui::TextDisabled("GUID: %016llX", static_cast<unsigned long long>(guid));
    ImGui::Separator();

    if (ImGui::MenuItem("Rename"))
        OpenRename(guid, meta.name);

    if (ImGui::MenuItem("Delete"))
    {
        m_confirmDeleteOpen = true;
        m_deletingFolder    = false;
        m_deleteGuid        = guid;
    }

    ImGui::Separator();
    if (ImGui::MenuItem("Copy GUID"))
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "%016llX", static_cast<unsigned long long>(guid));
        ImGui::SetClipboardText(buf);
    }

    ImGui::EndPopup();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Rename popup
// ─────────────────────────────────────────────────────────────────────────────

void AssetBrowserPanel::OpenRename(uint64_t guid, const std::string& currentStem)
{
    m_renameOpen       = true;
    m_renamingFolder   = false;
    m_renameGuid       = guid;
    memset(m_renameBuffer, 0, sizeof(m_renameBuffer));
    strncpy(m_renameBuffer, currentStem.c_str(), sizeof(m_renameBuffer) - 1);
}

void AssetBrowserPanel::OpenFolderRename(const std::string& relDir)
{
    m_renameOpen       = true;
    m_renamingFolder   = true;
    m_renameFolderPath = relDir;
    memset(m_renameBuffer, 0, sizeof(m_renameBuffer));
    // stem = last component
    std::string stem = fs::path(relDir).filename().string();
    strncpy(m_renameBuffer, stem.c_str(), sizeof(m_renameBuffer) - 1);
}

void AssetBrowserPanel::DrawRenamePopup()
{
    if (m_renameOpen)
    {
        ImGui::OpenPopup("##rename_popup");
        m_renameOpen = false;
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                                ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    }

    if (ImGui::BeginPopupModal("##rename_popup", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar))
    {
        ImGui::Text(m_renamingFolder ? "Rename Folder" : "Rename Asset");
        ImGui::Separator();

        ImGui::SetNextItemWidth(260);
        bool confirm = ImGui::InputText("##rn_input", m_renameBuffer,
                                        sizeof(m_renameBuffer),
                                        ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SetItemDefaultFocus();

        ImGui::Spacing();
        if (ImGui::Button("OK", ImVec2(120, 0)) || confirm)
        {
            const std::string newName(m_renameBuffer);
            if (!newName.empty())
            {
                if (m_renamingFolder)
                {
                    // Rename folder on disk and update all asset paths inside
                    const std::string wd = WorkDir();
                    fs::path oldAbs =  fs::path(wd) / m_renameFolderPath;
                    fs::path newAbs = oldAbs.parent_path() / newName;
                    std::error_code ec;
                    fs::rename(oldAbs, newAbs, ec);
                    if (!ec)
                    {
                        // Reload all sidecars that were inside the renamed dir
                        // — just do a full refresh; the new paths will be picked up
                        AssetManager::Get().Refresh();
                        if (m_selectedDir == m_renameFolderPath)
                            m_selectedDir = fs::relative(newAbs, wd).string();
                    }
                }
                else
                {
                    AssetManager::Get().RenameAsset(m_renameGuid, newName);
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

// ─────────────────────────────────────────────────────────────────────────────
//  New folder popup
// ─────────────────────────────────────────────────────────────────────────────

void AssetBrowserPanel::OpenNewFolder(const std::string& parentRelDir)
{
    m_newFolderOpen   = true;
    m_newFolderParent = parentRelDir;
    memset(m_newFolderBuffer, 0, sizeof(m_newFolderBuffer));
    strncpy(m_newFolderBuffer, "NewFolder", sizeof(m_newFolderBuffer) - 1);
}

void AssetBrowserPanel::OpenNewScript(const std::string& parentRelDir)
{
    m_newScriptOpen   = true;
    m_newScriptParent = parentRelDir;
    memset(m_newScriptBuffer, 0, sizeof(m_newScriptBuffer));
    strncpy(m_newScriptBuffer, "NewScript", sizeof(m_newScriptBuffer) - 1);
}

void AssetBrowserPanel::DrawNewScriptPopup()
{
    if (m_newScriptOpen)
    {
        ImGui::OpenPopup("##new_script");
        m_newScriptOpen = false;
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                                ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    }

    if (ImGui::BeginPopupModal("##new_script", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar))
    {
        ImGui::Text("New Lua Script");
        ImGui::Separator();
        ImGui::SetNextItemWidth(260);
        bool confirm = ImGui::InputText("##ns_input", m_newScriptBuffer,
                                        sizeof(m_newScriptBuffer),
                                        ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SetItemDefaultFocus();
        ImGui::Spacing();

        if (ImGui::Button("Create", ImVec2(120, 0)) || confirm)
        {
            const std::string name(m_newScriptBuffer);
            if (!name.empty())
            {
                const std::string wd = WorkDir();
                std::string relDir   = m_newScriptParent.empty() ? "assets/scripts" : m_newScriptParent;
                fs::path absDir = fs::path(wd) / relDir;
                std::error_code ec;
                fs::create_directories(absDir, ec);

                // Unique filename
                std::string stem = name;
                fs::path    absFile;
                int         suffix = 0;
                do {
                    absFile = absDir / (stem + ".lua");
                    if (!fs::exists(absFile)) break;
                    stem = name + "_" + std::to_string(++suffix);
                } while (true);

                // Write template Lua script
                std::ofstream f(absFile.string(), std::ios::trunc);
                if (f.is_open())
                {
                    f << "-- " << stem << "\n";
                    f << "\n";
                    f << "function OnStart()\n";
                    f << "end\n";
                    f << "\n";
                    f << "function OnUpdate(dt)\n";
                    f << "end\n";
                    f.close();
                    AssetManager::Get().Refresh();
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

void AssetBrowserPanel::DrawNewFolderPopup()
{
    if (m_newFolderOpen)
    {
        ImGui::OpenPopup("##new_folder");
        m_newFolderOpen = false;
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                                ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    }

    if (ImGui::BeginPopupModal("##new_folder", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar))
    {
        ImGui::Text("New Folder");
        ImGui::Separator();
        ImGui::SetNextItemWidth(260);
        bool confirm = ImGui::InputText("##nf_input", m_newFolderBuffer,
                                        sizeof(m_newFolderBuffer),
                                        ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SetItemDefaultFocus();
        ImGui::Spacing();

        if (ImGui::Button("Create", ImVec2(120, 0)) || confirm)
        {
            const std::string name(m_newFolderBuffer);
            if (!name.empty())
            {
                std::string rel = m_newFolderParent.empty()
                    ? name : (m_newFolderParent + "/" + name);
                AssetManager::Get().CreateFolder(rel);
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0)))
            ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }
}

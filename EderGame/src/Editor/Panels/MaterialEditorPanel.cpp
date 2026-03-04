#include "MaterialEditorPanel.h"
#include <imgui/imgui.h>
#include <filesystem>
#include <algorithm>
#include <cstdio>
#include <sstream>
#include <iomanip>

namespace fs = std::filesystem;
using namespace Krayon;

// ─────────────────────────────────────────────────────────────────────────────
//  Helpers
// ─────────────────────────────────────────────────────────────────────────────

static std::string GuidHex(uint64_t g)
{
    if (g == 0) return "(none)";
    std::ostringstream ss;
    ss << std::hex << std::uppercase << g;
    return ss.str();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Open
// ─────────────────────────────────────────────────────────────────────────────

void MaterialEditorPanel::Open(uint64_t guid)
{
    if (guid == 0) return;
    if (AssetManager::Get().ReadMaterialAsset(guid, m_current))
    {
        m_openGuid = guid;
        m_dirty    = false;
        open       = true;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  OnDraw
// ─────────────────────────────────────────────────────────────────────────────

void MaterialEditorPanel::OnDraw()
{
    if (!ImGui::Begin(Title(), &open)) { ImGui::End(); return; }

    // ── Left: material list ──────────────────────────────────────────────────
    const float listW = 180.0f;
    ImGui::BeginChild("##matlist", ImVec2(listW, 0), true);
    DrawNewMaterialSection();
    ImGui::Separator();
    DrawMaterialList();
    ImGui::EndChild();

    ImGui::SameLine();

    // ── Right: inspector ─────────────────────────────────────────────────────
    ImGui::BeginChild("##matinspect", ImVec2(0, 0), false);
    if (m_openGuid != 0)
        DrawMaterialInspector();
    else
    {
        ImVec2 avail = ImGui::GetContentRegionAvail();
        const char* hint = "Select a material\nto inspect it.";
        ImVec2 sz = ImGui::CalcTextSize(hint, nullptr, false, avail.x);
        ImGui::SetCursorPosY((avail.y - sz.y) * 0.4f);
        ImGui::SetCursorPosX((avail.x - sz.x) * 0.5f);
        ImGui::TextDisabled("%s", hint);
    }
    ImGui::EndChild();

    // ── Popups ────────────────────────────────────────────────────────────────
    // New material dialog
    if (m_newMatOpen)
        ImGui::OpenPopup("New Material##newmatpop");

    if (ImGui::BeginPopupModal("New Material##newmatpop",
                               nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextDisabled("Name:");
        ImGui::SetNextItemWidth(220);
        ImGui::InputText("##nmname", m_newMatName, sizeof(m_newMatName));
        ImGui::TextDisabled("Folder (relative to workDir):");
        ImGui::SetNextItemWidth(220);
        ImGui::InputText("##nmdir", m_newMatDir, sizeof(m_newMatDir));

        ImGui::Spacing();
        if (ImGui::Button("Create", ImVec2(100, 0)))
        {
            uint64_t newGuid = AssetManager::Get().CreateMaterialAsset(
                m_newMatDir, m_newMatName);
            if (newGuid)
                Open(newGuid);
            m_newMatOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(80, 0)))
        {
            m_newMatOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Rename dialog
    if (m_renameOpen)
        ImGui::OpenPopup("Rename Material##renmtpop");

    if (ImGui::BeginPopupModal("Rename Material##renmtpop",
                               nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextDisabled("New name (file stem):");
        ImGui::SetNextItemWidth(200);
        ImGui::InputText("##renname", m_renameBuf, sizeof(m_renameBuf));
        ImGui::Spacing();
        if (ImGui::Button("Rename", ImVec2(90, 0)))
        {
            if (m_renameBuf[0] != '\0')
            {
                AssetManager::Get().RenameAsset(m_openGuid, m_renameBuf);
                // Reload after rename
                AssetManager::Get().ReadMaterialAsset(m_openGuid, m_current);
            }
            m_renameOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(80, 0)))
        {
            m_renameOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Confirm delete dialog
    if (m_confirmDelete)
        ImGui::OpenPopup("Delete Material?##delmtpop");

    if (ImGui::BeginPopupModal("Delete Material?##delmtpop",
                               nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        const AssetMeta* meta = AssetManager::Get().FindByGuid(m_deleteGuid);
        if (meta)
        {
            ImGui::Text("Delete \"%s\" ?", meta->name.c_str());
            ImGui::TextDisabled("This cannot be undone.");
        }
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.72f, 0.10f, 0.10f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.18f, 0.18f, 1.0f));
        if (ImGui::Button("Delete", ImVec2(90, 0)))
        {
            AssetManager::Get().DeleteAsset(m_deleteGuid);
            if (m_deleteGuid == m_openGuid)
            {
                m_openGuid = 0;
                m_current  = {};
                m_dirty    = false;
            }
            m_confirmDelete = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor(2);
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(80, 0)))
        {
            m_confirmDelete = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::End();
}

// ─────────────────────────────────────────────────────────────────────────────
//  DrawNewMaterialSection
// ─────────────────────────────────────────────────────────────────────────────

void MaterialEditorPanel::DrawNewMaterialSection()
{
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.18f, 0.45f, 0.18f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.58f, 0.25f, 1.0f));
    if (ImGui::Button("+ New Material", ImVec2(-1, 0)))
    {
        m_newMatName[0] = '\0';
        std::strncpy(m_newMatName, "NewMaterial", sizeof(m_newMatName) - 1);
        std::strncpy(m_newMatDir,  "assets/materials", sizeof(m_newMatDir) - 1);
        m_newMatOpen = true;
    }
    ImGui::PopStyleColor(2);
}

// ─────────────────────────────────────────────────────────────────────────────
//  DrawMaterialList
// ─────────────────────────────────────────────────────────────────────────────

void MaterialEditorPanel::DrawMaterialList()
{
    const auto& all = AssetManager::Get().GetAll();
    // Collect materials sorted by name
    std::vector<const AssetMeta*> mats;
    for (const auto& [guid, meta] : all)
        if (meta.type == AssetType::Material)
            mats.push_back(&meta);
    std::sort(mats.begin(), mats.end(),
              [](const AssetMeta* a, const AssetMeta* b){ return a->name < b->name; });

    for (const AssetMeta* m : mats)
    {
        bool selected = (m->guid == m_openGuid);
        ImGui::PushID(static_cast<int>(m->guid & 0xFFFFFFFF));

        if (ImGui::Selectable(m->name.c_str(), selected,
                              ImGuiSelectableFlags_AllowDoubleClick))
            Open(m->guid);

        // Drag source — emit ASSET_GUID so Inspector drop slots accept it
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
        {
            ImGui::SetDragDropPayload("ASSET_GUID", &m->guid, sizeof(uint64_t));
            ImGui::Text("[Mat]  %s", m->name.c_str());
            ImGui::EndDragDropSource();
        }

        // Context menu per row
        if (ImGui::BeginPopupContextItem("##matctx"))
        {
            ImGui::TextDisabled("%s", m->name.c_str());
            ImGui::Separator();
            if (ImGui::MenuItem("Rename"))
            {
                m_openGuid = m->guid;
                AssetManager::Get().ReadMaterialAsset(m->guid, m_current);
                std::strncpy(m_renameBuf, m->name.c_str(), sizeof(m_renameBuf) - 1);
                m_renameOpen = true;
            }
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
            if (ImGui::MenuItem("Delete"))
            {
                m_deleteGuid    = m->guid;
                m_confirmDelete = true;
            }
            ImGui::PopStyleColor();
            if (ImGui::MenuItem("Copy GUID"))
            {
                const std::string s = GuidHex(m->guid);
                ImGui::SetClipboardText(s.c_str());
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }

    if (mats.empty())
        ImGui::TextDisabled("No materials");
}

// ─────────────────────────────────────────────────────────────────────────────
//  DrawShaderDropField
//  Returns true and updates shaderGuid when a Shader asset is dropped.
// ─────────────────────────────────────────────────────────────────────────────

bool MaterialEditorPanel::DrawShaderDropField(const char* label, uint64_t& shaderGuid)
{
    bool changed = false;

    // Hover detection
    bool canDrop = false;
    if (ImGui::GetDragDropPayload() &&
        ImGui::GetDragDropPayload()->IsDataType("ASSET_GUID"))
    {
        uint64_t hg = *reinterpret_cast<const uint64_t*>(
            ImGui::GetDragDropPayload()->Data);
        const AssetMeta* hm = AssetManager::Get().FindByGuid(hg);
        canDrop = hm && hm->type == AssetType::Shader;
    }

    // Display name
    std::string displayName = "(none)";
    if (shaderGuid)
    {
        const AssetMeta* sm = AssetManager::Get().FindByGuid(shaderGuid);
        if (sm) displayName = sm->name;
        else    displayName = GuidHex(shaderGuid);
    }

    ImGui::PushID(label);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", label);
    ImGui::SameLine(120.0f);

    ImVec4 bgCol = canDrop
        ? ImVec4(0.18f, 0.48f, 0.18f, 0.85f)
        : ImVec4(0.10f, 0.10f, 0.10f, 1.00f);

    ImGui::PushStyleColor(ImGuiCol_Button,        bgCol);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.20f, 0.24f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  bgCol);

    std::string btn = displayName;
    if (btn.size() > 22) btn = btn.substr(0, 19) + "...";
    btn += "  \xce\xb2";
    ImGui::Button(btn.c_str(), ImVec2(ImGui::GetContentRegionAvail().x, 0));

    ImGui::PopStyleColor(3);

    if (shaderGuid && ImGui::IsItemHovered())
    {
        const AssetMeta* sm = AssetManager::Get().FindByGuid(shaderGuid);
        if (sm) ImGui::SetTooltip("%s", sm->path.c_str());
    }

    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ASSET_GUID"))
        {
            uint64_t droppedGuid = *reinterpret_cast<const uint64_t*>(p->Data);
            const AssetMeta* meta = AssetManager::Get().FindByGuid(droppedGuid);
            if (meta && meta->type == AssetType::Shader)
            {
                shaderGuid = droppedGuid;
                changed    = true;
            }
        }
        ImGui::EndDragDropTarget();
    }

    // Right-click to clear
    if (ImGui::BeginPopupContextItem("##clrshd"))
    {
        if (ImGui::MenuItem("Clear")) { shaderGuid = 0; changed = true; }
        ImGui::EndPopup();
    }

    ImGui::PopID();
    return changed;
}

// ─────────────────────────────────────────────────────────────────────────────
//  DrawTextureDropField
//  Accept ASSET_GUID drops of type Texture. Shows name or "(none)".
//  Has an [x] button to clear, and tooltip on hover.
// ─────────────────────────────────────────────────────────────────────────────

bool MaterialEditorPanel::DrawTextureDropField(const char* label, uint64_t& texGuid)
{
    bool changed = false;

    bool canDrop = false;
    if (ImGui::GetDragDropPayload() &&
        ImGui::GetDragDropPayload()->IsDataType("ASSET_GUID"))
    {
        uint64_t hg = *reinterpret_cast<const uint64_t*>(
            ImGui::GetDragDropPayload()->Data);
        const AssetMeta* hm = AssetManager::Get().FindByGuid(hg);
        canDrop = hm && hm->type == AssetType::Texture;
    }

    std::string displayName = "(none)";
    if (texGuid)
    {
        const AssetMeta* tm = AssetManager::Get().FindByGuid(texGuid);
        if (tm)
        {
            const std::string& p = tm->path;
            const size_t dot = p.rfind('.');
            displayName = tm->name + (dot != std::string::npos ? p.substr(dot) : "");
        }
        else
            displayName = GuidHex(texGuid);
    }

    ImGui::PushID(label);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", label);
    ImGui::SameLine(120.0f);

    ImVec4 bgCol = canDrop
        ? ImVec4(0.10f, 0.38f, 0.10f, 0.90f)
        : (texGuid ? ImVec4(0.12f, 0.18f, 0.24f, 1.00f)
                   : ImVec4(0.10f, 0.10f, 0.10f, 1.00f));

    ImGui::PushStyleColor(ImGuiCol_Button,        bgCol);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.22f, 0.28f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  bgCol);

    std::string btn = displayName;
    if (btn.size() > 20) btn = btn.substr(0, 17) + "...";
    btn += "  \xce\xb2";
    const float clearW = 26.0f;
    ImGui::Button(btn.c_str(), ImVec2(ImGui::GetContentRegionAvail().x - clearW, 0));
    ImGui::PopStyleColor(3);

    if (texGuid && ImGui::IsItemHovered())
    {
        const AssetMeta* tm = AssetManager::Get().FindByGuid(texGuid);
        if (tm) ImGui::SetTooltip("%s", tm->path.c_str());
    }

    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ASSET_GUID"))
        {
            uint64_t droppedGuid = *reinterpret_cast<const uint64_t*>(p->Data);
            const AssetMeta* meta = AssetManager::Get().FindByGuid(droppedGuid);
            if (meta && meta->type == AssetType::Texture)
            {
                texGuid = droppedGuid;
                changed = true;
            }
        }
        ImGui::EndDragDropTarget();
    }

    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.28f, 0.12f, 0.12f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.50f, 0.18f, 0.18f, 1.0f));
    if (ImGui::Button("x##clr", ImVec2(22, 0)) && texGuid != 0)
    {
        texGuid = 0;
        changed = true;
    }
    ImGui::PopStyleColor(2);

    ImGui::PopID();
    return changed;
}

// ─────────────────────────────────────────────────────────────────────────────
//  DrawMaterialInspector
// ─────────────────────────────────────────────────────────────────────────────

void MaterialEditorPanel::DrawMaterialInspector()
{
    auto& AM = AssetManager::Get();

    // Header row
    const AssetMeta* meta = AM.FindByGuid(m_openGuid);
    if (!meta) { m_openGuid = 0; return; }

    ImGui::TextColored(ImVec4(0.92f, 0.92f, 0.92f, 1.0f),
                       "[Mat] %s", meta->name.c_str());
    ImGui::TextDisabled("GUID: %s", GuidHex(m_openGuid).c_str());
    ImGui::TextDisabled("Path: %s", meta->path.c_str());

    // Action buttons
    ImGui::Spacing();
    if (ImGui::SmallButton("Rename"))
    {
        std::strncpy(m_renameBuf, meta->name.c_str(), sizeof(m_renameBuf) - 1);
        m_renameOpen = true;
    }
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.40f, 0.40f, 1.0f));
    if (ImGui::SmallButton("Delete"))
    {
        m_deleteGuid    = m_openGuid;
        m_confirmDelete = true;
    }
    ImGui::PopStyleColor();

    ImGui::Separator();
    ImGui::Spacing();

    // ── Shader slots ──────────────────────────────────────────────
    ImGui::TextColored(ImVec4(0.65f, 0.72f, 1.0f, 1.0f), "Shaders");
    ImGui::Spacing();
    bool changed = false;
    changed |= DrawShaderDropField("Vert Shader", m_current.vertShaderGuid);
    changed |= DrawShaderDropField("Frag Shader", m_current.fragShaderGuid);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ── Surface params ────────────────────────────────────────────
    ImGui::TextColored(ImVec4(0.75f, 0.90f, 0.75f, 1.0f), "Surface");
    ImGui::Spacing();

    changed |= ImGui::ColorEdit4("Albedo",   m_current.albedo);
    changed |= ImGui::DragFloat ("Roughness", &m_current.roughness, 0.01f, 0.0f, 1.0f);
    changed |= ImGui::DragFloat ("Metallic",  &m_current.metallic,  0.01f, 0.0f, 1.0f);
    changed |= ImGui::ColorEdit3("Emissive", m_current.emissive);

    // ── Textures ──────────────────────────────────────────────────
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.65f, 0.85f, 1.0f, 1.0f), "Textures");
    ImGui::Spacing();

    changed |= DrawTextureDropField("Albedo Tex",    m_current.albedoTexGuid);
    changed |= DrawTextureDropField("Normal Tex",    m_current.normalTexGuid);
    changed |= DrawTextureDropField("Roughness Tex", m_current.roughnessTexGuid);
    changed |= DrawTextureDropField("Emissive Tex",  m_current.emissiveTexGuid);

    // ── Auto-save on any change ───────────────────────────────────
    if (changed)
    {
        AM.SaveMaterialAsset(m_openGuid, m_current);
        m_dirty = false;
    }
}

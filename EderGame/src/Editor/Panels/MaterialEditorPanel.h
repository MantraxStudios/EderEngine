#pragma once
#include "Panel.h"
#include <IO/AssetManager.h>
#include <cstdint>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
//  MaterialEditorPanel
//
//  Allows creating, editing, renaming and deleting .mat assets through
//  the AssetManager (GUID-based, sidecar-stable).
//
//  Workflow:
//    • Asset Browser double-clicks a .mat  →  editor.Open(guid)
//    • "New Material" button in the panel  →  prompts dir + name
//    • Shader fields accept ASSET_GUID drag-drop (type == Shader)
//    • Changes are saved to disk on every edit (SaveMaterialAsset)
// ─────────────────────────────────────────────────────────────────────────────

class MaterialEditorPanel : public Panel
{
public:
    const char* Title() const override { return "Material Editor"; }
    void        OnDraw()      override;

    /// Open a material asset for inspection / editing.
    void Open(uint64_t guid);

private:
    // ── Helpers ───────────────────────────────────────────────────
    void DrawNewMaterialSection();
    void DrawMaterialList();
    void DrawMaterialInspector();
    bool DrawShaderDropField (const char* label, uint64_t& shaderGuid);
    bool DrawTextureDropField(const char* label, uint64_t& texGuid);

    // ── New-material creation state ────────────────────────────────
    bool m_newMatOpen       = false;
    char m_newMatName[128]  = "NewMaterial";
    char m_newMatDir[256]   = "assets/materials";

    // ── Currently open material ────────────────────────────────────
    uint64_t              m_openGuid  = 0;
    Krayon::MaterialAsset m_current;        // loaded copy (written on change)
    bool                  m_dirty     = false;

    // ── Rename popup ───────────────────────────────────────────────
    bool m_renameOpen = false;
    char m_renameBuf[128] = {};

    // ── Delete confirm ─────────────────────────────────────────────
    bool     m_confirmDelete = false;
    uint64_t m_deleteGuid    = 0;
};

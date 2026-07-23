#pragma once
#include "Panel.h"
#include "PostProcess/PostProcessGraph.h"
#include <functional>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
//  PostProcessPanel
//  Unity-style post-process stack editor. Lets the user add built-in effects
//  (Color Grading, Bloom, Depth of Field, Vignette, Chromatic Aberration,
//  Film Grain, Sharpen, Scanlines) with labelled sliders, reorder/toggle them,
//  edit a custom shader effect, and save/load the whole stack as a preset file.
//
//  Usage:
//    panel.SetGraph(&m_ppGraph);
//    panel.SetOnChanged([this](){ m_ppDirty = true; });
//    if (panel.open) panel.OnDraw();
// ─────────────────────────────────────────────────────────────────────────────
class PostProcessPanel : public Panel
{
public:
    const char* Title() const override { return "Post Process"; }
    void        OnDraw()      override;

    void SetGraph    (Krayon::PostProcessGraph* graph) { m_graph = graph; }
    void SetOnChanged(std::function<void()>     cb)    { m_onChanged = std::move(cb); }

private:
    void DrawPresetBar();
    void DrawAddMenu();
    void DrawEffectInspector(int index);

    void RefreshPresets();
    bool SavePreset(const std::string& name);
    bool LoadPreset(const std::string& absPath);
    void LoadDefaultStack();

    Krayon::PostProcessGraph* m_graph     = nullptr;
    std::function<void()>     m_onChanged;
    int                       m_selected  = -1;

    // Preset browser state
    std::vector<std::string>  m_presetFiles;   // absolute paths of *.pp presets
    bool                      m_presetsScanned = false;
    char                      m_savePresetName[128] = "MyPreset";

    // Custom-effect add popup state
    char m_addName  [128] = "New Effect";
    char m_addShader[512] = "shaders/passthrough.frag.spv";
};

#pragma once
#include "Panel.h"
#include "PostProcess/PostProcessGraph.h"
#include <functional>

// ─────────────────────────────────────────────────────────────────────────────
//  PostProcessPanel
//  Editor panel that shows the PostProcessGraph, lets the user reorder effects,
//  toggle/edit params, add new effects, and remove existing ones.
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
    void DrawEffectRow (int index);

    Krayon::PostProcessGraph* m_graph     = nullptr;
    std::function<void()>     m_onChanged;
    int                       m_selected  = -1;

    // Add-effect popup state
    char m_addName  [128] = "New Effect";
    char m_addShader[512] = "shaders/passthrough.frag.spv";
};

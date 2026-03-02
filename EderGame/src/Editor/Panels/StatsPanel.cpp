#include "StatsPanel.h"
#include <imgui/imgui.h>
#include <cstdio>

void StatsPanel::Update(float dt)
{
    smooth = 0.9f * smooth + 0.1f * dt;
    fpsHistory[histOffset] = (smooth > 0.0f) ? 1.0f / smooth : 0.0f;
    histOffset = (histOffset + 1) % HISTORY;
}

void StatsPanel::OnDraw()
{
    // Overlay — bottom-left corner, no decoration
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(10, io.DisplaySize.y - 110), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(220, 100), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.65f);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration      |
        ImGuiWindowFlags_NoInputs          |
        ImGuiWindowFlags_NoMove            |
        ImGuiWindowFlags_NoSavedSettings   |
        ImGuiWindowFlags_NoFocusOnAppearing|
        ImGuiWindowFlags_NoBringToFrontOnFocus;

    if (!ImGui::Begin("##StatsOverlay", nullptr, flags))
    { ImGui::End(); return; }

    float fps = (smooth > 0.0f) ? 1.0f / smooth : 0.0f;
    float ms  = smooth * 1000.0f;

    // FPS / ms row
    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.2f, 1.f), "STATS");
    ImGui::SameLine();
    ImGui::TextDisabled(" |");
    ImGui::SameLine();
    ImGui::Text("%.0f FPS", fps);
    ImGui::SameLine();
    ImGui::TextDisabled("%.2f ms", ms);

    // FPS graph
    char overlay[32];
    snprintf(overlay, sizeof(overlay), "%.0f", fps);
    ImGui::PlotLines("##fps", fpsHistory, HISTORY, histOffset,
                     overlay, 0.0f, 300.0f, ImVec2(-1, 44));

    ImGui::End();
}

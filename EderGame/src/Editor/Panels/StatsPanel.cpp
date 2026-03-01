#include "StatsPanel.h"
#include <imgui/imgui.h>

void StatsPanel::Update(float dt)
{
    smooth = 0.9f * smooth + 0.1f * dt;
}

void StatsPanel::OnDraw()
{
    ImGui::SetNextWindowPos (ImVec2(10,  30), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(175, 60), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(Title(), &open)) { ImGui::End(); return; }

    ImGui::Text("FPS  %.0f", 1.0f / smooth);
    ImGui::Text("ms   %.2f", smooth * 1000.0f);

    ImGui::End();
}

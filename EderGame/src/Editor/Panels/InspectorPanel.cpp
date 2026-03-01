#include "InspectorPanel.h"
#include <imgui/imgui.h>

void InspectorPanel::OnDraw()
{
    ImGui::SetNextWindowPos (ImVec2(10, 465), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(230, 170), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(Title(), &open)) { ImGui::End(); return; }

    if (scene && selected >= 0 && selected < (int)scene->GetObjects().size())
    {
        Transform& t = scene->GetObjects()[selected].transform;
        ImGui::DragFloat3("Position", &t.position.x, 0.05f);
        ImGui::DragFloat3("Rotation", &t.rotation.x, 0.5f);
        ImGui::DragFloat3("Scale",    &t.scale.x,    0.05f, 0.001f, 100.0f);
    }
    else
    {
        ImGui::TextDisabled("No object selected");
    }

    ImGui::End();
}

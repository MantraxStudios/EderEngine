#include "HierarchyPanel.h"
#include <imgui/imgui.h>

void HierarchyPanel::OnDraw()
{
    if (!scene) return;

    ImGui::SetNextWindowPos (ImVec2(10, 255), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(230, 200), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(Title(), &open)) { ImGui::End(); return; }

    auto& objs = scene->GetObjects();
    for (int i = 0; i < (int)objs.size(); i++)
    {
        char label[32];
        snprintf(label, sizeof(label), "Object %d", i);
        bool sel = (selected == i);
        if (ImGui::Selectable(label, sel))
            selected = sel ? -1 : i;
    }

    ImGui::End();
}

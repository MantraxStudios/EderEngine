#include "CameraPanel.h"
#include <imgui/imgui.h>

void CameraPanel::OnDraw()
{
    if (!camera) return;

    ImGui::SetNextWindowPos (ImVec2(10, 100), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(230, 145), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(Title(), &open)) { ImGui::End(); return; }

    glm::vec3 pos = camera->GetPosition();
    glm::vec3 fwd = camera->GetForward();
    ImGui::Text("Pos  %.1f  %.1f  %.1f", pos.x, pos.y, pos.z);
    ImGui::Text("Fwd  %.2f  %.2f  %.2f", fwd.x, fwd.y, fwd.z);
    ImGui::Separator();
    ImGui::DragFloat3("FPS Pos", &camera->fpsPos.x,   0.1f);
    ImGui::DragFloat ("Near",    &camera->nearPlane,  0.01f, 0.001f,   10.0f);
    ImGui::DragFloat ("Far",     &camera->farPlane,   1.0f,  10.0f,  2000.0f);

    ImGui::End();
}

#include "CameraPanel.h"
#include <imgui/imgui.h>

void CameraPanel::OnDraw()
{
    if (!camera) return;
    if (!ImGui::Begin(Title(), &open)) { ImGui::End(); return; }

    glm::vec3 pos = camera->GetPosition();
    glm::vec3 fwd = camera->GetForward();

    // Header: position / direction read-only
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.05f, 0.05f, 0.05f, 1.0f));
    ImGui::BeginChild("##caminfo", ImVec2(-1, 54), ImGuiChildFlags_Borders);
    ImGui::TextDisabled("LOCATION");
    ImGui::SameLine(76);
    ImGui::Text("%.1f   %.1f   %.1f", pos.x, pos.y, pos.z);
    ImGui::TextDisabled("FORWARD");
    ImGui::SameLine(76);
    ImGui::Text("%.2f  %.2f  %.2f", fwd.x, fwd.y, fwd.z);
    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::Spacing();
    ImGui::SeparatorText("Perspective");

    // FOV
    float fovDeg = camera->fov;
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderFloat("##fov", &fovDeg, 10.0f, 120.0f, "FOV  %.0f deg"))
        camera->fov = fovDeg;

    ImGui::SetNextItemWidth(-70);
    ImGui::DragFloat("Near##cam", &camera->nearPlane, 0.01f, 0.001f, 10.0f,  "%.3f");
    ImGui::SetNextItemWidth(-70);
    ImGui::DragFloat("Far##cam",  &camera->farPlane,  1.0f,  10.0f,  5000.0f, "%.0f");

    ImGui::Spacing();
    ImGui::SeparatorText("Movement");

    ImGui::SetNextItemWidth(-70);
    ImGui::DragFloat("Speed##cam",  &camera->moveSpeed,  0.5f, 0.5f, 500.0f, "%.1f");

    ImGui::SetNextItemWidth(-70);
    ImGui::DragFloat3("Position##cam", &camera->fpsPos.x, 0.1f);

    ImGui::Spacing();
    ImGui::BeginDisabled();
    ImGui::Checkbox("FPS Mode", &camera->fpsMode);
    ImGui::EndDisabled();

    ImGui::End();
}

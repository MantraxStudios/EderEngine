#include "InspectorPanel.h"
#include "ECS/Components/TagComponent.h"
#include "ECS/Components/TransformComponent.h"
#include "ECS/Components/MeshRendererComponent.h"
#include "ECS/Components/LightComponent.h"
#include <imgui/imgui.h>
#include <cstring>

// ─────────────────────────────────────────────────────────────────────────────
// Helper: component header with right-click "Remove" option.
// Returns true if the header is open AND the component was NOT removed.
// ─────────────────────────────────────────────────────────────────────────────
template<typename T>
static bool ComponentHeader(const char* label, Registry* reg, Entity e)
{
    bool open    = ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen);
    bool removed = false;

    // Right-click context menu on the header item
    if (ImGui::BeginPopupContextItem())
    {
        if (ImGui::MenuItem("Remove Component"))
        {
            reg->Remove<T>(e);
            removed = true;
        }
        ImGui::EndPopup();
    }

    return open && !removed;
}

// ─────────────────────────────────────────────────────────────────────────────
// OnDraw
// ─────────────────────────────────────────────────────────────────────────────

void InspectorPanel::OnDraw()
{
    if (!ImGui::Begin(Title(), &open)) { ImGui::End(); return; }

    if (!registry || selected == NULL_ENTITY)
    {
        ImGui::TextDisabled("No entity selected");
        ImGui::End();
        return;
    }

    // ── Entity header ────────────────────────────────────────────────────────
    ImGui::Text("Entity  #%u", selected);
    ImGui::Separator();
    ImGui::Spacing();

    DrawTagComponent();
    DrawTransformComponent();
    DrawMeshRendererComponent();
    DrawLightComponent();

    // ── Add Component ────────────────────────────────────────────────────────
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    DrawAddComponent();

    ImGui::End();
}

// ─────────────────────────────────────────────────────────────────────────────
// Component sections
// ─────────────────────────────────────────────────────────────────────────────

void InspectorPanel::DrawTagComponent()
{
    if (!registry->Has<TagComponent>(selected)) return;

    // Tag is not removable — use plain CollapsingHeader
    if (ImGui::CollapsingHeader("Tag", ImGuiTreeNodeFlags_DefaultOpen))
    {
        auto& tag = registry->Get<TagComponent>(selected);
        char buf[128];
        strncpy(buf, tag.name.c_str(), sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##tagname", buf, sizeof(buf)))
            tag.name = buf;
    }
}

void InspectorPanel::DrawTransformComponent()
{
    if (!registry->Has<TransformComponent>(selected)) return;

    if (ComponentHeader<TransformComponent>("Transform", registry, selected))
    {
        auto& t = registry->Get<TransformComponent>(selected);
        ImGui::DragFloat3("Position", &t.position.x, 0.05f);
        ImGui::DragFloat3("Rotation", &t.rotation.x, 0.5f);
        ImGui::DragFloat3("Scale",    &t.scale.x,    0.05f, 0.001f, 100.0f);
    }
}

void InspectorPanel::DrawMeshRendererComponent()
{
    if (!registry->Has<MeshRendererComponent>(selected)) return;

    if (ComponentHeader<MeshRendererComponent>("Mesh Renderer", registry, selected))
    {
        auto& m = registry->Get<MeshRendererComponent>(selected);

        ImGui::LabelText("Mesh",     "%s", m.meshPath.empty()     ? "(none)" : m.meshPath.c_str());
        ImGui::LabelText("Material", "%s", m.materialPath.empty() ? "(none)" : m.materialPath.c_str());

        ImGui::Spacing();
        ImGui::Checkbox("Visible",     &m.visible);
        ImGui::SameLine();
        ImGui::Checkbox("Cast Shadow", &m.castShadow);
    }
}

void InspectorPanel::DrawLightComponent()
{
    if (!registry->Has<LightComponent>(selected)) return;

    if (ComponentHeader<LightComponent>("Light", registry, selected))
    {
        auto& l = registry->Get<LightComponent>(selected);

        const char* types[] = { "Directional", "Point", "Spot" };
        int typeIdx = static_cast<int>(l.type);
        if (ImGui::Combo("Type", &typeIdx, types, IM_ARRAYSIZE(types)))
            l.type = static_cast<LightType>(typeIdx);

        ImGui::ColorEdit3("Color",     &l.color.x);
        ImGui::DragFloat ("Intensity", &l.intensity, 0.05f, 0.0f, 100.0f);

        if (l.type != LightType::Directional)
            ImGui::DragFloat("Range", &l.range, 0.5f, 0.0f, 1000.0f);

        if (l.type == LightType::Spot)
        {
            ImGui::DragFloat("Inner Angle", &l.innerConeAngle, 0.5f, 0.0f, 89.0f);
            ImGui::DragFloat("Outer Angle", &l.outerConeAngle, 0.5f, 1.0f, 90.0f);
        }

        ImGui::Spacing();
        ImGui::Checkbox("Cast Shadow", &l.castShadow);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Add Component
// ─────────────────────────────────────────────────────────────────────────────

void InspectorPanel::DrawAddComponent()
{
    // Check if there is at least one component that can be added
    const bool canAddTransform     = !registry->Has<TransformComponent>(selected);
    const bool canAddMeshRenderer  = !registry->Has<MeshRendererComponent>(selected);
    const bool canAddLight         = !registry->Has<LightComponent>(selected);
    const bool anyAvailable        = canAddTransform || canAddMeshRenderer || canAddLight;

    float btnW = ImGui::GetContentRegionAvail().x;
    if (!anyAvailable) ImGui::BeginDisabled();
    if (ImGui::Button("Add Component", ImVec2(btnW, 0)))
        ImGui::OpenPopup("##add_comp_popup");
    if (!anyAvailable) ImGui::EndDisabled();

    if (ImGui::BeginPopup("##add_comp_popup"))
    {
        if (canAddTransform && ImGui::MenuItem("Transform"))
            registry->Add<TransformComponent>(selected);

        if (canAddMeshRenderer && ImGui::MenuItem("Mesh Renderer"))
            registry->Add<MeshRendererComponent>(selected);

        if (canAddLight && ImGui::MenuItem("Light"))
            registry->Add<LightComponent>(selected);

        ImGui::EndPopup();
    }
}

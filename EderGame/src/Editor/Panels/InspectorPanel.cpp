#include "InspectorPanel.h"
#include "ECS/Components/TagComponent.h"
#include "ECS/Components/TransformComponent.h"
#include "ECS/Components/MeshRendererComponent.h"
#include "ECS/Components/LightComponent.h"
#include "ECS/Components/SunShaftsComponent.h"
#include "ECS/Components/VolumetricLightComponent.h"
#include "ECS/Components/VolumetricFogComponent.h"
#include "ECS/Components/AnimationComponent.h"
#include "Core/MaterialManager.h"
#include "Core/Material.h"
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <cstring>

static void ComponentStripe(ImVec4 color)
{
    ImVec2 min = ImGui::GetItemRectMin();
    ImVec2 max = ImVec2(min.x + 3.0f, ImGui::GetItemRectMax().y);
    ImGui::GetWindowDrawList()->AddRectFilled(min, max, ImGui::ColorConvertFloat4ToU32(color));
}

template<typename T>
static bool ComponentHeader(const char* label, Registry* reg, Entity e, ImVec4 stripe)
{
    bool open    = ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen);
    ComponentStripe(stripe);
    bool removed = false;
    if (ImGui::BeginPopupContextItem())
    {
        ImGui::TextDisabled("%s", label);
        ImGui::Separator();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
        if (ImGui::MenuItem("Remove Component")) { reg->Remove<T>(e); removed = true; }
        ImGui::PopStyleColor();
        ImGui::EndPopup();
    }
    return open && !removed;
}

static bool Vec3Row(const char* label, float* v, float speed,
                    float vMin = -FLT_MAX, float vMax = FLT_MAX, float resetVal = 0.0f)
{
    bool changed = false;
    ImGui::PushID(label);
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", label);
    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-28);
    changed = ImGui::DragFloat3("##v", v, speed, vMin, vMax);
    ImGui::TableSetColumnIndex(2);
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.25f, 0.25f, 0.25f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.45f, 0.45f, 0.45f, 1.0f));
    if (ImGui::Button("R##r", ImVec2(22, 0)))
    { v[0] = v[1] = v[2] = resetVal; changed = true; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reset");
    ImGui::PopStyleColor(2);
    ImGui::PopID();
    return changed;
}

void InspectorPanel::OnDraw()
{
    if (!ImGui::Begin(Title(), &open)) { ImGui::End(); return; }

    if (!registry || selected == NULL_ENTITY)
    {
        ImVec2 avail = ImGui::GetContentRegionAvail();
        const char* hint = "Select an actor in the\nWorld Outliner to edit it.";
        ImVec2 sz = ImGui::CalcTextSize(hint, nullptr, false, avail.x);
        ImGui::SetCursorPosY((avail.y - sz.y) * 0.45f);
        ImGui::SetCursorPosX((avail.x - sz.x) * 0.5f);
        ImGui::TextDisabled("%s", hint);
        ImGui::End();
        return;
    }

    const char* entityName = registry->Has<TagComponent>(selected)
        ? registry->Get<TagComponent>(selected).name.c_str()
        : "Entity";
    ImGui::TextColored(ImVec4(0.92f, 0.92f, 0.92f, 1.0f), "%s", entityName);
    ImGui::SameLine();
    ImGui::TextDisabled("  #%u", selected);
    ImGui::Separator();
    ImGui::Spacing();

    DrawTagComponent();
    DrawTransformComponent();
    DrawMeshRendererComponent();
    DrawLightComponent();
    DrawSunShaftsComponent();
    DrawVolumetricLightComponent();
    DrawVolumetricFogComponent();
    DrawAnimationComponent();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    DrawAddComponent();

    ImGui::End();
}

void InspectorPanel::DrawTagComponent()
{
    if (!registry->Has<TagComponent>(selected)) return;
    ImGui::PushID("Tag");
    if (ImGui::CollapsingHeader("Tag", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ComponentStripe(ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
        auto& tag = registry->Get<TagComponent>(selected);
        char buf[128];
        strncpy(buf, tag.name.c_str(), sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##tagname", buf, sizeof(buf)))
            tag.name = buf;
    }
    ImGui::PopID();
}

void InspectorPanel::DrawTransformComponent()
{
    if (!registry->Has<TransformComponent>(selected)) return;
    ImGui::PushID("Transform");
    if (ComponentHeader<TransformComponent>(
            "Transform", registry, selected, ImVec4(0.25f, 0.85f, 0.45f, 1.0f)))
    {
        auto& t = registry->Get<TransformComponent>(selected);
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(4, 3));
        if (ImGui::BeginTable("##xform", 3, ImGuiTableFlags_None))
        {
            ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed,  68.0f);
            ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("reset", ImGuiTableColumnFlags_WidthFixed,  26.0f);
            Vec3Row("Location", &t.position.x, 0.05f, -FLT_MAX, FLT_MAX, 0.0f);
            Vec3Row("Rotation", &t.rotation.x, 0.5f,  -FLT_MAX, FLT_MAX, 0.0f);
            Vec3Row("Scale",    &t.scale.x,    0.01f,  0.001f,   100.0f,  1.0f);
            ImGui::EndTable();
        }
        ImGui::PopStyleVar();
    }
    ImGui::PopID();
}

void InspectorPanel::DrawMeshRendererComponent()
{
    if (!registry->Has<MeshRendererComponent>(selected)) return;
    ImGui::PushID("MeshRenderer");
    if (ComponentHeader<MeshRendererComponent>(
            "Mesh Renderer", registry, selected, ImVec4(0.30f, 0.60f, 1.0f, 1.0f)))
    {
        auto& m = registry->Get<MeshRendererComponent>(selected);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.10f, 0.10f, 0.10f, 1.0f));
        ImGui::LabelText("Mesh##mr",  "%s", m.meshPath.empty() ? "(none)" : m.meshPath.c_str());
        ImGui::LabelText("Material##mr", "%s", m.materialName.c_str());
        ImGui::PopStyleColor();
        ImGui::Spacing();
        ImGui::Checkbox("Visible",     &m.visible);
        ImGui::SameLine(120);
        ImGui::Checkbox("Cast Shadow", &m.castShadow);

        // ── Alpha mode (edits shared material) ──
        Material* mat = MaterialManager::Get().Has(m.materialName)
                        ? &MaterialManager::Get().Get(m.materialName) : nullptr;
        if (mat)
        {
            ImGui::Spacing();
            ImGui::TextDisabled("-- Alpha --");
            const char* modes[] = { "Opaque", "Alpha Test (Cutout)", "Alpha Blend" };
            int current = static_cast<int>(mat->alphaMode);
            if (ImGui::Combo("Alpha Mode", &current, modes, 3))
            {
                mat->alphaMode = static_cast<Material::AlphaMode>(current);
                // For blend, lower opacity so IsTransparent() returns true
                if (mat->alphaMode == Material::AlphaMode::AlphaBlend && mat->opacity >= 0.999f)
                    mat->opacity = 0.5f;
                else if (mat->alphaMode != Material::AlphaMode::AlphaBlend)
                    mat->opacity = 1.0f;
            }
            if (mat->alphaMode == Material::AlphaMode::AlphaTest)
                ImGui::DragFloat("Cutoff", &mat->alphaCutoff, 0.01f, 0.0f, 1.0f);
            if (mat->alphaMode == Material::AlphaMode::AlphaBlend)
                ImGui::DragFloat("Opacity", &mat->opacity, 0.01f, 0.0f, 1.0f);
        }
    }
    ImGui::PopID();
}

void InspectorPanel::DrawLightComponent()
{
    if (!registry->Has<LightComponent>(selected)) return;
    ImGui::PushID("Light");
    if (ComponentHeader<LightComponent>(
            "Light", registry, selected, ImVec4(1.0f, 0.85f, 0.20f, 1.0f)))
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
    ImGui::PopID();
}

void InspectorPanel::DrawSunShaftsComponent()
{
    if (!registry->Has<SunShaftsComponent>(selected)) return;
    ImGui::PushID("SunShafts");
    if (ComponentHeader<SunShaftsComponent>("Sun Shafts", registry, selected, ImVec4(1.0f, 0.7f, 0.2f, 1.0f)))
    {
        auto& s = registry->Get<SunShaftsComponent>(selected);
        ImGui::Checkbox("Enabled",      &s.enabled);
        ImGui::Separator();
        ImGui::TextDisabled("-- Shafts (god rays) --");
        ImGui::DragFloat("Density",     &s.density,    0.05f,  0.0f, 20.0f);
        ImGui::DragFloat("Weight",      &s.weight,     0.01f,  0.0f,  3.0f);
        ImGui::DragFloat("Decay",       &s.decay,      0.005f, 0.5f,  0.999f);
        ImGui::DragFloat("Sun Radius",  &s.sunRadius,  0.002f, 0.005f, 0.2f);
        ImGui::Separator();
        ImGui::TextDisabled("-- Bloom / Glare --");
        ImGui::DragFloat("Bloom Scale", &s.bloomScale, 0.05f, 0.0f, 10.0f);
        ImGui::DragFloat("Exposure",    &s.exposure,   0.005f,0.0f,  1.0f);
        ImGui::Separator();
        ImGui::ColorEdit3("Tint",       &s.tint.x);
    }
    ImGui::PopID();
}

void InspectorPanel::DrawVolumetricLightComponent()
{
    if (!registry->Has<VolumetricLightComponent>(selected)) return;
    ImGui::PushID("VolumetricLight");
    if (ComponentHeader<VolumetricLightComponent>("Volumetric Light", registry, selected, ImVec4(0.4f, 0.8f, 1.0f, 1.0f)))
    {
        auto& v = registry->Get<VolumetricLightComponent>(selected);
        ImGui::Checkbox("Enabled",      &v.enabled);
        ImGui::Separator();
        ImGui::TextDisabled("-- Ray March --");
        ImGui::DragInt  ("Steps",       &v.numSteps,    1,     8,    256);
        ImGui::DragFloat("Max Distance",&v.maxDistance, 1.0f,  1.0f, 500.0f);
        ImGui::DragFloat("Jitter",      &v.jitter,      0.05f, 0.0f, 2.0f);
        ImGui::Separator();
        ImGui::TextDisabled("-- Participating Medium --");
        ImGui::DragFloat("Density",     &v.density,     0.001f, 0.0f, 1.0f, "%.4f");
        ImGui::DragFloat("Absorption",  &v.absorption,  0.001f, 0.0f, 1.0f, "%.4f");
        ImGui::DragFloat("Anisotropy g",&v.g,           0.01f, -0.99f, 0.99f);
        ImGui::Separator();
        ImGui::TextDisabled("-- Output --");
        ImGui::DragFloat("Intensity",   &v.intensity,   0.01f, 0.0f, 5.0f);
        ImGui::ColorEdit3("Tint",       &v.tint.x);
    }
    ImGui::PopID();
}

void InspectorPanel::DrawVolumetricFogComponent()
{
    if (!registry->Has<VolumetricFogComponent>(selected)) return;
    ImGui::PushID("VolumetricFog");
    if (ComponentHeader<VolumetricFogComponent>("Volumetric Fog", registry, selected, ImVec4(0.6f, 0.85f, 0.6f, 1.0f)))
    {
        auto& f = registry->Get<VolumetricFogComponent>(selected);
        ImGui::Checkbox("Enabled",          &f.enabled);
        ImGui::Separator();
        ImGui::TextDisabled("-- Colour --");
        ImGui::ColorEdit3("Fog Color",       &f.fogColor.x);
        ImGui::ColorEdit3("Horizon Color",   &f.horizonColor.x);
        ImGui::ColorEdit3("Sun Scatter",     &f.sunScatterColor.x);
        ImGui::Separator();
        ImGui::TextDisabled("-- Density --");
        ImGui::DragFloat("Density",          &f.density,       0.001f, 0.0f, 1.0f, "%.4f");
        ImGui::DragFloat("Height Falloff",   &f.heightFalloff, 0.005f, 0.0f, 2.0f, "%.4f");
        ImGui::DragFloat("Height Offset",    &f.heightOffset,  0.5f,  -100.0f, 100.0f);
        ImGui::Separator();
        ImGui::TextDisabled("-- Distance --");
        ImGui::DragFloat("Fog Start",        &f.fogStart,      0.5f,   0.0f, 500.0f);
        ImGui::DragFloat("Fog End",          &f.fogEnd,        1.0f,   1.0f, 2000.0f);
        ImGui::Separator();
        ImGui::TextDisabled("-- Scatter --");
        ImGui::DragFloat("Scatter Strength", &f.scatterStrength,0.01f, 0.0f, 3.0f);
        ImGui::DragFloat("Max Opacity",      &f.maxFogAmount,  0.01f,  0.0f, 1.0f);
    }
    ImGui::PopID();
}

void InspectorPanel::DrawAnimationComponent()
{
    if (!registry->Has<AnimationComponent>(selected)) return;
    ImGui::PushID("Animation");
    if (ComponentHeader<AnimationComponent>("Animation", registry, selected, ImVec4(0.8f, 0.5f, 1.0f, 1.0f)))
    {
        auto& a = registry->Get<AnimationComponent>(selected);
        ImGui::Checkbox("Playing", &a.playing);
        ImGui::SameLine(120);
        ImGui::Checkbox("Loop",    &a.loop);
        ImGui::DragInt  ("Clip Index", &a.animIndex, 1, 0, 255);
        ImGui::DragFloat("Speed",      &a.speed,     0.01f, 0.0f, 10.0f);

        ImGui::Separator();
        ImGui::TextDisabled("-- Playback --");
        ImGui::Text("Time: %.2f s", a.currentTime);
        if (ImGui::Button("Reset"))
            a.currentTime = 0.0f;
    }
    ImGui::PopID();
}

void InspectorPanel::DrawAddComponent()
{
    const bool canAddTransform    = !registry->Has<TransformComponent>(selected);
    const bool canAddMeshRenderer = !registry->Has<MeshRendererComponent>(selected);
    const bool canAddLight        = !registry->Has<LightComponent>(selected);
    const bool canAddSunShafts    = !registry->Has<SunShaftsComponent>(selected);
    const bool canAddVolumetric   = !registry->Has<VolumetricLightComponent>(selected);
    const bool canAddFog          = !registry->Has<VolumetricFogComponent>(selected);
    const bool canAddAnim         = !registry->Has<AnimationComponent>(selected);
    const bool anyAvailable       = canAddTransform || canAddMeshRenderer || canAddLight
                                 || canAddSunShafts || canAddVolumetric || canAddFog || canAddAnim;

    float btnW = ImGui::GetContentRegionAvail().x;
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.18f, 0.18f, 0.18f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.80f, 0.10f, 0.10f, 0.60f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.80f, 0.10f, 0.10f, 1.0f));
    if (!anyAvailable) ImGui::BeginDisabled();
    if (ImGui::Button("+ Add Component", ImVec2(btnW, 0)))
        ImGui::OpenPopup("##add_comp_popup");
    if (!anyAvailable) ImGui::EndDisabled();
    ImGui::PopStyleColor(3);

    if (ImGui::BeginPopup("##add_comp_popup"))
    {
        ImGui::TextDisabled("COMPONENTS");
        ImGui::Separator();
        if (canAddTransform    && ImGui::MenuItem("   Transform"))        registry->Add<TransformComponent>(selected);
        if (canAddMeshRenderer && ImGui::MenuItem("   Mesh Renderer"))    registry->Add<MeshRendererComponent>(selected);
        if (canAddLight        && ImGui::MenuItem("   Light"))            registry->Add<LightComponent>(selected);
        if (canAddSunShafts    && ImGui::MenuItem("   Sun Shafts"))       registry->Add<SunShaftsComponent>(selected);
        if (canAddVolumetric   && ImGui::MenuItem("   Volumetric Light")) registry->Add<VolumetricLightComponent>(selected);
        if (canAddFog          && ImGui::MenuItem("   Volumetric Fog"))   registry->Add<VolumetricFogComponent>(selected);
        if (canAddAnim         && ImGui::MenuItem("   Animation"))        registry->Add<AnimationComponent>(selected);
        ImGui::EndPopup();
    }
}

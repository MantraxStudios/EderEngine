#include "InspectorPanel.h"
#include "ECS/Components/TagComponent.h"
#include "ECS/Components/TransformComponent.h"
#include "ECS/Components/HierarchyComponent.h"
#include "ECS/Components/MeshRendererComponent.h"
#include "ECS/Components/LightComponent.h"
#include "ECS/Components/VolumetricFogComponent.h"
#include "ECS/Components/AnimationComponent.h"
#include "ECS/Components/RigidbodyComponent.h"
#include "ECS/Components/ColliderComponent.h"
#include "ECS/Components/ScriptComponent.h"
#include "ECS/Components/CharacterControllerComponent.h"
#include "ECS/Systems/TransformSystem.h"
#include "Physics/PhysicsSystem.h"
#include "Scripting/LuaScriptSystem.h"
#include "Core/MaterialManager.h"
#include "Core/Material.h"
#include <IO/AssetManager.h>
#include <filesystem>
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

// ─────────────────────────────────────────────────────────────────────────────
//  AssetDropField
//  Renders a Unity-style row:   [Label]  [ asset name            ↓ ]
//  The box is a drag-drop target.  Returns true when a compatible asset
//  is dropped and fills outPath / outGuid.
// ─────────────────────────────────────────────────────────────────────────────
bool InspectorPanel::AssetDropField(const char*         label,
                                    Krayon::AssetType   expectedType,
                                    const std::string&  currentPath,
                                    std::string&        outPath,
                                    uint64_t&           outGuid)
{
    using namespace Krayon;
    bool changed = false;

    // Derive display name from the current path (file stem)
    std::string stem = currentPath.empty() ? "(none)"
        : std::filesystem::path(currentPath).stem().string();

    ImGui::PushID(label);

    // Label column
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", label);
    ImGui::SameLine(110);

    // Detect whether a compatible payload is being hovered
    bool canDrop = false;
    if (ImGui::GetDragDropPayload() &&
        ImGui::GetDragDropPayload()->IsDataType("ASSET_GUID"))
    {
        uint64_t hoverGuid = *reinterpret_cast<const uint64_t*>(
            ImGui::GetDragDropPayload()->Data);
        const AssetMeta* hm = AssetManager::Get().FindByGuid(hoverGuid);
        canDrop = hm && (expectedType == AssetType::Unknown || hm->type == expectedType);
    }

    // Draw a styled button that looks like an input field
    const float btnW = ImGui::GetContentRegionAvail().x;
    ImVec4 bgCol = canDrop
        ? ImVec4(0.20f, 0.50f, 0.20f, 0.85f)   // green tint while valid payload hovered
        : ImVec4(0.12f, 0.12f, 0.12f, 1.00f);

    ImGui::PushStyleColor(ImGuiCol_Button,        bgCol);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.20f, 0.25f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  bgCol);

    // Show truncated stem + small arrow indicator
    std::string btnLabel = stem;
    if (btnLabel.size() > 22) btnLabel = btnLabel.substr(0, 19) + "...";
    btnLabel += "  \xce\xb2"; // β as a small "select" hint glyph
    ImGui::Button(btnLabel.c_str(), ImVec2(btnW, 0));

    ImGui::PopStyleColor(3);

    // Tooltip with full path
    if (!currentPath.empty() && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", currentPath.c_str());

    // Drag-drop target
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ASSET_GUID"))
        {
            uint64_t droppedGuid = *reinterpret_cast<const uint64_t*>(p->Data);
            const AssetMeta* meta = AssetManager::Get().FindByGuid(droppedGuid);
            if (meta && (expectedType == AssetType::Unknown || meta->type == expectedType))
            {
                outPath = meta->path;
                outGuid = droppedGuid;
                changed = true;
            }
        }
        ImGui::EndDragDropTarget();
    }

    ImGui::PopID();
    return changed;
}

// ─────────────────────────────────────────────────────────────────────────────

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
    DrawHierarchyComponent();
    DrawTransformComponent();
    DrawMeshRendererComponent();
    DrawLightComponent();
    DrawVolumetricFogComponent();
    DrawAnimationComponent();
    DrawRigidbodyComponent();
    DrawColliderComponent();
    DrawCharacterControllerComponent();
    DrawScriptComponent();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    DrawAddComponent();

    ImGui::End();
}

// ─────────────────────────────────────────────────────────────────────────────

void InspectorPanel::DrawHierarchyComponent()
{
    bool hasHier   = registry->Has<HierarchyComponent>(selected);
    bool hasParent = hasHier && registry->Get<HierarchyComponent>(selected).parent != NULL_ENTITY;

    if (!hasParent) return;   // Only show section when actually parented

    ImGui::PushID("Hierarchy");
    if (ImGui::CollapsingHeader("Hierarchy", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ComponentStripe(ImVec4(0.85f, 0.60f, 0.10f, 1.0f));

        Entity parent = registry->Get<HierarchyComponent>(selected).parent;
        const char* parentName = "Unknown";
        if (registry->Has<TagComponent>(parent))
            parentName = registry->Get<TagComponent>(parent).name.c_str();

        // Parent row
        if (ImGui::BeginTable("##hierTable", 2, ImGuiTableFlags_None))
        {
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("Parent");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%s  (#%u)", parentName, parent);

            ImGui::EndTable();
        }

        // Detach button
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.55f, 0.20f, 0.05f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.75f, 0.28f, 0.08f, 1.0f));
        if (ImGui::Button("Detach from Parent", ImVec2(-1, 0)))
            TransformSystem::Detach(selected, *registry);
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Removes parent link while preserving world position");

        ImGui::Spacing();
    }
    ImGui::PopID();
}

// ─────────────────────────────────────────────────────────────────────────────

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

        const glm::vec3 prevScale = t.scale;

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

        // Scale affects shape geometry — force physics actor rebuild immediately.
        // Position / rotation are handled every frame via setGlobalPose in SyncActors.
        if (t.scale != prevScale && registry->Has<ColliderComponent>(selected))
            PhysicsSystem::Get().MarkDirty(selected);
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

        // ── Mesh asset drop slot ──────────────────────────────────
        {
            std::string newPath; uint64_t newGuid = 0;
            if (AssetDropField("Mesh", Krayon::AssetType::Mesh, m.meshPath, newPath, newGuid))
            {
                m.meshGuid = newGuid;
                m.meshPath = newPath;
            }
        }

        // ── Material asset drop slot ──────────────────────────────
        {
            // Resolve display path from materialGuid if available
            std::string matDisplayPath = m.materialName;
            if (m.materialGuid != 0)
            {
                const auto* mm = Krayon::AssetManager::Get().FindByGuid(m.materialGuid);
                if (mm) matDisplayPath = mm->path;
            }
            std::string newPath; uint64_t newGuid = 0;
            if (AssetDropField("Material", Krayon::AssetType::Material,
                               matDisplayPath, newPath, newGuid))
            {
                m.materialGuid = newGuid;
                // derive materialName from file stem for existing pipeline
                const auto* mm = Krayon::AssetManager::Get().FindByGuid(newGuid);
                if (mm) m.materialName = mm->name;
            }
        }
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

        // -- Volumetric Light (all light types) --
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Volumetric Light");
        ImGui::Checkbox("Enabled##vol", &l.volumetricEnabled);
        if (l.volumetricEnabled)
        {
            ImGui::TextDisabled("-- Ray March --");
            ImGui::DragInt  ("Steps##vol",        &l.volNumSteps,    1,     8,    256);
            ImGui::DragFloat("Max Distance##vol", &l.volMaxDistance, 1.0f,  1.0f, 500.0f);
            ImGui::DragFloat("Jitter##vol",       &l.volJitter,      0.05f, 0.0f, 2.0f);
            ImGui::TextDisabled("-- Participating Medium --");
            ImGui::DragFloat("Density##vol",      &l.volDensity,     0.001f, 0.0f, 1.0f, "%.4f");
            ImGui::DragFloat("Absorption##vol",   &l.volAbsorption,  0.001f, 0.0f, 1.0f, "%.4f");
            ImGui::DragFloat("Anisotropy g##vol", &l.volG,           0.01f, -0.99f, 0.99f);
            ImGui::TextDisabled("-- Output --");
            ImGui::DragFloat("Intensity##vol",    &l.volIntensity,   0.01f, 0.0f, 5.0f);
            ImGui::ColorEdit3("Tint##vol",        &l.volTint.x);
        }

        if (l.type == LightType::Directional)
        {
            // -- Sun Shafts (Directional only) --
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "Sun Shafts");
            ImGui::Checkbox("Enabled##ss", &l.sunShaftsEnabled);
            if (l.sunShaftsEnabled)
            {
                ImGui::TextDisabled("-- Shafts (god rays) --");
                ImGui::DragFloat("Density##ss",     &l.shaftsDensity,    0.05f,  0.0f, 20.0f);
                ImGui::DragFloat("Weight##ss",      &l.shaftsWeight,     0.01f,  0.0f,  3.0f);
                ImGui::DragFloat("Decay##ss",       &l.shaftsDecay,      0.005f, 0.5f,  0.999f);
                ImGui::DragFloat("Sun Radius##ss",  &l.shaftsSunRadius,  0.002f, 0.005f, 0.2f);
                ImGui::TextDisabled("-- Bloom / Glare --");
                ImGui::DragFloat("Bloom Scale##ss", &l.shaftsBloomScale, 0.05f, 0.0f, 10.0f);
                ImGui::DragFloat("Exposure##ss",    &l.shaftsExposure,   0.005f, 0.0f, 1.0f);
                ImGui::Separator();
                ImGui::ColorEdit3("Tint##ss",       &l.shaftsTint.x);
            }
        }
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

        // ── Model/FBX drop slot ───────────────────────────────────
        // Animations are embedded in the FBX; dropping here also sets
        // the MeshRendererComponent's mesh on the same entity.
        {
            std::string currentModel;
            uint64_t    currentGuid = 0;
            if (registry->Has<MeshRendererComponent>(selected))
            {
                auto& mr2   = registry->Get<MeshRendererComponent>(selected);
                currentModel = mr2.meshPath;
                currentGuid  = mr2.meshGuid;
                // If we only have a path but no GUID yet, try to resolve it
                if (currentGuid == 0 && !currentModel.empty())
                    currentGuid = Krayon::AssetManager::Get().GetGuid(currentModel);
            }

            std::string newPath; uint64_t newGuid = 0;
            if (AssetDropField("Model", Krayon::AssetType::Mesh, currentModel, newPath, newGuid))
            {
                if (registry->Has<MeshRendererComponent>(selected))
                {
                    auto& mr2  = registry->Get<MeshRendererComponent>(selected);
                    mr2.meshGuid = newGuid;
                    mr2.meshPath = newPath;
                }
            }
        }

        ImGui::Spacing();
        ImGui::Checkbox("Playing", &a.playing);
        ImGui::SameLine(120);
        ImGui::Checkbox("Loop",    &a.loop);
        ImGui::DragInt  ("Clip Index",       &a.animIndex,      1,     0,    255);
        ImGui::DragFloat("Speed",            &a.speed,          0.01f, 0.0f, 10.0f);
        ImGui::DragFloat("Blend Duration",   &a.blendDuration,  0.01f, 0.0f, 2.0f, "%.2f s");

        ImGui::Separator();
        ImGui::TextDisabled("-- Playback --");
        ImGui::Text("Time: %.2f s", a.currentTime);

        // Show blend progress bar while a crossfade is active
        if (a.prevIndex >= 0 && a.blendDuration > 0.0f)
        {
            float progress = glm::clamp(a.blendTime / a.blendDuration, 0.0f, 1.0f);
            char overlay[32];
            snprintf(overlay, sizeof(overlay), "Blend %.0f%%", progress * 100.0f);
            ImGui::ProgressBar(progress, ImVec2(-1, 0), overlay);
        }

        if (ImGui::Button("Reset"))
        {
            a.currentTime = 0.0f;
            a.prevIndex   = -1;
            a.blendTime   = 0.0f;
        }
    }
    ImGui::PopID();
}

// ─────────────────────────────────────────────────────────────────────────────

void InspectorPanel::DrawRigidbodyComponent()
{
    if (!registry->Has<RigidbodyComponent>(selected)) return;

    if (ComponentHeader<RigidbodyComponent>("Rigidbody", registry, selected, ImVec4(0.3f, 0.6f, 1.0f, 1.0f)))
    {
        auto& rb = registry->Get<RigidbodyComponent>(selected);

        // snapshot for dirty detection
        const float prevMass        = rb.mass;
        const float prevLinearDrag  = rb.linearDrag;
        const float prevAngularDrag = rb.angularDrag;
        const bool  prevUseGravity  = rb.useGravity;
        const bool  prevKinematic   = rb.isKinematic;

        ImGui::Columns(2, "rb_cols", false);
        ImGui::SetColumnWidth(0, 130.0f);

        ImGui::Text("Mass");        ImGui::NextColumn();
        ImGui::SetNextItemWidth(-1);
        ImGui::DragFloat("##rb_mass", &rb.mass, 0.1f, 0.001f, 9999.0f, "%.2f kg");
        ImGui::NextColumn();

        ImGui::Text("Linear Drag"); ImGui::NextColumn();
        ImGui::SetNextItemWidth(-1);
        ImGui::DragFloat("##rb_ldrag", &rb.linearDrag, 0.001f, 0.0f, 10.0f, "%.3f");
        ImGui::NextColumn();

        ImGui::Text("Angular Drag"); ImGui::NextColumn();
        ImGui::SetNextItemWidth(-1);
        ImGui::DragFloat("##rb_adrag", &rb.angularDrag, 0.001f, 0.0f, 10.0f, "%.3f");
        ImGui::NextColumn();

        ImGui::Text("Use Gravity"); ImGui::NextColumn();
        ImGui::Checkbox("##rb_grav", &rb.useGravity);
        ImGui::NextColumn();

        ImGui::Text("Kinematic");  ImGui::NextColumn();
        ImGui::Checkbox("##rb_kin", &rb.isKinematic);
        ImGui::NextColumn();

        ImGui::Columns(1);

        // Read-only velocity
        ImGui::Spacing();
        ImGui::TextDisabled("Linear Vel:  (%.2f, %.2f, %.2f)",
            rb.linearVelocity.x, rb.linearVelocity.y, rb.linearVelocity.z);
        ImGui::TextDisabled("Angular Vel: (%.2f, %.2f, %.2f)",
            rb.angularVelocity.x, rb.angularVelocity.y, rb.angularVelocity.z);

        ImGui::Spacing();

        // Apply changes to PhysX if any field was modified
        if (rb.mass        != prevMass        ||
            rb.linearDrag  != prevLinearDrag  ||
            rb.angularDrag != prevAngularDrag ||
            rb.useGravity  != prevUseGravity  ||
            rb.isKinematic != prevKinematic)
        {
            PhysicsSystem::Get().MarkDirty(selected);
        }
    }
}

void InspectorPanel::DrawColliderComponent()
{
    if (!registry->Has<ColliderComponent>(selected)) return;

    if (ComponentHeader<ColliderComponent>("Collider", registry, selected, ImVec4(0.2f, 0.9f, 0.4f, 1.0f)))
    {
        auto& col = registry->Get<ColliderComponent>(selected);

        // snapshot for dirty detection
        const ColliderShape prevShape      = col.shape;
        const glm::vec3     prevHalfExt    = col.boxHalfExtents;
        const float         prevRadius     = col.radius;
        const float         prevCapsuleHH  = col.capsuleHalfHeight;
        const glm::vec3     prevCenter     = col.center;
        const float         prevStaticFric = col.staticFriction;
        const float         prevDynFric    = col.dynamicFriction;
        const float         prevRest       = col.restitution;
        const bool          prevTrigger    = col.isTrigger;

        // Shape selector
        const char* shapes[] = { "Box", "Sphere", "Capsule" };
        int shapeIdx = static_cast<int>(col.shape);
        ImGui::Text("Shape"); ImGui::SameLine(130.0f);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::Combo("##col_shape", &shapeIdx, shapes, 3))
            col.shape = static_cast<ColliderShape>(shapeIdx);

        ImGui::Columns(2, "col_cols", false);
        ImGui::SetColumnWidth(0, 130.0f);

        if (col.shape == ColliderShape::Box)
        {
            ImGui::Text("Half Extents"); ImGui::NextColumn();
            ImGui::SetNextItemWidth(-1);
            ImGui::DragFloat3("##col_hext", &col.boxHalfExtents.x, 0.01f, 0.001f, 999.0f, "%.3f");
            ImGui::NextColumn();
        }
        else if (col.shape == ColliderShape::Sphere)
        {
            ImGui::Text("Radius"); ImGui::NextColumn();
            ImGui::SetNextItemWidth(-1);
            ImGui::DragFloat("##col_rad", &col.radius, 0.01f, 0.001f, 999.0f, "%.3f");
            ImGui::NextColumn();
        }
        else // Capsule
        {
            ImGui::Text("Radius"); ImGui::NextColumn();
            ImGui::SetNextItemWidth(-1);
            ImGui::DragFloat("##col_crad", &col.radius, 0.01f, 0.001f, 999.0f, "%.3f");
            ImGui::NextColumn();

            ImGui::Text("Half Height"); ImGui::NextColumn();
            ImGui::SetNextItemWidth(-1);
            ImGui::DragFloat("##col_chh", &col.capsuleHalfHeight, 0.01f, 0.001f, 999.0f, "%.3f");
            ImGui::NextColumn();
        }

        ImGui::Text("Center"); ImGui::NextColumn();
        ImGui::SetNextItemWidth(-1);
        ImGui::DragFloat3("##col_ctr", &col.center.x, 0.01f, -999.0f, 999.0f, "%.3f");
        ImGui::NextColumn();

        ImGui::Text("Static Fric."); ImGui::NextColumn();
        ImGui::SetNextItemWidth(-1);
        ImGui::DragFloat("##col_sf", &col.staticFriction,  0.01f, 0.0f, 1.0f, "%.2f");
        ImGui::NextColumn();

        ImGui::Text("Dynamic Fric."); ImGui::NextColumn();
        ImGui::SetNextItemWidth(-1);
        ImGui::DragFloat("##col_df", &col.dynamicFriction, 0.01f, 0.0f, 1.0f, "%.2f");
        ImGui::NextColumn();

        ImGui::Text("Restitution"); ImGui::NextColumn();
        ImGui::SetNextItemWidth(-1);
        ImGui::DragFloat("##col_res", &col.restitution,    0.01f, 0.0f, 1.0f, "%.2f");
        ImGui::NextColumn();

        ImGui::Text("Is Trigger"); ImGui::NextColumn();
        ImGui::Checkbox("##col_trig", &col.isTrigger);
        ImGui::NextColumn();

        ImGui::Columns(1);
        ImGui::Spacing();

        // Apply changes to PhysX if any field was modified
        if (col.shape            != prevShape       ||
            col.boxHalfExtents   != prevHalfExt     ||
            col.radius           != prevRadius      ||
            col.capsuleHalfHeight!= prevCapsuleHH   ||
            col.center           != prevCenter      ||
            col.staticFriction   != prevStaticFric  ||
            col.dynamicFriction  != prevDynFric     ||
            col.restitution      != prevRest        ||
            col.isTrigger        != prevTrigger)
        {
            PhysicsSystem::Get().MarkDirty(selected);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────

void InspectorPanel::DrawCharacterControllerComponent()
{
    if (!registry->Has<CharacterControllerComponent>(selected)) return;

    if (ComponentHeader<CharacterControllerComponent>("Character Controller", registry, selected,
            ImVec4(1.0f, 0.55f, 0.1f, 1.0f)))
    {
        auto& cc = registry->Get<CharacterControllerComponent>(selected);

        const float prevRadius     = cc.radius;
        const float prevHeight     = cc.height;
        const float prevStepOffset = cc.stepOffset;
        const float prevSlopeLimit = cc.slopeLimit;
        const float prevSkinWidth  = cc.skinWidth;
        const glm::vec3 prevCenter = cc.center;

        ImGui::Columns(2, "cc_cols", false);
        ImGui::SetColumnWidth(0, 130.0f);

        ImGui::Text("Radius"); ImGui::NextColumn();
        ImGui::SetNextItemWidth(-1);
        ImGui::DragFloat("##cc_rad", &cc.radius, 0.01f, 0.01f, 10.0f, "%.3f");
        ImGui::NextColumn();

        ImGui::Text("Height"); ImGui::NextColumn();
        ImGui::SetNextItemWidth(-1);
        ImGui::DragFloat("##cc_hgt", &cc.height, 0.01f, 0.01f, 10.0f, "%.3f");
        ImGui::NextColumn();

        ImGui::Text("Step Offset"); ImGui::NextColumn();
        ImGui::SetNextItemWidth(-1);
        ImGui::DragFloat("##cc_step", &cc.stepOffset, 0.01f, 0.0f, 3.0f, "%.3f");
        ImGui::NextColumn();

        ImGui::Text("Slope Limit"); ImGui::NextColumn();
        ImGui::SetNextItemWidth(-1);
        ImGui::DragFloat("##cc_slope", &cc.slopeLimit, 0.5f, 0.0f, 89.9f, "%.1f°");
        ImGui::NextColumn();

        ImGui::Text("Skin Width"); ImGui::NextColumn();
        ImGui::SetNextItemWidth(-1);
        ImGui::DragFloat("##cc_skin", &cc.skinWidth, 0.001f, 0.001f, 1.0f, "%.3f");
        ImGui::NextColumn();

        ImGui::Text("Center"); ImGui::NextColumn();
        ImGui::SetNextItemWidth(-1);
        ImGui::DragFloat3("##cc_ctr", &cc.center.x, 0.01f, -10.0f, 10.0f, "%.3f");
        ImGui::NextColumn();

        // Runtime info (read-only)
        ImGui::Separator();
        ImGui::Text("Grounded"); ImGui::NextColumn();
        ImGui::TextDisabled("%s", cc.isGrounded ? "yes" : "no");
        ImGui::NextColumn();

        ImGui::Columns(1);
        ImGui::Spacing();

        if (cc.radius     != prevRadius     ||
            cc.height     != prevHeight     ||
            cc.stepOffset != prevStepOffset ||
            cc.slopeLimit != prevSlopeLimit ||
            cc.skinWidth  != prevSkinWidth  ||
            cc.center     != prevCenter)
        {
            PhysicsSystem::Get().MarkDirty(selected);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────

void InspectorPanel::DrawScriptComponent()
{
    if (!registry->Has<ScriptComponent>(selected)) return;

    if (ComponentHeader<ScriptComponent>("Script", registry, selected, ImVec4(0.9f, 0.75f, 0.1f, 1.0f)))
    {
        auto& sc = registry->Get<ScriptComponent>(selected);

        // Script asset slot
        {
            std::string newPath; uint64_t newGuid = 0;
            if (AssetDropField("Script", Krayon::AssetType::Script, sc.scriptPath, newPath, newGuid))
            {
                sc.scriptGuid = newGuid;
                sc.scriptPath = newPath;
                sc.started    = false; // trigger reload
                LuaScriptSystem::Get().Reload(selected);
            }
        }

        ImGui::Spacing();

        // Reload button
        if (ImGui::Button("Reload Script"))
        {
            sc.started = false;
            LuaScriptSystem::Get().Reload(selected);
        }

        // Show path
        if (!sc.scriptPath.empty())
        {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", sc.scriptPath.c_str());
        }

        ImGui::Spacing();
    }
}

// ─────────────────────────────────────────────────────────────────────────────

void InspectorPanel::DrawAddComponent()
{
    const bool canAddTransform    = !registry->Has<TransformComponent>(selected);
    const bool canAddMeshRenderer = !registry->Has<MeshRendererComponent>(selected);
    const bool canAddLight        = !registry->Has<LightComponent>(selected);
    const bool canAddFog          = !registry->Has<VolumetricFogComponent>(selected);
    const bool canAddAnim         = !registry->Has<AnimationComponent>(selected);
    const bool canAddRigidbody    = !registry->Has<RigidbodyComponent>(selected);
    const bool canAddCollider     = !registry->Has<ColliderComponent>(selected);
    const bool canAddCC           = !registry->Has<CharacterControllerComponent>(selected);
    const bool canAddScript       = !registry->Has<ScriptComponent>(selected);
    const bool anyAvailable       = canAddTransform || canAddMeshRenderer || canAddLight
                                 || canAddFog || canAddAnim || canAddRigidbody || canAddCollider
                                 || canAddCC || canAddScript;

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
        if (canAddTransform    && ImGui::MenuItem("   Transform"))      registry->Add<TransformComponent>(selected);
        if (canAddMeshRenderer && ImGui::MenuItem("   Mesh Renderer"))  registry->Add<MeshRendererComponent>(selected);
        if (canAddLight        && ImGui::MenuItem("   Light"))          registry->Add<LightComponent>(selected);
        if (canAddFog          && ImGui::MenuItem("   Volumetric Fog")) registry->Add<VolumetricFogComponent>(selected);
        if (canAddAnim         && ImGui::MenuItem("   Animation"))      registry->Add<AnimationComponent>(selected);
        if (canAddRigidbody && ImGui::MenuItem("   Rigidbody"))
        {
            registry->Add<RigidbodyComponent>(selected);
            PhysicsSystem::Get().MarkDirty(selected); // rebuild static->dynamic if collider exists
        }
        if (canAddCollider  && ImGui::MenuItem("   Collider"))
        {
            registry->Add<ColliderComponent>(selected);
            PhysicsSystem::Get().MarkDirty(selected); // rebuild shapeless->shaped if rigidbody exists
        }
        if (canAddCC && ImGui::MenuItem("   Character Controller"))
        {
            registry->Add<CharacterControllerComponent>(selected);
            PhysicsSystem::Get().MarkDirty(selected);
        }
        if (canAddScript && ImGui::MenuItem("   Script"))
            registry->Add<ScriptComponent>(selected);
        ImGui::EndPopup();
    }
}

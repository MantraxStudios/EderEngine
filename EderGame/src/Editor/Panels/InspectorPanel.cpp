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
#include "ECS/Components/AudioSourceComponent.h"
#include "ECS/Components/PlayerStartComponent.h"
#include "ECS/Components/PlayerComponent.h"
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
#include <string>
#include <algorithm>
#include <cctype>

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

    // Unity-style header: editable name + entity id (replaces the old Tag section)
    {
        char nameBuf[128] = "Entity";
        if (registry->Has<TagComponent>(selected))
        {
            strncpy(nameBuf, registry->Get<TagComponent>(selected).name.c_str(), sizeof(nameBuf) - 1);
            nameBuf[sizeof(nameBuf) - 1] = '\0';
        }
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 60.0f);
        if (ImGui::InputText("##entname", nameBuf, sizeof(nameBuf)) &&
            registry->Has<TagComponent>(selected))
            registry->Get<TagComponent>(selected).name = nameBuf;
        ImGui::SameLine();
        ImGui::TextDisabled("#%u", selected);
    }
    ImGui::Separator();
    ImGui::Spacing();

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
    DrawAudioSourceComponent();
    DrawPlayerStartComponent();

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

    if (!hasParent) return;  

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
            {
                glm::vec3 prevRot = t.rotation;
                Vec3Row("Rotation", &t.rotation.x, 0.5f, -FLT_MAX, FLT_MAX, 0.0f);
                if (t.rotation != prevRot) t.usePhysicsQuat = false;
            }
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
        // ── Per-submesh material slots ────────────────────────────
        {
            uint32_t smCount = 0;
            if (m_getMeshSubmeshCount && m.meshGuid != 0)
                smCount = m_getMeshSubmeshCount(m.meshGuid);

            if (smCount > 1)
            {
                if (m.subMeshMaterialGuids.size() != smCount)
                {
                    m.subMeshMaterialGuids.resize(smCount, 0);
                    m.subMeshMaterialNames.resize(smCount);
                }

                ImGui::Spacing();
                ImGui::TextDisabled("-- Sub-mesh Materials --");
                ImGui::SameLine();
                if (ImGui::SmallButton("Auto-detect") && m_getMeshSubmeshName)
                {
                    auto& am = Krayon::AssetManager::Get();
                    auto toLower = [](std::string s) {
                        for (auto& c : s) c = (char)std::tolower((unsigned char)c);
                        return s;
                    };

                    const auto& allAssets = am.GetAll();
                    for (uint32_t si = 0; si < smCount; si++)
                    {
                        std::string smName = m_getMeshSubmeshName(m.meshGuid, si);
                        if (smName.empty()) continue;
                        std::string smLow = toLower(smName);

                        // 1st pass: exact name match on existing .mat
                        uint64_t matGuid = 0;
                        for (auto& [guid, asset] : allAssets)
                        {
                            if (asset.type != Krayon::AssetType::Material) continue;
                            if (toLower(asset.name) == smLow) { matGuid = guid; break; }
                        }
                        // 2nd pass: contains match on .mat
                        if (!matGuid)
                        {
                            for (auto& [guid, asset] : allAssets)
                            {
                                if (asset.type != Krayon::AssetType::Material) continue;
                                std::string an = toLower(asset.name);
                                if (an.find(smLow) != std::string::npos ||
                                    smLow.find(an)  != std::string::npos)
                                { matGuid = guid; break; }
                            }
                        }
                        // 3rd pass: find a matching texture and CREATE a .mat from it
                        if (!matGuid)
                        {
                            uint64_t texGuid = 0;
                            for (auto& [guid, asset] : allAssets)
                            {
                                if (asset.type != Krayon::AssetType::Texture) continue;
                                std::string an = toLower(asset.name);
                                if (an == smLow ||
                                    an.find(smLow) != std::string::npos ||
                                    smLow.find(an)  != std::string::npos)
                                { texGuid = guid; break; }
                            }
                            if (texGuid)
                            {
                                // Create a real .mat file so it can be edited in the Material Editor
                                uint64_t newGuid = am.CreateMaterialAsset("assets/materials", smName);
                                if (newGuid)
                                {
                                    Krayon::MaterialAsset ma;
                                    if (am.ReadMaterialAsset(newGuid, ma))
                                    {
                                        ma.albedoTexGuid = texGuid;
                                        am.SaveMaterialAsset(newGuid, ma);
                                    }
                                    matGuid = newGuid;
                                }
                            }
                        }

                        if (matGuid)
                        {
                            m.subMeshMaterialGuids[si] = matGuid;
                            const auto* mm = am.FindByGuid(matGuid);
                            m.subMeshMaterialNames[si] = mm ? mm->name : "";
                        }
                    }
                }
                for (uint32_t si = 0; si < smCount; si++)
                {
                    ImGui::PushID((int)si);
                    // Build label: "Slot N (AssipmName)"
                    std::string smHint = m_getMeshSubmeshName
                        ? m_getMeshSubmeshName(m.meshGuid, si) : "";
                    char label[128];
                    if (!smHint.empty())
                        snprintf(label, sizeof(label), "Slot %u (%s)", si, smHint.c_str());
                    else
                        snprintf(label, sizeof(label), "Slot %u", si);
                    std::string displayPath = m.subMeshMaterialNames[si];
                    if (m.subMeshMaterialGuids[si] != 0)
                    {
                        const auto* mm = Krayon::AssetManager::Get().FindByGuid(m.subMeshMaterialGuids[si]);
                        if (mm) displayPath = mm->path;
                    }
                    std::string newPath; uint64_t newGuid = 0;
                    if (AssetDropField(label, Krayon::AssetType::Material, displayPath, newPath, newGuid))
                    {
                        m.subMeshMaterialGuids[si] = newGuid;
                        const auto* mm2 = Krayon::AssetManager::Get().FindByGuid(newGuid);
                        m.subMeshMaterialNames[si] = mm2 ? mm2->name : newPath;
                    }
                    if (m_openMaterial && m.subMeshMaterialGuids[si] != 0)
                    {
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Edit"))
                            m_openMaterial(m.subMeshMaterialGuids[si]);
                    }
                    ImGui::PopID();
                }

                // ── Per-submesh entities / local transforms ───────────────
                ImGui::Spacing();
                ImGui::TextDisabled("-- Sub-mesh Entities --");
                if (m.subMeshTransforms.size() < smCount)
                    m.subMeshTransforms.resize(smCount);
                if (m.subMeshEntityIds.size() < smCount)
                    m.subMeshEntityIds.resize(smCount, 0);

                for (uint32_t si = 0; si < smCount; ++si)
                {
                    ImGui::PushID((int)si + 10000);
                    std::string smHint2 = m_getMeshSubmeshName
                        ? m_getMeshSubmeshName(m.meshGuid, si) : "";
                    char smLabel[128];
                    if (!smHint2.empty())
                        snprintf(smLabel, sizeof(smLabel), "Slot %u (%s)", si, smHint2.c_str());
                    else
                        snprintf(smLabel, sizeof(smLabel), "Slot %u", si);

                    if (ImGui::TreeNodeEx(smLabel, 0))
                    {
                        uint32_t linkedId = m.subMeshEntityIds[si];
                        bool hasEntity = linkedId != 0
                            && registry && registry->Has<TransformComponent>(linkedId);

                        if (hasEntity)
                        {
                            // Show linked entity name.
                            const char* entName = "Entity";
                            if (registry->Has<TagComponent>(linkedId))
                                entName = registry->Get<TagComponent>(linkedId).name.c_str();
                            ImGui::TextColored({0.4f,0.9f,0.4f,1.f}, "Entity: %s [%u]", entName, linkedId);
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Unlink"))
                                m.subMeshEntityIds[si] = 0;
                        }
                        else
                        {
                            // No entity — show manual transform + create button.
                            auto& st = m.subMeshTransforms[si];
                            ImGui::DragFloat3("Position", &st.position.x,    0.01f);
                            ImGui::DragFloat3("Rotation", &st.rotEulerDeg.x, 0.5f);
                            ImGui::DragFloat3("Scale",    &st.scale.x,       0.01f, 0.001f, 1000.0f);
                            if (ImGui::SmallButton("Reset")) st = SubMeshTransform{};
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Create Entity") && registry)
                            {
                                // Create a child entity that drives this sub-mesh.
                                Entity newE = registry->Create();

                                auto& tag = registry->Add<TagComponent>(newE);
                                char tagName[64];
                                if (!smHint2.empty())
                                    snprintf(tagName, sizeof(tagName), "%s", smHint2.c_str());
                                else
                                    snprintf(tagName, sizeof(tagName), "SubMesh %u", si);
                                tag.name = tagName;

                                auto& tc = registry->Add<TransformComponent>(newE);
                                tc.position = st.position;
                                tc.rotation = st.rotEulerDeg;  // same YXZ euler convention
                                tc.scale    = st.scale;

                                // Wire HierarchyComponent to parent.
                                auto& hc = registry->Add<HierarchyComponent>(newE);
                                hc.parent = selected;
                                if (registry->Has<HierarchyComponent>(selected))
                                    registry->Get<HierarchyComponent>(selected).children.push_back(newE);
                                else
                                    registry->Add<HierarchyComponent>(selected).children.push_back(newE);

                                m.subMeshEntityIds[si] = newE;
                            }
                        }
                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                }
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

// ── Unity-style two-column property rows (label left, widget right) ─────────
static bool PropsBegin(const char* id)
{
    if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_None)) return false;
    ImGui::TableSetupColumn("l", ImGuiTableColumnFlags_WidthFixed, 120.0f);
    ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch);
    return true;
}
static void PropRow(const char* label)
{
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-1);
}
static void PropsEnd() { ImGui::EndTable(); }

void InspectorPanel::DrawLightComponent()
{
    if (!registry->Has<LightComponent>(selected)) return;
    ImGui::PushID("Light");
    if (ComponentHeader<LightComponent>(
            "Light", registry, selected, ImVec4(1.0f, 0.85f, 0.20f, 1.0f)))
    {
        auto& l = registry->Get<LightComponent>(selected);

        if (PropsBegin("##light"))
        {
            PropRow("Type");
            const char* types[] = { "Directional", "Point", "Spot" };
            int typeIdx = static_cast<int>(l.type);
            if (ImGui::Combo("##type", &typeIdx, types, IM_ARRAYSIZE(types)))
                l.type = static_cast<LightType>(typeIdx);

            PropRow("Color");     ImGui::ColorEdit3("##col", &l.color.x);
            PropRow("Intensity"); ImGui::DragFloat("##int", &l.intensity, 0.05f, 0.0f, 100.0f);

            PropRow("Use Temperature"); ImGui::Checkbox("##useTemp", &l.useTemperature);
            if (l.useTemperature)
            {
                PropRow("Temperature");
                ImGui::SliderFloat("##temp", &l.colorTemperature, 1000.0f, 15000.0f, "%.0f K");
            }

            if (l.type == LightType::Directional)
            {
                PropRow("Ambient");
                ImGui::DragFloat("##amb", &l.ambientIntensity, 0.02f, 0.0f, 4.0f);
            }
            else
            {
                PropRow("Range");
                ImGui::DragFloat("##range", &l.range, 0.5f, 0.0f, 1000.0f);
            }
            if (l.type == LightType::Spot)
            {
                PropRow("Inner Angle"); ImGui::DragFloat("##inner", &l.innerConeAngle, 0.5f, 0.0f, 89.0f);
                PropRow("Outer Angle"); ImGui::DragFloat("##outer", &l.outerConeAngle, 0.5f, 1.0f, 90.0f);
            }

            PropRow("Cast Shadow"); ImGui::Checkbox("##shadow", &l.castShadow);
            if (l.castShadow && l.type == LightType::Directional)
            {
                PropRow("Shadow Distance");
                ImGui::DragFloat("##shdist", &l.shadowDistance, 1.0f, 10.0f, 2000.0f);
            }
            PropsEnd();
        }

        // Advanced blocks collapsed by default so the inspector stays scannable.
        ImGui::Spacing();
        if (ImGui::TreeNode("Volumetric Light"))
        {
            if (PropsBegin("##vol"))
            {
                PropRow("Enabled"); ImGui::Checkbox("##ven", &l.volumetricEnabled);
                if (l.volumetricEnabled)
                {
                    PropRow("Steps");        ImGui::DragInt  ("##vsteps", &l.volNumSteps,    1,     8,    256);
                    PropRow("Max Distance"); ImGui::DragFloat("##vmax",   &l.volMaxDistance, 1.0f,  1.0f, 500.0f);
                    PropRow("Jitter");       ImGui::DragFloat("##vjit",   &l.volJitter,      0.05f, 0.0f, 2.0f);
                    PropRow("Density");      ImGui::DragFloat("##vden",   &l.volDensity,     0.001f, 0.0f, 1.0f, "%.4f");
                    PropRow("Absorption");   ImGui::DragFloat("##vabs",   &l.volAbsorption,  0.001f, 0.0f, 1.0f, "%.4f");
                    PropRow("Anisotropy");   ImGui::DragFloat("##vg",     &l.volG,           0.01f, -0.99f, 0.99f);
                    PropRow("Intensity");    ImGui::DragFloat("##vint",   &l.volIntensity,   0.01f, 0.0f, 5.0f);
                    PropRow("Tint");         ImGui::ColorEdit3("##vtint", &l.volTint.x);
                }
                PropsEnd();
            }
            ImGui::TreePop();
        }

        if (l.type == LightType::Directional && ImGui::TreeNode("Sun Shafts"))
        {
            if (PropsBegin("##shafts"))
            {
                PropRow("Enabled"); ImGui::Checkbox("##sen", &l.sunShaftsEnabled);
                if (l.sunShaftsEnabled)
                {
                    PropRow("Density");     ImGui::DragFloat("##sden",  &l.shaftsDensity,    0.05f,  0.0f, 20.0f);
                    PropRow("Weight");      ImGui::DragFloat("##swgt",  &l.shaftsWeight,     0.01f,  0.0f,  3.0f);
                    PropRow("Decay");       ImGui::DragFloat("##sdec",  &l.shaftsDecay,      0.005f, 0.5f,  0.999f);
                    PropRow("Sun Radius");  ImGui::DragFloat("##srad",  &l.shaftsSunRadius,  0.002f, 0.005f, 0.2f);
                    PropRow("Bloom Scale"); ImGui::DragFloat("##sblm",  &l.shaftsBloomScale, 0.05f,  0.0f, 10.0f);
                    PropRow("Exposure");    ImGui::DragFloat("##sexp",  &l.shaftsExposure,   0.005f, 0.0f,  1.0f);
                    PropRow("Tint");        ImGui::ColorEdit3("##stint", &l.shaftsTint.x);
                }
                PropsEnd();
            }
            ImGui::TreePop();
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
        const bool  prevFreezeX     = rb.freezeRotationX;
        const bool  prevFreezeY     = rb.freezeRotationY;
        const bool  prevFreezeZ     = rb.freezeRotationZ;

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

        ImGui::Text("Freeze Rotation"); ImGui::NextColumn();
        ImGui::PushID("rb_freeze");
        ImGui::Checkbox("X", &rb.freezeRotationX); ImGui::SameLine();
        ImGui::Checkbox("Y", &rb.freezeRotationY); ImGui::SameLine();
        ImGui::Checkbox("Z", &rb.freezeRotationZ);
        ImGui::PopID();
        ImGui::NextColumn();

        ImGui::Columns(1);

        // Read-only velocity
        ImGui::Spacing();
        ImGui::TextDisabled("Linear Vel:  (%.2f, %.2f, %.2f)",
            rb.linearVelocity.x, rb.linearVelocity.y, rb.linearVelocity.z);
        ImGui::TextDisabled("Angular Vel: (%.2f, %.2f, %.2f)",
            rb.angularVelocity.x, rb.angularVelocity.y, rb.angularVelocity.z);

        ImGui::Spacing();

        // Apply changes to physics if any field was modified
        if (rb.mass        != prevMass        ||
            rb.linearDrag  != prevLinearDrag  ||
            rb.angularDrag != prevAngularDrag ||
            rb.useGravity  != prevUseGravity  ||
            rb.isKinematic != prevKinematic   ||
            rb.freezeRotationX != prevFreezeX ||
            rb.freezeRotationY != prevFreezeY ||
            rb.freezeRotationZ != prevFreezeZ)
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
        else if (col.shape == ColliderShape::Capsule)
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

        // Apply changes to physics if any field was modified
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

        // Open in VS Code
        if (!sc.scriptPath.empty())
        {
            ImGui::SameLine();
            if (ImGui::Button("Edit Script"))
            {
                namespace fs = std::filesystem;
                const std::string wd = Krayon::AssetManager::Get().GetWorkDir();
                std::string abs = (fs::path(wd) / sc.scriptPath).string();
                std::string cmd = "code \"" + abs + "\"";
                system(cmd.c_str());
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%s", sc.scriptPath.c_str());
        }

        ImGui::Spacing();
    }
}

// ─────────────────────────────────────────────────────────────────────────────

void InspectorPanel::DrawAudioSourceComponent()
{
    if (!registry->Has<AudioSourceComponent>(selected)) return;

    if (ComponentHeader<AudioSourceComponent>("Audio Source", registry, selected,
            ImVec4(0.20f, 0.85f, 0.90f, 1.0f)))
    {
        auto& au = registry->Get<AudioSourceComponent>(selected);

        // ── Audio clip drop slot ──────────────────────────────────
        {
            std::string newPath; uint64_t newGuid = 0;
            if (AssetDropField("Clip", Krayon::AssetType::Audio, au.audioPath, newPath, newGuid))
            {
                au.audioGuid = newGuid;
                au.audioPath = newPath;
                au.started   = false; // reload on next Update
            }
        }

        ImGui::Spacing();
        ImGui::DragFloat("Volume",       &au.volume,      0.01f, 0.0f, 1.0f, "%.2f");

        ImGui::Spacing();
        ImGui::Checkbox("Loop",          &au.loop);
        ImGui::SameLine(120);
        ImGui::Checkbox("Play On Awake", &au.playOnAwake);
        ImGui::Checkbox("Spatial (3D)",  &au.spatial);
        ImGui::SameLine(120);
        ImGui::Checkbox("Muted",         &au.muted);

        if (au.spatial)
        {
            ImGui::Spacing();
            ImGui::TextDisabled("-- 3D Attenuation --");
            ImGui::DragFloat("Min Distance", &au.minDistance, 0.1f, 0.01f, 500.0f, "%.2f");
            ImGui::DragFloat("Max Distance", &au.maxDistance, 1.0f, 1.0f,  2000.0f, "%.2f");
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
    const bool canAddAudio        = !registry->Has<AudioSourceComponent>(selected);
    const bool anyAvailable       = canAddTransform || canAddMeshRenderer || canAddLight
                                 || canAddFog || canAddAnim || canAddRigidbody || canAddCollider
                                 || canAddCC || canAddScript || canAddAudio;

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
        // Search filter — focus it the first frame the popup opens so you can
        // just start typing to narrow the list.
        static char filter[64] = {};
        if (ImGui::IsWindowAppearing())
        {
            filter[0] = '\0';
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##compfilter", "Search...", filter, sizeof(filter));
        ImGui::Separator();

        // Case-insensitive substring match against the filter text.
        auto match = [](const char* name, const char* f) -> bool
        {
            if (!f[0]) return true;
            std::string a = name, b = f;
            std::transform(a.begin(), a.end(), a.begin(), ::tolower);
            std::transform(b.begin(), b.end(), b.begin(), ::tolower);
            return a.find(b) != std::string::npos;
        };
        auto item = [&](bool can, const char* label) -> bool
        {
            return can && match(label, filter) && ImGui::MenuItem(label);
        };

        if (item(canAddTransform,    "Transform"))      registry->Add<TransformComponent>(selected);
        if (item(canAddMeshRenderer, "Mesh Renderer"))  registry->Add<MeshRendererComponent>(selected);
        if (item(canAddLight,        "Light"))          registry->Add<LightComponent>(selected);
        if (item(canAddFog,          "Volumetric Fog")) registry->Add<VolumetricFogComponent>(selected);
        if (item(canAddAnim,         "Animation"))      registry->Add<AnimationComponent>(selected);
        if (item(canAddRigidbody,    "Rigidbody"))
        {
            registry->Add<RigidbodyComponent>(selected);
            PhysicsSystem::Get().MarkDirty(selected); // rebuild static->dynamic if collider exists
        }
        if (item(canAddCollider,     "Collider"))
        {
            registry->Add<ColliderComponent>(selected);
            PhysicsSystem::Get().MarkDirty(selected); // rebuild shapeless->shaped if rigidbody exists
        }
        if (item(canAddCC,           "Character Controller"))
        {
            registry->Add<CharacterControllerComponent>(selected);
            PhysicsSystem::Get().MarkDirty(selected);
        }
        if (item(canAddScript,       "Script"))
            registry->Add<ScriptComponent>(selected);
        if (item(canAddAudio,        "Audio Source"))
            registry->Add<AudioSourceComponent>(selected);
        if (item(!registry->Has<PlayerStartComponent>(selected), "Player Start"))
            registry->Add<PlayerStartComponent>(selected);
        ImGui::EndPopup();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  PlayerStartComponent
// ─────────────────────────────────────────────────────────────────────────────
void InspectorPanel::DrawPlayerStartComponent()
{
    if (!registry->Has<PlayerStartComponent>(selected)) return;
    if (!ComponentHeader<PlayerStartComponent>("Player Start", registry, selected,
            ImVec4(0.2f, 0.8f, 0.3f, 1.0f)))
        return;

    auto& ps = registry->Get<PlayerStartComponent>(selected);

    ImGui::BeginTable("##pst", 3, ImGuiTableFlags_SizingFixedFit);
    ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, 100.0f);
    ImGui::TableSetupColumn("val",   ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("btn",   ImGuiTableColumnFlags_WidthFixed, 26.0f);

    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Prefab");
    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-1);

    // Show the current path in a read-only input
    char buf[512];
    std::strncpy(buf, ps.prefabPath.c_str(), sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    ImGui::InputText("##psp", buf, sizeof(buf), ImGuiInputTextFlags_ReadOnly);

    // Drag-drop target for .prefab assets
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("ASSET_GUID"))
        {
            uint64_t guid = *(const uint64_t*)pl->Data;
            const auto* metaPtr = Krayon::AssetManager::Get().FindByGuid(guid);
            if (metaPtr && (metaPtr->type == Krayon::AssetType::Prefab || metaPtr->type == Krayon::AssetType::Scene))
                ps.prefabPath = metaPtr->path;
        }
        ImGui::EndDragDropTarget();
    }

    ImGui::TableSetColumnIndex(2);
    if (ImGui::Button("X##clrps", ImVec2(22, 0))) ps.prefabPath.clear();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Clear");

    ImGui::EndTable();

    if (registry->Has<PlayerComponent>(selected))
    {
        ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.3f, 1.0f), "[Player]  This entity is the spawned player.");
    }
}


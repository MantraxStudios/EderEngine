#include "HierarchyPanel.h"
#include "ECS/Components/TagComponent.h"
#include "ECS/Components/TransformComponent.h"
#include "ECS/Components/HierarchyComponent.h"
#include "ECS/Components/LightComponent.h"
#include "ECS/Components/MeshRendererComponent.h"
#include "ECS/Components/AnimationComponent.h"
#include "ECS/Components/CameraComponent.h"
#include "ECS/Components/ScriptComponent.h"
#include "ECS/Components/RigidbodyComponent.h"
#include "ECS/Components/ColliderComponent.h"
#include "ECS/Components/CharacterControllerComponent.h"
#include "ECS/Components/AudioSourceComponent.h"
#include "ECS/Components/VolumetricFogComponent.h"
#include "ECS/Components/LayerComponent.h"
#include "ECS/Systems/TransformSystem.h"
#include <imgui/imgui.h>
#include <cstdio>
#include <cstring>

void HierarchyPanel::DuplicateSelected()
{
    if (!registry || selected == NULL_ENTITY) return;
    Entity e    = selected;
    Entity copy = registry->Create();
    if (registry->Has<TagComponent>(e))
    {
        auto tag  = registry->Get<TagComponent>(e);
        tag.name += " (copy)";
        registry->Add<TagComponent>(copy) = tag;
    }
    if (registry->Has<TransformComponent>(e))
        registry->Add<TransformComponent>(copy) = registry->Get<TransformComponent>(e);
    if (registry->Has<MeshRendererComponent>(e))
        registry->Add<MeshRendererComponent>(copy) = registry->Get<MeshRendererComponent>(e);
    if (registry->Has<LightComponent>(e))
        registry->Add<LightComponent>(copy) = registry->Get<LightComponent>(e);
    if (registry->Has<AnimationComponent>(e))
        registry->Add<AnimationComponent>(copy) = registry->Get<AnimationComponent>(e);
    if (registry->Has<CameraComponent>(e))
        registry->Add<CameraComponent>(copy) = registry->Get<CameraComponent>(e);
    if (registry->Has<ScriptComponent>(e))
        registry->Add<ScriptComponent>(copy) = registry->Get<ScriptComponent>(e);
    if (registry->Has<RigidbodyComponent>(e))
        registry->Add<RigidbodyComponent>(copy) = registry->Get<RigidbodyComponent>(e);
    if (registry->Has<ColliderComponent>(e))
        registry->Add<ColliderComponent>(copy) = registry->Get<ColliderComponent>(e);
    if (registry->Has<CharacterControllerComponent>(e))
        registry->Add<CharacterControllerComponent>(copy) = registry->Get<CharacterControllerComponent>(e);
    if (registry->Has<AudioSourceComponent>(e))
        registry->Add<AudioSourceComponent>(copy) = registry->Get<AudioSourceComponent>(e);
    if (registry->Has<VolumetricFogComponent>(e))
        registry->Add<VolumetricFogComponent>(copy) = registry->Get<VolumetricFogComponent>(e);
    if (registry->Has<LayerComponent>(e))
        registry->Add<LayerComponent>(copy) = registry->Get<LayerComponent>(e);
    selected = copy;
}

void HierarchyPanel::DrawEntityNode(Entity e)
{
    if (!registry) return;

    const char* name = registry->Has<TagComponent>(e)
        ? registry->Get<TagComponent>(e).name.c_str()
        : "Entity";

    const char* icon;
    if      (registry->Has<LightComponent>      (e)) icon = "[L] ";
    else if (registry->Has<MeshRendererComponent>(e)) icon = "[M] ";
    else                                               icon = "[A] ";

    char label[256];
    snprintf(label, sizeof(label), "%s%s", icon, name);

    bool hasChildren = registry->Has<HierarchyComponent>(e)
                    && !registry->Get<HierarchyComponent>(e).children.empty();

    ImGuiTreeNodeFlags flags =
        ImGuiTreeNodeFlags_OpenOnArrow   |
        ImGuiTreeNodeFlags_SpanFullWidth |
        ImGuiTreeNodeFlags_FramePadding;
    if (!hasChildren) flags |= ImGuiTreeNodeFlags_Leaf;
    if (selected == e) flags |= ImGuiTreeNodeFlags_Selected;

    bool open = ImGui::TreeNodeEx((void*)(intptr_t)e, flags, "%s", label);

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
        selected = (selected == e) ? NULL_ENTITY : e;

    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
    {
        ImGui::SetDragDropPayload("ENTITY_REPARENT", &e, sizeof(Entity));
        ImGui::Text("Move: %s%s", icon, name);
        ImGui::EndDragDropSource();
    }

    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* payload =
                ImGui::AcceptDragDropPayload("ENTITY_REPARENT"))
        {
            Entity dragged = *(const Entity*)payload->Data;
            if (dragged != e && !TransformSystem::IsDescendant(e, dragged, *registry))
                pendingAttach = { dragged, e };
        }
        ImGui::EndDragDropTarget();
    }

    if (ImGui::BeginPopupContextItem())
    {
        ImGui::TextDisabled("%s%s", icon, name);
        ImGui::Separator();

        if (ImGui::MenuItem("Duplicate"))
        {
            selected = e;
            DuplicateSelected();
        }

        bool hasParent = registry->Has<HierarchyComponent>(e)
                      && registry->Get<HierarchyComponent>(e).parent != NULL_ENTITY;
        if (hasParent)
        {
            ImGui::Separator();
            if (ImGui::MenuItem("Detach from Parent"))
                pendingDetach = e;
        }

        ImGui::Separator();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
        if (ImGui::MenuItem("Delete"))
            pendingDestroy = e;
        ImGui::PopStyleColor();

        ImGui::EndPopup();
    }

    if (open)
    {
        if (hasChildren)
        {
            auto children = registry->Get<HierarchyComponent>(e).children;
            for (Entity c : children)
                DrawEntityNode(c);
        }
        ImGui::TreePop();
    }
}

void HierarchyPanel::OnDraw()
{
    pendingDestroy = NULL_ENTITY;
    pendingDetach  = NULL_ENTITY;
    pendingAttach  = {};

    if (!ImGui::Begin(Title(), &open)) { ImGui::End(); return; }

    if (!registry)
    {
        ImGui::TextDisabled("No registry");
        ImGui::End();
        return;
    }

    const std::vector<Entity> entities = registry->GetEntities();
    int total = (int)entities.size();

    float addBtnW = 26.0f;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - addBtnW - ImGui::GetStyle().ItemSpacing.x);
    ImGui::InputTextWithHint("##search", "Search...", searchBuf, sizeof(searchBuf));
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.80f, 0.10f, 0.10f, 1.0f));
    if (ImGui::Button("+", ImVec2(addBtnW, 0)))
    {
        Entity e = registry->Create();
        registry->Add<TagComponent>(e);
        registry->Add<TransformComponent>(e);
        selected = e;
    }
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add Entity");

    ImGui::TextDisabled("%d actor%s", total, total == 1 ? "" : "s");
    ImGui::Separator();

    bool hasFilter = (searchBuf[0] != '\0');

    if (!hasFilter)
    {
        for (Entity e : entities)
        {
            if (registry->Has<HierarchyComponent>(e)
             && registry->Get<HierarchyComponent>(e).parent != NULL_ENTITY)
                continue;
            DrawEntityNode(e);
        }

        // Drop on empty space -> detach (make root)
        ImVec2 avail = ImGui::GetContentRegionAvail();
        if (avail.y > 4.0f)
        {
            ImGui::InvisibleButton("##panelDrop", ImVec2(-1.0f, avail.y));
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload =
                        ImGui::AcceptDragDropPayload("ENTITY_REPARENT"))
                {
                    pendingDetach = *(const Entity*)payload->Data;
                }
                ImGui::EndDragDropTarget();
            }
        }
    }
    else
    {
        // Filtered flat list
        for (Entity e : entities)
        {
            const char* name = registry->Has<TagComponent>(e)
                ? registry->Get<TagComponent>(e).name.c_str()
                : "Entity";
            if (!strstr(name, searchBuf)) continue;

            ImGui::PushID((int)e);
            bool sel = (selected == e);
            if (sel) ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.80f, 0.10f, 0.10f, 0.40f));
            if (ImGui::Selectable(name, sel, ImGuiSelectableFlags_SpanAllColumns))
                selected = sel ? NULL_ENTITY : e;
            if (sel) ImGui::PopStyleColor();
            ImGui::PopID();
        }
    }

    if (total == 0)
    {
        float avail = ImGui::GetContentRegionAvail().x;
        const char* hint = "Click + to add an actor";
        ImGui::SetCursorPosX((avail - ImGui::CalcTextSize(hint).x) * 0.5f);
        ImGui::TextDisabled("%s", hint);
    }

    ImGui::End();

    if (pendingAttach.child != NULL_ENTITY && pendingAttach.parent != NULL_ENTITY)
        TransformSystem::Attach(pendingAttach.child, pendingAttach.parent, *registry);

    if (pendingDetach != NULL_ENTITY)
        TransformSystem::Detach(pendingDetach, *registry);

    if (pendingDestroy != NULL_ENTITY)
    {
        if (selected == pendingDestroy) selected = NULL_ENTITY;
        TransformSystem::PrepareDestroy(pendingDestroy, *registry);
        registry->Destroy(pendingDestroy);
    }
}
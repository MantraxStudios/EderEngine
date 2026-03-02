#include "HierarchyPanel.h"
#include "ECS/Components/TagComponent.h"
#include "ECS/Components/TransformComponent.h"
#include "ECS/Components/LightComponent.h"
#include "ECS/Components/MeshRendererComponent.h"
#include <imgui/imgui.h>
#include <cstdio>
#include <cstring>

void HierarchyPanel::OnDraw()
{
    pendingDestroy = NULL_ENTITY;
    if (!ImGui::Begin(Title(), &open)) { ImGui::End(); return; }

    if (!registry)
    {
        ImGui::TextDisabled("No registry");
        ImGui::End();
        return;
    }

    // Copy entity list so we can safely destroy inside the loop
    const std::vector<Entity> entities = registry->GetEntities();
    int   total   = (int)entities.size();

    float addBtnW = 26.0f;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - addBtnW - ImGui::GetStyle().ItemSpacing.x);
    ImGui::InputTextWithHint("##search", "Search...", searchBuf, sizeof(searchBuf));
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.80f, 0.10f, 0.10f, 1.0f));
    if (ImGui::Button("+", ImVec2(addBtnW, 0)))
    {
        Entity e = registry->Create();
        registry->Add<TagComponent>(e);
        selected = e;
    }
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add Entity");

    ImGui::TextDisabled("%d actor%s", total, total == 1 ? "" : "s");
    ImGui::Separator();

    bool hasFilter = (searchBuf[0] != '\0');

    for (Entity e : entities)
    {
        ImGui::PushID((int)e);

        const char* name = registry->Has<TagComponent>(e)
            ? registry->Get<TagComponent>(e).name.c_str()
            : "Entity";

        if (hasFilter && !strstr(name, searchBuf))
        {
            ImGui::PopID();
            continue;
        }

        const char* icon;
        ImVec4      iconColor;
        if (registry->Has<LightComponent>(e))
        {
            icon      = "[L]";
            iconColor = ImVec4(1.0f, 0.85f, 0.25f, 1.0f);
        }
        else if (registry->Has<MeshRendererComponent>(e))
        {
            icon      = "[M]";
            iconColor = ImVec4(0.45f, 0.75f, 1.0f, 1.0f);
        }
        else
        {
            icon      = "[A]";
            iconColor = ImVec4(0.65f, 0.65f, 0.65f, 1.0f);
        }

        ImGui::TextColored(iconColor, "%s", icon);
        ImGui::SameLine();

        bool sel = (selected == e);
        if (sel)
            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.80f, 0.10f, 0.10f, 0.40f));

        if (ImGui::Selectable(name, sel, ImGuiSelectableFlags_SpanAllColumns))
            selected = sel ? NULL_ENTITY : e;

        if (sel)
            ImGui::PopStyleColor();

        if (ImGui::BeginPopupContextItem())
        {
            ImGui::TextDisabled("%s", name);
            ImGui::Separator();
            if (ImGui::MenuItem("Duplicate"))
            {
                Entity copy = registry->Create();
                if (registry->Has<TagComponent>(e))
                {
                    auto tag  = registry->Get<TagComponent>(e);
                    tag.name += " (copy)";
                    registry->Add<TagComponent>(copy) = tag;
                }
                selected = copy;
            }
            if (ImGui::MenuItem("Focus")) {}
            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
            if (ImGui::MenuItem("Delete"))
            {
                pendingDestroy = e;
            }
            ImGui::PopStyleColor();
            ImGui::EndPopup();
        }

        ImGui::PopID();
    }

    if (pendingDestroy != NULL_ENTITY)
    {
        if (selected == pendingDestroy) selected = NULL_ENTITY;
        registry->Destroy(pendingDestroy);
        pendingDestroy = NULL_ENTITY;
    }

    if (total == 0)
    {
        ImGui::Spacing();
        float avail = ImGui::GetContentRegionAvail().x;
        const char* hint = "Click + to add an actor";
        ImGui::SetCursorPosX((avail - ImGui::CalcTextSize(hint).x) * 0.5f);
        ImGui::TextDisabled("%s", hint);
    }

    ImGui::End();
}
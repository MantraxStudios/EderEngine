#include "HierarchyPanel.h"
#include "ECS/Components/TagComponent.h"
#include <imgui/imgui.h>
#include <cstdio>

void HierarchyPanel::OnDraw()
{
    if (!ImGui::Begin(Title(), &open)) { ImGui::End(); return; }

    if (!registry)
    {
        ImGui::TextDisabled("No registry");
        ImGui::End();
        return;
    }

    // ── Toolbar ──────────────────────────────────────────────────────────────
    if (ImGui::Button("+ Entity"))
    {
        Entity e = registry->Create();
        registry->Add<TagComponent>(e);
        selected = e;
    }

    ImGui::Separator();

    // ── Entity list ──────────────────────────────────────────────────────────
    for (Entity e : registry->GetEntities())
    {
        const char* name = registry->Has<TagComponent>(e)
            ? registry->Get<TagComponent>(e).name.c_str()
            : "Entity";

        char label[128];
        snprintf(label, sizeof(label), "%s##%u", name, e);

        bool sel = (selected == e);
        if (ImGui::Selectable(label, sel))
            selected = sel ? NULL_ENTITY : e;

        // Right-click context menu
        if (ImGui::BeginPopupContextItem())
        {
            if (ImGui::MenuItem("Duplicate"))
            {
                Entity copy = registry->Create();
                if (registry->Has<TagComponent>(e))
                {
                    auto tag  = registry->Get<TagComponent>(e);
                    tag.name += " (copy)";
                    registry->Add<TagComponent>(copy) = tag;
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Delete"))
            {
                if (selected == e) selected = NULL_ENTITY;
                registry->Destroy(e);
            }
            ImGui::EndPopup();
        }
    }

    ImGui::End();
}

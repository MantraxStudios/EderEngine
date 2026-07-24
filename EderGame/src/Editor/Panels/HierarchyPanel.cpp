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
#include "ECS/Components/PlayerStartComponent.h"
#include "ECS/Components/PlayerComponent.h"
#include "ECS/Systems/TransformSystem.h"
#include <imgui/imgui.h>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <string>

// Strip a trailing " (123)" enumeration suffix, returning the base name.
static std::string BaseName(const std::string& s)
{
    if (s.size() >= 4 && s.back() == ')')
    {
        size_t open = s.rfind(" (");
        if (open != std::string::npos && open + 2 < s.size() - 1)
        {
            bool digits = true;
            for (size_t i = open + 2; i < s.size() - 1; ++i)
                if (!std::isdigit(static_cast<unsigned char>(s[i]))) { digits = false; break; }
            if (digits) return s.substr(0, open);
        }
    }
    return s;
}

// Unity-style unique name: "Cube" → "Cube (1)", "Cube (1)" → "Cube (2)", picking
// the next free number among existing entities so names stay short (no crashes
// from an ever-growing "(copy) (copy) …" string).
static std::string MakeUniqueName(Registry& reg, const std::string& srcName)
{
    const std::string base = BaseName(srcName);
    int maxN = 0;
    reg.Each<TagComponent>([&](Entity, TagComponent& t)
    {
        const std::string& n = t.name;
        if (n == base) return;   // base counts as N = 0
        if (n.size() > base.size() + 3 &&
            n.compare(0, base.size(), base) == 0 &&
            n.compare(base.size(), 2, " (") == 0 && n.back() == ')')
        {
            bool digits = true; int val = 0;
            for (size_t i = base.size() + 2; i < n.size() - 1; ++i)
            {
                char c = n[i];
                if (!std::isdigit(static_cast<unsigned char>(c))) { digits = false; break; }
                val = val * 10 + (c - '0');
            }
            if (digits && val > maxN) maxN = val;
        }
    });
    return base + " (" + std::to_string(maxN + 1) + ")";
}

// Copy all non-hierarchy components from src to dst. renameRoot enumerates the
// name Unity-style (only the branch root should be renamed; children keep theirs).
static void CopyComponents(Entity src, Entity dst, Registry& reg, bool renameRoot)
{
    if (reg.Has<TagComponent>(src))
    {
        auto tag = reg.Get<TagComponent>(src);
        if (renameRoot)
            tag.name = MakeUniqueName(reg, tag.name);
        reg.Add<TagComponent>(dst) = tag;
    }
    if (reg.Has<TransformComponent>(src))
        reg.Add<TransformComponent>(dst) = reg.Get<TransformComponent>(src);
    if (reg.Has<MeshRendererComponent>(src))
        reg.Add<MeshRendererComponent>(dst) = reg.Get<MeshRendererComponent>(src);
    if (reg.Has<LightComponent>(src))
        reg.Add<LightComponent>(dst) = reg.Get<LightComponent>(src);
    if (reg.Has<AnimationComponent>(src))
        reg.Add<AnimationComponent>(dst) = reg.Get<AnimationComponent>(src);
    if (reg.Has<CameraComponent>(src))
        reg.Add<CameraComponent>(dst) = reg.Get<CameraComponent>(src);
    if (reg.Has<ScriptComponent>(src))
        reg.Add<ScriptComponent>(dst) = reg.Get<ScriptComponent>(src);
    if (reg.Has<RigidbodyComponent>(src))
        reg.Add<RigidbodyComponent>(dst) = reg.Get<RigidbodyComponent>(src);
    if (reg.Has<ColliderComponent>(src))
        reg.Add<ColliderComponent>(dst) = reg.Get<ColliderComponent>(src);
    if (reg.Has<CharacterControllerComponent>(src))
        reg.Add<CharacterControllerComponent>(dst) = reg.Get<CharacterControllerComponent>(src);
    if (reg.Has<AudioSourceComponent>(src))
        reg.Add<AudioSourceComponent>(dst) = reg.Get<AudioSourceComponent>(src);
    if (reg.Has<VolumetricFogComponent>(src))
        reg.Add<VolumetricFogComponent>(dst) = reg.Get<VolumetricFogComponent>(src);
    if (reg.Has<LayerComponent>(src))
        reg.Add<LayerComponent>(dst) = reg.Get<LayerComponent>(src);
}

// Recursively duplicate entity e and all its descendants.
// newParent = the already-duplicated parent to attach to (NULL_ENTITY for roots).
// Returns the copy of e.
static Entity DuplicateBranch(Entity e, Entity newParent, Registry& reg, bool isRoot)
{
    Entity copy = reg.Create();
    CopyComponents(e, copy, reg, isRoot);

    // Wire up hierarchy: attach copy to its new parent WITHOUT calling Attach()
    // (which would recalculate local transforms). We preserve local transforms as-is.
    if (newParent != NULL_ENTITY)
    {
        if (!reg.Has<HierarchyComponent>(copy))
            reg.Add<HierarchyComponent>(copy);
        reg.Get<HierarchyComponent>(copy).parent = newParent;

        if (!reg.Has<HierarchyComponent>(newParent))
            reg.Add<HierarchyComponent>(newParent);
        reg.Get<HierarchyComponent>(newParent).children.push_back(copy);
    }

    // Recurse into children
    if (reg.Has<HierarchyComponent>(e))
    {
        for (Entity child : reg.Get<HierarchyComponent>(e).children)
            DuplicateBranch(child, copy, reg, false);
    }

    return copy;
}

void HierarchyPanel::DuplicateSelected()
{
    if (!registry || selected == NULL_ENTITY) return;
    Entity e = selected;

    // The duplicate root gets the same parent as the original
    Entity origParent = NULL_ENTITY;
    if (registry->Has<HierarchyComponent>(e))
        origParent = registry->Get<HierarchyComponent>(e).parent;

    Entity copy = DuplicateBranch(e, origParent, *registry, true);
    selected = copy;
}

void HierarchyPanel::DeleteSelected()
{
    if (!registry || selected == NULL_ENTITY) return;
    Entity toDelete = selected;
    selected = NULL_ENTITY;
    TransformSystem::DestroyWithChildren(toDelete, *registry);
}

void HierarchyPanel::DrawEntityNode(Entity e)
{
    if (!registry) return;

    const char* name = registry->Has<TagComponent>(e)
        ? registry->Get<TagComponent>(e).name.c_str()
        : "Entity";

    const char* icon;
    if      (registry->Has<PlayerStartComponent>(e))  icon = "[PS]";
    else if (registry->Has<PlayerComponent>     (e))  icon = "[PC]";
    else if (registry->Has<LightComponent>      (e))  icon = "[L] ";
    else if (registry->Has<MeshRendererComponent>(e)) icon = "[M] ";
    else                                               icon = "[A] ";

    // Inline rename row (double-click / F2)
    if (m_renaming == e)
    {
        ImGui::PushID((int)e);
        ImGui::SetNextItemWidth(-1);
        if (m_renameFocus) { ImGui::SetKeyboardFocusHere(); m_renameFocus = false; }
        bool done = ImGui::InputText("##rn", m_renameBuf, sizeof(m_renameBuf),
                                     ImGuiInputTextFlags_EnterReturnsTrue |
                                     ImGuiInputTextFlags_AutoSelectAll);
        if (done || ImGui::IsItemDeactivated())
        {
            if (m_renameBuf[0] && registry->Has<TagComponent>(e))
                registry->Get<TagComponent>(e).name = m_renameBuf;
            m_renaming = NULL_ENTITY;
        }
        ImGui::PopID();
        return;
    }

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

    // Double-click on the row = inline rename (Unity-style)
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
    {
        m_renaming    = e;
        m_renameFocus = true;
        strncpy(m_renameBuf, name, sizeof(m_renameBuf) - 1);
        m_renameBuf[sizeof(m_renameBuf) - 1] = '\0';
    }

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

        if (ImGui::MenuItem("Rename", "F2"))
        {
            m_renaming    = e;
            m_renameFocus = true;
            strncpy(m_renameBuf, name, sizeof(m_renameBuf) - 1);
            m_renameBuf[sizeof(m_renameBuf) - 1] = '\0';
        }

        if (ImGui::MenuItem("Duplicate", "Ctrl+D"))
        {
            selected = e;
            DuplicateSelected();
        }

        if (ImGui::MenuItem("Save as Prefab..."))
        {
            selected = e;
            if (m_onSavePrefab) m_onSavePrefab(e);
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
    if (ImGui::Button("+", ImVec2(addBtnW, 0)))
    {
        Entity e = registry->Create();
        registry->Add<TagComponent>(e);
        registry->Add<TransformComponent>(e);
        selected = e;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add Entity");

    // F2 renames the selected entity (when this window has focus)
    if (selected != NULL_ENTITY && ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) &&
        ImGui::IsKeyPressed(ImGuiKey_F2, false))
    {
        m_renaming    = selected;
        m_renameFocus = true;
        const char* nm = registry->Has<TagComponent>(selected)
            ? registry->Get<TagComponent>(selected).name.c_str() : "Entity";
        strncpy(m_renameBuf, nm, sizeof(m_renameBuf) - 1);
        m_renameBuf[sizeof(m_renameBuf) - 1] = '\0';
    }

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
        // Clear selection if it's the entity being destroyed or any of its descendants
        if (selected == pendingDestroy ||
            TransformSystem::IsDescendant(selected, pendingDestroy, *registry))
            selected = NULL_ENTITY;
        TransformSystem::DestroyWithChildren(pendingDestroy, *registry);
    }
}
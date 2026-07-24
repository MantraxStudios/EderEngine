#pragma once
#include "Panel.h"
#include "ECS/Registry.h"
#include "ECS/Entity.h"

class HierarchyPanel : public Panel
{
public:
    const char* Title() const override { return "World Outliner"; }
    void        OnDraw()      override;

    void   SetRegistry(Registry* r) { registry = r; }
    Entity GetSelected()  const     { return selected; }
    void   SetSelected(Entity e)    { selected = e; }
    void   DuplicateSelected();
    void   DeleteSelected();

    // Called when user picks "Save as Prefab" from the context menu.
    // Signature: void(Entity root)
    using SavePrefabCallback = std::function<void(Entity)>;
    void SetSavePrefabCallback(SavePrefabCallback cb) { m_onSavePrefab = std::move(cb); }

private:
    void DrawEntityNode(Entity e);

    Registry* registry       = nullptr;
    Entity    selected       = NULL_ENTITY;
    Entity    pendingDestroy = NULL_ENTITY;

    // Inline rename (double-click / F2, Unity-style)
    Entity    m_renaming     = NULL_ENTITY;
    bool      m_renameFocus  = false;
    char      m_renameBuf[128] = {};

    struct PendingAttach { Entity child = NULL_ENTITY; Entity parent = NULL_ENTITY; };
    PendingAttach pendingAttach;
    Entity        pendingDetach = NULL_ENTITY;

    char searchBuf[128] = {};

    SavePrefabCallback m_onSavePrefab;
};

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

private:
    void DrawEntityNode(Entity e);

    Registry* registry       = nullptr;
    Entity    selected       = NULL_ENTITY;
    Entity    pendingDestroy = NULL_ENTITY;

    // Deferred hierarchy ops — executed after the draw loop finishes
    struct PendingAttach { Entity child = NULL_ENTITY; Entity parent = NULL_ENTITY; };
    PendingAttach pendingAttach;
    Entity        pendingDetach = NULL_ENTITY;

    char searchBuf[128] = {};
};

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
    Entity GetSelected() const      { return selected; }

private:
    Registry* registry       = nullptr;
    Entity    selected       = NULL_ENTITY;
    Entity    pendingDestroy = NULL_ENTITY;
    char      searchBuf[128] = {};
};

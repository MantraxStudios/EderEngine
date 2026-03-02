#pragma once
#include "Panel.h"
#include "ECS/Registry.h"
#include "ECS/Entity.h"

class HierarchyPanel : public Panel
{
public:
    const char* Title() const override { return "Hierarchy"; }
    void        OnDraw()      override;

    void   SetRegistry(Registry* r) { registry = r; }
    Entity GetSelected() const      { return selected; }

private:
    Registry* registry = nullptr;
    Entity    selected = NULL_ENTITY;
};

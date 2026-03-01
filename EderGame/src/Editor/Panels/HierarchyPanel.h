#pragma once
#include "Panel.h"
#include "Core/Scene.h"

class HierarchyPanel : public Panel
{
public:
    const char* Title() const override { return "Hierarchy"; }
    void        OnDraw()      override;

    void SetScene(Scene* s)      { scene = s; }
    int  GetSelected()     const { return selected; }

private:
    Scene* scene    = nullptr;
    int    selected = -1;
};

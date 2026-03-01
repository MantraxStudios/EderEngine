#pragma once
#include "Panel.h"
#include "Core/Scene.h"

class InspectorPanel : public Panel
{
public:
    const char* Title() const override { return "Inspector"; }
    void        OnDraw()      override;

    void SetScene   (Scene* s) { scene    = s; }
    void SetSelected(int    i) { selected = i; }

private:
    Scene* scene    = nullptr;
    int    selected = -1;
};

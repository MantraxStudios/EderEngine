#pragma once
#include "Panel.h"
#include "ECS/Registry.h"
#include "ECS/Entity.h"

class InspectorPanel : public Panel
{
public:
    const char* Title() const override { return "Details"; }
    void        OnDraw()      override;

    void SetRegistry(Registry* r) { registry = r; }
    void SetSelected(Entity e)    { selected = e; }

private:
    void DrawTagComponent();
    void DrawTransformComponent();
    void DrawMeshRendererComponent();
    void DrawLightComponent();
    void DrawVolumetricFogComponent();
    void DrawAnimationComponent();
    void DrawAddComponent();

    Registry* registry = nullptr;
    Entity    selected = NULL_ENTITY;
};

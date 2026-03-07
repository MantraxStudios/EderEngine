#pragma once
#include "Panel.h"
#include "ECS/Registry.h"
#include "ECS/Entity.h"
#include <IO/AssetManager.h>
#include <cstdint>

class InspectorPanel : public Panel
{
public:
    const char* Title() const override { return "Details"; }
    void        OnDraw()      override;

    void SetRegistry(Registry* r) { registry = r; }
    void SetSelected(Entity e)    { selected = e; }

private:
    void DrawTagComponent();
    void DrawHierarchyComponent();
    void DrawTransformComponent();
    void DrawMeshRendererComponent();
    void DrawLightComponent();
    void DrawVolumetricFogComponent();
    void DrawAnimationComponent();
    void DrawRigidbodyComponent();
    void DrawColliderComponent();
    void DrawCharacterControllerComponent();
    void DrawScriptComponent();
    void DrawAudioSourceComponent();
    void DrawAddComponent();

    // Returns true (and fills outPath/outGuid) when an asset is dropped.
    // Renders a Unity-style labelled slot that acts as a drag-drop target.
    bool AssetDropField(const char* label,
                        Krayon::AssetType   expectedType,
                        const std::string&  currentPath,
                        std::string&        outPath,
                        uint64_t&           outGuid);

    Registry* registry = nullptr;
    Entity    selected = NULL_ENTITY;
};

#pragma once
#include "Panel.h"
#include "ECS/Registry.h"
#include "ECS/Entity.h"
#include <IO/AssetManager.h>
#include <functional>
#include <cstdint>

class InspectorPanel : public Panel
{
public:
    const char* Title() const override { return "Details"; }
    void        OnDraw()      override;

    void SetRegistry(Registry* r) { registry = r; }
    void SetSelected(Entity e)    { selected = e; }

    // Returns the submesh count for a given mesh GUID (0 if unknown).
    // Wired up by Application after mesh loading.
    void SetMeshSubmeshCountQuery(std::function<uint32_t(uint64_t)> fn)
    { m_getMeshSubmeshCount = std::move(fn); }

    // Returns the Assimp material name for a given submesh index (empty if unknown).
    void SetMeshSubmeshNameQuery(std::function<std::string(uint64_t, uint32_t)> fn)
    { m_getMeshSubmeshName = std::move(fn); }

    // Called when the user clicks "Edit" on a submesh material slot.
    void SetOpenMaterialCallback(std::function<void(uint64_t)> fn)
    { m_openMaterial = std::move(fn); }

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

    std::function<uint32_t(uint64_t)>              m_getMeshSubmeshCount;
    std::function<std::string(uint64_t, uint32_t)> m_getMeshSubmeshName;
    std::function<void(uint64_t)>                  m_openMaterial;
};

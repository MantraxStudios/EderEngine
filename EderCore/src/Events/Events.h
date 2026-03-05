#pragma once
#include "ECS/Entity.h"
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// Built-in engine events — fire via EventBus<T>::Emit(...)
// ─────────────────────────────────────────────────────────────────────────────

/// Fired whenever an entity is selected (viewport click or outliner click).
/// entity == NULL_ENTITY means "deselected".
struct EntitySelectedEvent
{
    Entity entity = NULL_ENTITY;
};

/// Fired when an entity's TransformComponent changes (gizmo or inspector).
struct TransformChangedEvent
{
    Entity entity = NULL_ENTITY;
};

/// Fired when a component is added to an entity.
struct ComponentAddedEvent
{
    Entity      entity            = NULL_ENTITY;
    const char* componentTypeName = "";
};

/// Fired when a component is removed from an entity.
struct ComponentRemovedEvent
{
    Entity      entity            = NULL_ENTITY;
    const char* componentTypeName = "";
};

/// Fired after a scene is opened or a new scene is created.
struct SceneLoadedEvent
{
    std::string sceneName;
};

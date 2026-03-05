#pragma once

// ─────────────────────────────────────────────────────────────────────────────
//  CameraComponent
//  ECS component that marks an entity as a game camera.
//  Position and orientation are driven by the entity's TransformComponent.
//  Only the entity with isActive == true drives the renderer's main camera.
// ─────────────────────────────────────────────────────────────────────────────

struct CameraComponent
{
    float fov      = 45.0f;   // Vertical field-of-view in degrees
    float nearPlane = 0.1f;   // Near clip plane (metres)
    float farPlane  = 500.0f; // Far  clip plane (metres)
    bool  isActive  = false;  // Only ONE active camera drives the renderer
};

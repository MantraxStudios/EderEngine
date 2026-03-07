#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include "DLLHeader.h"

// ─────────────────────────────────────────────────────────────────────────────
//  Camera — quaternion-based FPS camera.
//
//  Orientation is stored as (yaw, pitch) and converted to a quaternion on
//  demand.  All direction vectors and matrices are derived from that quaternion,
//  so there is no azimuth/elevation trigonometry at query sites.
//
//  Convention (right-handed, -Z forward, +Y up):
//    yaw   — rotation around world +Y  (positive = turn right)
//    pitch — rotation around local +X  (positive = look up), clamped ±89°
// ─────────────────────────────────────────────────────────────────────────────
class EDERGRAPHICS_API Camera
{
public:
    Camera();

    // Accumulate mouse look (pixel deltas).
    void FPSLook(float dx, float dy);

    // Directly set yaw and pitch from radians.
    // Used when syncing orientation from a CameraComponent world matrix.
    void SetOrientation(float yawRad, float pitchRad);

    // ── Direction vectors (world space) ──────────────────────────────────────
    glm::vec3 GetForward()  const;   // camera -Z axis in world space
    glm::vec3 GetRight()    const;   // camera +X axis in world space
    glm::vec3 GetUp()       const;   // camera +Y axis in world space
    glm::vec3 GetPosition() const { return fpsPos; }

    // ── Matrices ─────────────────────────────────────────────────────────────
    glm::mat4 GetView()               const;
    glm::mat4 GetProjection(float aspect) const;

    // ── Public state ─────────────────────────────────────────────────────────
    glm::vec3 fpsPos    = { 0.0f, 1.5f, 8.0f };
    bool      fpsMode   = true;   // kept for API compatibility
    float     fov       = 45.0f;
    float     nearPlane = 0.1f;
    float     farPlane  = 500.0f;

private:
    float m_yaw   = 0.0f;   // radians, world Y-axis
    float m_pitch = 0.0f;   // radians, local X-axis

    // Quaternion from current yaw + pitch.
    glm::quat GetQuat() const;
};

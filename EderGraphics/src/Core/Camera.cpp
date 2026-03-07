#include "Camera.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

Camera::Camera() = default;

// ─────────────────────────────────────────────────────────────────────────────
//  Internal quaternion
//  Build: yaw around world +Y first, then pitch around local +X.
//  This is the standard FPS decomposition and avoids gimbal lock.
// ─────────────────────────────────────────────────────────────────────────────
glm::quat Camera::GetQuat() const
{
    return glm::angleAxis(m_yaw,   glm::vec3(0.0f, 1.0f, 0.0f))
         * glm::angleAxis(m_pitch, glm::vec3(1.0f, 0.0f, 0.0f));
}

// ─────────────────────────────────────────────────────────────────────────────
//  Mouse look
// ─────────────────────────────────────────────────────────────────────────────
void Camera::FPSLook(float dx, float dy)
{
    constexpr float kSens = 0.0018f;
    m_yaw   += (dx * kSens) * -1.0f;
    m_pitch  = glm::clamp(m_pitch - dy * kSens,
                          glm::radians(-89.0f), glm::radians(89.0f));
}

// ─────────────────────────────────────────────────────────────────────────────
//  Direct orientation set (called from CameraComponent sync)
// ─────────────────────────────────────────────────────────────────────────────
void Camera::SetOrientation(float yawRad, float pitchRad)
{
    m_yaw   = yawRad;
    m_pitch = glm::clamp(pitchRad,
                         glm::radians(-89.0f), glm::radians(89.0f));
}

// ─────────────────────────────────────────────────────────────────────────────
//  Direction vectors — all derived from the quaternion
// ─────────────────────────────────────────────────────────────────────────────
glm::vec3 Camera::GetForward() const
{
    return GetQuat() * glm::vec3(0.0f, 0.0f, -1.0f);
}

glm::vec3 Camera::GetRight() const
{
    return GetQuat() * glm::vec3(1.0f, 0.0f, 0.0f);
}

glm::vec3 Camera::GetUp() const
{
    return GetQuat() * glm::vec3(0.0f, 1.0f, 0.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
//  View matrix  =  R^-1 * T^-1
//  Constructed from the quaternion directly — no lookAt trigonometry.
// ─────────────────────────────────────────────────────────────────────────────
glm::mat4 Camera::GetView() const
{
    glm::mat4 R = glm::mat4_cast(glm::inverse(GetQuat()));
    glm::mat4 T = glm::translate(glm::mat4(1.0f), -fpsPos);
    return R * T;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Projection matrix (Vulkan Y-flip applied)
// ─────────────────────────────────────────────────────────────────────────────
glm::mat4 Camera::GetProjection(float aspect) const
{
    glm::mat4 proj = glm::perspective(glm::radians(fov), aspect, nearPlane, farPlane);
    proj[1][1] *= -1.0f;
    return proj;
}

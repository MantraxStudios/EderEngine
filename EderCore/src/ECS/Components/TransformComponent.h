#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/euler_angles.hpp>

struct TransformComponent
{
    glm::vec3 position = { 0.0f, 0.0f, 0.0f };
    glm::vec3 rotation = { 0.0f, 0.0f, 0.0f }; // degrees (pitch, yaw, roll), YXZ order
    glm::vec3 scale    = { 1.0f, 1.0f, 1.0f };

    // Physics-driven rotation set by PhysicsSystem to avoid gimbal lock at ±90° pitch.
    // When true, GetMatrix() applies physicsQuat instead of Euler angles.
    glm::quat physicsQuat    = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    bool      usePhysicsQuat = false;

    glm::mat4 GetMatrix() const
    {
        glm::mat4 m = glm::mat4(1.0f);
        m = glm::translate(m, position);
        if (usePhysicsQuat)
        {
            m *= glm::mat4_cast(physicsQuat);
        }
        else
        {
            m = glm::rotate(m, glm::radians(rotation.y), { 0, 1, 0 });
            m = glm::rotate(m, glm::radians(rotation.x), { 1, 0, 0 });
            m = glm::rotate(m, glm::radians(rotation.z), { 0, 0, 1 });
        }
        m = glm::scale(m, scale);
        return m;
    }
};

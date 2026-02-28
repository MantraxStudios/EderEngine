#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>

struct Transform
{
    glm::vec3 position = { 0.0f, 0.0f, 0.0f };
    glm::vec3 rotation = { 0.0f, 0.0f, 0.0f }; // pitch(X) yaw(Y) roll(Z) en grados
    glm::vec3 scale    = { 1.0f, 1.0f, 1.0f };

    glm::mat4 GetMatrix() const
    {
        glm::mat4 t = glm::translate(glm::mat4(1.0f), position);
        glm::mat4 r = glm::eulerAngleYXZ(
            glm::radians(rotation.y),
            glm::radians(rotation.x),
            glm::radians(rotation.z));
        glm::mat4 s = glm::scale(glm::mat4(1.0f), scale);
        return t * r * s;
    }
};

#include "Camera.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

Camera::Camera(glm::vec3 target, float distance, float fov)
    : target(target)
    , distance(distance)
    , azimuth(0.0f)
    , elevation(glm::radians(20.0f))
    , fov(fov)
{
}

void Camera::OnScroll(double delta)
{
    distance = std::max(0.5f, distance - static_cast<float>(delta) * 0.4f);
}

void Camera::Update(GLFWwindow* window, float /*dt*/)
{
    double mx, my;
    glfwGetCursorPos(window, &mx, &my);

    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS)
    {
        if (!firstMouse)
        {
            float dx =  static_cast<float>(mx - lastMouseX) * 0.005f;
            float dy = -static_cast<float>(my - lastMouseY) * 0.005f;
            azimuth   -= dx;
            elevation  = std::clamp(elevation + dy, glm::radians(-89.0f), glm::radians(89.0f));
        }
        firstMouse = false;
    }
    else
    {
        firstMouse = true;
    }

    lastMouseX = mx;
    lastMouseY = my;
}

glm::vec3 Camera::GetPosition() const
{
    return target + glm::vec3(
        distance * std::cos(elevation) * std::sin(azimuth),
        distance * std::sin(elevation),
        distance * std::cos(elevation) * std::cos(azimuth));
}

glm::mat4 Camera::GetView() const
{
    return glm::lookAt(GetPosition(), target, glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 Camera::GetProjection(float aspect) const
{
    glm::mat4 proj = glm::perspective(glm::radians(fov), aspect, nearPlane, farPlane);
    proj[1][1] *= -1.0f;
    return proj;
}

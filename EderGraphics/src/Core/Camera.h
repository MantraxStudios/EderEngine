#pragma once
#include <glm/glm.hpp>
#include "DLLHeader.h"

class EDERGRAPHICS_API Camera
{
public:
    Camera(glm::vec3 target = {0.0f, 0.0f, 0.0f}, float distance = 5.0f, float fov = 45.0f);

    // dx/dy: mouse delta en píxeles, mouseDown: botón izquierdo presionado
    void Orbit(float dx, float dy);
    void Zoom (float delta);

    glm::mat4 GetView()                const;
    glm::mat4 GetProjection(float aspect) const;
    glm::vec3 GetPosition()            const;

    glm::vec3 target;
    float     distance;
    float     nearPlane = 0.1f;
    float     farPlane  = 500.0f;

private:
    float azimuth;
    float elevation;
    float fov;
};

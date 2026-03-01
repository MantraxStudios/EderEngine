#pragma once
#include <glm/glm.hpp>
#include "DLLHeader.h"

class EDERGRAPHICS_API Camera
{
public:
    Camera(glm::vec3 target = {0.0f, 0.0f, 0.0f}, float distance = 5.0f, float fov = 45.0f);

    // Orbit (modo órbita original)
    void Orbit(float dx, float dy);
    void Zoom (float delta);

    // FPS
    void      FPSLook      (float dx, float dy);  // deltas en píxeles
    void      SetOrientation(float azimuthRad, float elevationRad);
    glm::vec3 GetForward() const;
    glm::vec3 GetRight  () const;

    glm::mat4 GetView()               const;
    glm::mat4 GetProjection(float aspect) const;
    glm::vec3 GetPosition()           const;

    // -- configuración pública --
    bool      fpsMode  = false;
    glm::vec3 fpsPos   = { 0.0f, 1.5f, 8.0f };

    glm::vec3 target;
    float     distance;
    float     nearPlane = 0.1f;
    float     farPlane  = 500.0f;

private:
    float azimuth;    // yaw  (reutilizado en FPS)
    float elevation;  // pitch (reutilizado en FPS)
    float fov;
};

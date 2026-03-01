#pragma once
#include "Panel.h"
#include "Core/Camera.h"

class CameraPanel : public Panel
{
public:
    const char* Title() const override { return "Camera"; }
    void        OnDraw()      override;

    void SetCamera(Camera* c) { camera = c; }

private:
    Camera* camera = nullptr;
};

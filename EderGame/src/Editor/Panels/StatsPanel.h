#pragma once
#include "Panel.h"

class StatsPanel : public Panel
{
public:
    const char* Title() const override { return "Stats"; }
    void        OnDraw()      override;
    void        Update(float dt);

private:
    float smooth = 0.016f;
};

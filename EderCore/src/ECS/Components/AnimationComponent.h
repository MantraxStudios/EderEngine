#pragma once

struct AnimationComponent
{
    int   animIndex   = 0;     // animation clip index (not name) — use 0 for first clip
    float speed       = 1.0f;  // playback multiplier (1 = normal speed)
    bool  loop        = true;  // whether to loop after reaching clip end
    bool  playing     = true;  // true = advancing time each frame
    float currentTime = 0.0f;  // current playback time in seconds (auto-managed)
};

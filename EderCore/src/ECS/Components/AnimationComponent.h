#pragma once

struct AnimationComponent
{
    // ── User-facing ──────────────────────────────────────────────────────────
    int   animIndex     = 0;      // clip to play (0-based index)
    float speed         = 1.0f;   // playback multiplier
    bool  loop          = true;   // loop at end of clip
    bool  playing       = true;   // pause/resume without resetting time
    float blendDuration = 0.2f;   // crossfade duration in seconds when animIndex changes

    // ── Auto-managed (do not write from outside the update loop) ─────────────
    float currentTime    = 0.0f;  // playback time of the active clip
    int   activeIndex    = -1;    // clip currently driving (synced to animIndex with blend)
    int   prevIndex      = -1;    // clip being blended from (-1 = no blend)
    float prevTime       = 0.0f;  // playback time of the source clip during blend
    float blendTime      = 0.0f;  // seconds elapsed since blend started
};

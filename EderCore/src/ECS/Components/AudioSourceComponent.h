#pragma once
#include <string>
#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────────
//  AudioSourceComponent
//  Attach an audio clip asset (.wav / .mp3) to an entity.
//  AudioSystem reads audioGuid via AssetManager and streams/plays it through
//  FMOD with full 3D spatialisation when spatial=true.
//
//  Properties (all serialised):
//    audioGuid    — AssetManager GUID of the audio file
//    audioPath    — relative path (display + fallback reload)
//    volume       — [0.0, 1.0]
//    minDistance  — 3D: distance at which attenuation begins
//    maxDistance  — 3D: distance at which sound is inaudible
//    loop         — loop the clip
//    playOnAwake  — start playing when the scene starts
//    spatial      — true = 3D positional,  false = 2D (no attenuation)
//    muted        — mute without stopping
//
//  Runtime (not serialised):
//    started      — set once playOnAwake has been triggered
// ─────────────────────────────────────────────────────────────────────────────
struct AudioSourceComponent
{
    uint64_t    audioGuid   = 0;
    std::string audioPath;

    float volume      = 1.0f;
    float minDistance = 1.0f;
    float maxDistance = 50.0f;
    bool  loop        = false;
    bool  playOnAwake = true;
    bool  spatial     = true;
    bool  muted       = false;

    // Runtime — never written to .scene
    bool started = false;
};

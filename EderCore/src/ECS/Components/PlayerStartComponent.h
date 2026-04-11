#pragma once
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
//  PlayerStartComponent
//  Place this on an entity to mark it as a player spawn point.
//  At game start the engine automatically loads `prefabPath` and instantiates
//  it at this entity's world position, exactly like Unreal's APlayerStart.
//
//  prefabPath : content-relative path to a .prefab file,
//               e.g. "prefabs/player.prefab"
// ─────────────────────────────────────────────────────────────────────────────
struct PlayerStartComponent
{
    std::string prefabPath;   // relative to Content root
};

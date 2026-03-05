#pragma once
#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────────
// LayerComponent
//
//  Assigns an entity to one of 32 named layers and defines which other layers
//  it interacts with (collision filtering + raycasting mask).
//
//  layer       — index 0-31 that this entity belongs to.
//  layerMask   — 32-bit bitmask; bit N set means this entity interacts with
//                layer N (both collision and raycast queries).
//
//  Example:
//      auto& lc = registry.Add<LayerComponent>(e);
//      lc.layer     = 2;           // belongs to layer 2
//      lc.layerMask = 0b00000101;  // collides with layers 0 and 2
//
//  Layer name constants (optional; game code may define its own):
//      static constexpr uint8_t LAYER_DEFAULT  = 0;
//      static constexpr uint8_t LAYER_PLAYER   = 1;
//      static constexpr uint8_t LAYER_ENEMY    = 2;
//      static constexpr uint8_t LAYER_TRIGGER  = 3;
//      ...
// ─────────────────────────────────────────────────────────────────────────────

struct LayerComponent
{
    uint8_t  layer     = 0;          // which layer this entity is on  (0–31)
    uint32_t layerMask = 0xFFFFFFFF; // which layers it interacts with (all by default)
};

// ── Utility helpers ───────────────────────────────────────────────────────────

/// Build a mask that includes exactly the given layer.
inline constexpr uint32_t LayerBit(uint8_t layer) noexcept
{
    return layer < 32u ? (1u << layer) : 0u;
}

/// Returns true if the two LayerComponents interact with each other.
inline bool LayersInteract(const LayerComponent& a, const LayerComponent& b) noexcept
{
    return (a.layerMask & LayerBit(b.layer)) &&
           (b.layerMask & LayerBit(a.layer));
}

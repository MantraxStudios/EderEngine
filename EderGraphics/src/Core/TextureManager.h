#pragma once
#include "Renderer/Vulkan/VulkanTexture.h"
#include "DLLHeader.h"
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// TextureManager — singleton that owns / caches all VulkanTexture objects.
// A texture is loaded the first time Load() is called for a given path,
// then re-used from cache on subsequent calls.
// ─────────────────────────────────────────────────────────────────────────────
class EDERGRAPHICS_API TextureManager
{
public:
    static TextureManager& Get();

    /// Load a texture from disk (or return cached instance).
    /// Throws std::runtime_error if loading fails (prints to console on first
    /// attempt; subsequent attempts with the same bad path rethrow silently).
    VulkanTexture& Load(const std::string& path);

    bool Has(const std::string& path) const;

    /// Destroy all GPU resources. Call before Vulkan device cleanup.
    void Destroy();

private:
    TextureManager() = default;
    TextureManager(const TextureManager&)            = delete;
    TextureManager& operator=(const TextureManager&) = delete;

    std::unordered_map<std::string, std::unique_ptr<VulkanTexture>> textures;
    std::unordered_set<std::string>                                 failedPaths;
};

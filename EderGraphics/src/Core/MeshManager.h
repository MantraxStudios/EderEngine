#pragma once
#include "Renderer/Vulkan/VulkanMesh.h"
#include "DLLHeader.h"
#include <unordered_map>
#include <memory>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// MeshManager — singleton that owns all loaded VulkanMesh objects, keyed by
// file path. A mesh is loaded on first access and cached for subsequent uses.
// ─────────────────────────────────────────────────────────────────────────────
class EDERGRAPHICS_API MeshManager
{
public:
    static MeshManager& Get();

    // Load a mesh from disk (if not already cached) and return a reference.
    // Throws std::runtime_error if loading fails.
    VulkanMesh& Load(const std::string& path);

    // Returns true if the mesh has been loaded already.
    bool Has(const std::string& path) const;

    // Destroy all GPU resources. Call before Vulkan device cleanup.
    void Destroy();

private:
    MeshManager() = default;
    MeshManager(const MeshManager&)            = delete;
    MeshManager& operator=(const MeshManager&) = delete;

    std::unordered_map<std::string, std::unique_ptr<VulkanMesh>> meshes;
};

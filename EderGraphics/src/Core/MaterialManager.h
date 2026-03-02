#pragma once
#include "Material.h"
#include "MaterialLayout.h"
#include "DLLHeader.h"
#include <unordered_map>
#include <memory>
#include <string>

class EDERGRAPHICS_API VulkanPipeline;

// ─────────────────────────────────────────────────────────────────────────────
// MaterialManager — singleton that owns all GPU materials, keyed by name.
// Always has a "default" material (opaque white, roughness 0.5).
// ─────────────────────────────────────────────────────────────────────────────
class EDERGRAPHICS_API MaterialManager
{
public:
    static MaterialManager& Get();

    // Register a new material. Calls Build(layout, pipeline) internally.
    // Returns a reference to the stored material.
    Material& Add(const std::string& name, const MaterialLayout& layout, VulkanPipeline& pipeline);

    // Retrieve a material by name. Falls back to "default" if not found.
    Material& Get(const std::string& name);

    // Direct access to the "default" material.
    Material& GetDefault();

    bool Has(const std::string& name) const;

    // Destroy all GPU resources. Call before Vulkan device cleanup.
    void Destroy();

private:
    MaterialManager() = default;
    MaterialManager(const MaterialManager&)            = delete;
    MaterialManager& operator=(const MaterialManager&) = delete;
    std::unordered_map<std::string, std::unique_ptr<Material>> materials;
};

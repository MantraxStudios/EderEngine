#include "MaterialManager.h"
#include "Renderer/Vulkan/VulkanPipeline.h"

MaterialManager& MaterialManager::Get()
{
    static MaterialManager instance;
    return instance;
}

Material& MaterialManager::Add(const std::string& name,
                                const MaterialLayout& layout,
                                VulkanPipeline& pipeline)
{
    // Destroy old GPU resources if re-registering the same name
    auto it = materials.find(name);
    if (it != materials.end())
        it->second->Destroy();

    auto mat = std::make_unique<Material>();
    mat->Build(layout, pipeline);
    Material& ref = *mat;
    materials[name] = std::move(mat);
    return ref;
}

Material& MaterialManager::Get(const std::string& name)
{
    auto it = materials.find(name);
    if (it != materials.end()) return *it->second;
    return GetDefault();
}

Material& MaterialManager::GetDefault()
{
    return *materials.at("default");
}

bool MaterialManager::Has(const std::string& name) const
{
    return materials.count(name) > 0;
}

void MaterialManager::Destroy()
{
    for (auto& [name, mat] : materials)
        mat->Destroy();
    materials.clear();
}

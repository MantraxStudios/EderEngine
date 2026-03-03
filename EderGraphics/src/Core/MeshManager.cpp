#include "MeshManager.h"
#include <stdexcept>

MeshManager& MeshManager::Get()
{
    static MeshManager instance;
    return instance;
}

VulkanMesh& MeshManager::Load(const std::string& path)
{
    auto it = meshes.find(path);
    if (it != meshes.end())
        return *it->second;

    auto mesh = std::make_unique<VulkanMesh>();
    mesh->Load(path);
    VulkanMesh* ptr = mesh.get();
    meshes[path] = std::move(mesh);
    return *ptr;
}

bool MeshManager::Has(const std::string& path) const
{
    return meshes.find(path) != meshes.end();
}

void MeshManager::Destroy()
{
    for (auto& [path, mesh] : meshes)
        mesh->Destroy();
    meshes.clear();
}

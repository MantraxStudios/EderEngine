#include "MeshManager.h"
#include <stdexcept>
#include <iostream>

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

    // Already known-bad path — rethrow silently (no repeated console spam)
    if (failedPaths.count(path))
        throw std::runtime_error("[MeshManager] Not found: " + path);

    auto mesh = std::make_unique<VulkanMesh>();
    try
    {
        mesh->Load(path);
    }
    catch (const std::exception& e)
    {
        failedPaths.insert(path);
        std::cerr << "[MeshManager] No se encontro el modelo '" << path << "': " << e.what() << "\n";
        throw;
    }
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

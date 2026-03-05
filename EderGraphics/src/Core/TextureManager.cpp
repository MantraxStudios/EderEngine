#include "TextureManager.h"
#include <stdexcept>
#include <iostream>

TextureManager& TextureManager::Get()
{
    static TextureManager instance;
    return instance;
}

VulkanTexture& TextureManager::Load(const std::string& path)
{
    auto it = textures.find(path);
    if (it != textures.end())
        return *it->second;

    // Already known-bad path — rethrow silently (no repeated console spam)
    if (failedPaths.count(path))
        throw std::runtime_error("[TextureManager] Not found: " + path);

    auto tex = std::make_unique<VulkanTexture>();
    try
    {
        tex->Load(path);
    }
    catch (const std::exception& e)
    {
        failedPaths.insert(path);
        std::cerr << "[TextureManager] No se encontro la textura '" << path << "': " << e.what() << "\n";
        throw;
    }
    VulkanTexture* ptr = tex.get();
    textures[path] = std::move(tex);
    return *ptr;
}

bool TextureManager::Has(const std::string& path) const
{
    return textures.find(path) != textures.end();
}

void TextureManager::Destroy()
{
    for (auto& [path, tex] : textures)
        tex->Destroy();
    textures.clear();
}

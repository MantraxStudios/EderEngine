#include "TextureManager.h"
#include <stdexcept>
#include <iostream>

TextureManager& TextureManager::Get()
{
    static TextureManager instance;
    return instance;
}

VulkanTexture& TextureManager::Load(const std::string& path, bool srgb)
{
    // sRGB and linear views of the same file are cached separately.
    const std::string key = srgb ? path : (path + "\x01lin");

    auto it = textures.find(key);
    if (it != textures.end())
        return *it->second;

    // Already known-bad path — rethrow silently (no repeated console spam)
    if (failedPaths.count(key))
        throw std::runtime_error("[TextureManager] Not found: " + path);

    auto tex = std::make_unique<VulkanTexture>();
    try
    {
        tex->Load(path, srgb);
    }
    catch (const std::exception& e)
    {
        failedPaths.insert(key);
        std::cerr << "[TextureManager] No se encontro la textura '" << path << "': " << e.what() << "\n";
        throw;
    }
    VulkanTexture* ptr = tex.get();
    textures[key] = std::move(tex);
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

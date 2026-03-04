#include "TextureManager.h"
#include <stdexcept>

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

    auto tex = std::make_unique<VulkanTexture>();
    tex->Load(path);
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

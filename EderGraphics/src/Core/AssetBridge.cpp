#include "AssetBridge.h"
#include <IO/AssetManager.h>
#include <iostream>

void EG_InitAssets(const std::string& workDir,
                   bool               compiled,
                   const std::string& pakPath)
{
    std::cout << "[EderGraphics] EG_InitAssets workDir=\"" << workDir
              << "\" compiled=" << compiled << "\n";
    Krayon::AssetManager::Get().Init(workDir, compiled, pakPath);
    std::cout << "[EderGraphics] AssetManager ready — "
              << Krayon::AssetManager::Get().GetAll().size()
              << " assets registered\n";
}

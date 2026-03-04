#pragma once
#include "DLLHeader.h"
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
//  AssetBridge — initialises the AssetManager singleton INSIDE EderGraphics.dll
//
//  Because EderGame.exe and EderGraphics.dll are separate binary images they
//  each get their own copy of every static local (including AssetManager::Get).
//  Calling EG_InitAssets() from main.cpp ensures the DLL's singleton is
//  initialised with the same workDir so LoadSpv / VulkanTexture / VulkanMesh
//  all resolve assets correctly.
// ─────────────────────────────────────────────────────────────────────────────

EDERGRAPHICS_API void EG_InitAssets(const std::string& workDir,
                                    bool               compiled  = false,
                                    const std::string& pakPath   = "");

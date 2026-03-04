// ─────────────────────────────────────────────────────────────────────────────
//  EderPlayer — standalone game runtime (no ImGui / Editor).
//  All assets are loaded from Game.pak (compiled mode).
// ─────────────────────────────────────────────────────────────────────────────

#include "PlayerApp.h"
#include "Core/AssetBridge.h"
#include <IO/AssetManager.h>
#include <IO/KRCompiler.h>
#include <filesystem>
#include <iostream>
#ifdef _WIN32
#include <windows.h>
#endif

int main()
{
    // Ensure CWD = directory containing the executable (where Game.pak lives)
#ifdef _WIN32
    char exePath[MAX_PATH];
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    std::filesystem::current_path(std::filesystem::path(exePath).parent_path());
#endif

    // Init both singletons (exe + DLL) in compiled/PAK mode
    Krayon::AssetManager::Get().Init(".", true, "Game.pak");
    EG_InitAssets(".", true, "Game.pak");

    // Read game.conf embedded in the PAK
    const auto cfgBytes = Krayon::AssetManager::Get().GetBytes("game.conf");
    const Krayon::GameConfig config = cfgBytes.empty()
        ? Krayon::GameConfig{}
        : Krayon::GameConfig::Deserialize(cfgBytes);

    if (cfgBytes.empty())
        std::cerr << "[EderPlayer] Warning: game.conf not found in PAK — using defaults.\n";
    else
        std::cout << "[EderPlayer] Game: " << config.gameName
                  << "  |  Initial scene: " << config.initialScene << "\n";

    PlayerApp app;
    return app.Run(config.initialScene, config.gameName);
}

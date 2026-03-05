// ─────────────────────────────────────────────────────────────────────────────
//  EderPlayer — standalone game runtime (no ImGui / Editor).
//  All assets are loaded from Game.pak (compiled mode).
//  When launched with --preview, assets are loaded from a loose workdir
//  and the given scene file is used directly (editor play mode).
// ─────────────────────────────────────────────────────────────────────────────

#include "PlayerApp.h"
#include "Core/AssetBridge.h"
#include <IO/AssetManager.h>
#include <IO/KRCompiler.h>
#include <filesystem>
#include <iostream>
#include <string>
#ifdef _WIN32
#include <windows.h>
#endif

int main(int argc, char* argv[])
{
    // Ensure CWD = directory containing the executable
#ifdef _WIN32
    char exePath[MAX_PATH];
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    std::filesystem::current_path(std::filesystem::path(exePath).parent_path());
#endif

    // ── Parse command-line arguments ─────────────────────────────────────────
    bool        previewMode = false;
    std::string workdir;
    std::string scenePath;

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--preview")
        {
            previewMode = true;
        }
        else if (arg == "--workdir" && i + 1 < argc)
        {
            workdir = argv[++i];
        }
        else if (arg == "--scene" && i + 1 < argc)
        {
            scenePath = argv[++i];
        }
    }

    // ── Preview mode (editor play mode) ──────────────────────────────────────
    if (previewMode)
    {
        if (workdir.empty() || scenePath.empty())
        {
            std::cerr << "[EderPlayer] --preview requires --workdir and --scene.\n";
            return 1;
        }

        // Init asset managers with raw loose files (non-PAK)
        Krayon::AssetManager::Get().Init(workdir, false);
        EG_InitAssets(workdir, false);

        PlayerApp app;
        return app.RunPreview(scenePath);
    }

    // ── Normal shipping mode (PAK) ────────────────────────────────────────────
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

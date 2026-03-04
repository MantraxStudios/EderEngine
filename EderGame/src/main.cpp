#include "Application.h"
#include "Core/AssetBridge.h"
#include <IO/AssetManager.h>
#include <filesystem>
#ifdef _WIN32
#include <windows.h>
#endif

int main()
{
    // Ensure CWD = directory containing the executable (where shaders/, assets/ live)
#ifdef _WIN32
    char exePath[MAX_PATH];
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    std::filesystem::current_path(std::filesystem::path(exePath).parent_path());
#endif

    // exe and EderGraphics.dll each have their own static singleton 
    // Both must be initialised separately.
    //   • exe singleton  → used by Editor panels (AssetBrowserPanel, etc.)
    //   • dll singleton  → used by VulkanTexture, VulkanMesh, LoadSpv, etc.
    Krayon::AssetManager::Get().Init("Game/Content", false);
    EG_InitAssets("Game/Content", false);

    Application app;
    return app.Run();
}


/*
// ── SHIPPING MODE — replace the editor Init block above with this: ─────────
//
//   1.  Build > Build Game...  in the Editor writes "Game.pak" containing
//       all assets + "game.conf" (gameName= and initialScene=).
//
//   2.  In main() for the shipping executable:
//
//       Krayon::AssetManager::Get().Init(".", true, "Game.pak");
//       EG_InitAssets(".", true, "Game.pak");
//
//       // Read initial scene and game name from the embedded config
//       auto cfgBytes = Krayon::AssetManager::Get().GetBytes("game.conf");
//       auto config   = Krayon::GameConfig::Deserialize(cfgBytes);
//       // config.gameName      → "MyGame"
//       // config.initialScene  → "scenes/main.scene"
//
//       Application app;
//       app.Run();   // App::Init() loads the initial scene via LoadScene()
*/

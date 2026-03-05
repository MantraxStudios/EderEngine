#include "Application.h"
#include "Core/AssetBridge.h"
#include <IO/AssetManager.h>
#include <filesystem>
#include <string>
#ifdef _WIN32
#include <windows.h>
#endif

int main(int argc, char* argv[])
{
    // Ensure CWD = directory containing the executable (where shaders/, assets/ live)
#ifdef _WIN32
    char exePath[MAX_PATH];
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    std::filesystem::current_path(std::filesystem::path(exePath).parent_path());
#endif

    // ── Parse command-line arguments ─────────────────────────────────────────
    // --workdir <path>  : project Content directory (absolute or relative to CWD)
    // --project <name>  : project name shown in the title bar
    std::string workdir     = "Game/Content";
    std::string projectName = "EderEngine";

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--workdir" && i + 1 < argc)
            workdir = argv[++i];
        else if (arg == "--project" && i + 1 < argc)
            projectName = argv[++i];
    }

    // exe and EderGraphics.dll each have their own static singleton
    // Both must be initialised separately.
    //   • exe singleton  → used by Editor panels (AssetBrowserPanel, etc.)
    //   • dll singleton  → used by VulkanTexture, VulkanMesh, LoadSpv, etc.
    Krayon::AssetManager::Get().Init(workdir, false);
    EG_InitAssets(workdir.c_str(), false);

    Application app;
    app.SetProjectName(projectName);
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

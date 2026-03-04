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

    // exe and EderGraphics.dll each have their own static singleton.
    // Both must be initialised separately.
    //   • exe singleton  → used by Editor panels (AssetBrowserPanel, etc.)
    //   • dll singleton  → used by VulkanTexture, VulkanMesh, LoadSpv, etc.
    Krayon::AssetManager::Get().Init("Game/Content", false);
    EG_InitAssets("Game/Content", false);

    Application app;
    return app.Run();
}


/*
// ── PAK workflow (offline build step) ────────────────────────────
Krayon::KRCompiler::Build("Game.pak", {
    { "textures/stone.png", "C:/Assets/stone.png" },
    { "shaders/triangle.frag.spv", "C:/Assets/triangle.frag.spv" }
});

// ── Shipping mode ─────────────────────────────────────────────────
Krayon::AssetManager::Get().Init(".", true, "Game.pak");

// ── Query bytes anywhere ──────────────────────────────────────────
auto bytes = Krayon::AssetManager::Get().GetBytes("textures/stone.png");
auto bytes = Krayon::AssetManager::Get().GetBytesByGuid(guid);
*/


/*
// ── PAK workflow (offline build step) ────────────────────────────
Krayon::KRCompiler::Build("Game.pak", {
    { "textures/stone.png", "C:/Assets/stone.png" },
    { "shaders/triangle.frag.spv", "C:/Assets/triangle.frag.spv" }
});

// ── Shipping mode ─────────────────────────────────────────────────
Krayon::AssetManager::Get().Init(".", true, "Game.pak");

// ── Query bytes anywhere ──────────────────────────────────────────
auto bytes = Krayon::AssetManager::Get().GetBytes("textures/stone.png");
auto bytes = Krayon::AssetManager::Get().GetBytesByGuid(guid);
*/
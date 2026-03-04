#include "Application.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <imgui/imgui.h>
#include <glm/gtc/constants.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/euler_angles.hpp>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <cstdio>

#include "Core/MaterialLayout.h"
#include "Core/MaterialManager.h"
#include "Core/MeshManager.h"
#include "Core/TextureManager.h"
#include "ECS/Components/VolumetricFogComponent.h"
#include "ECS/Components/AnimationComponent.h"
#include "ECS/Systems/TransformSystem.h"
#include "Renderer/VulkanRenderer.h"
#include "Renderer/Vulkan/VulkanInstance.h"
#include "Renderer/Vulkan/VulkanSwapchain.h"
#include "Physics/PhysicsSystem.h"
#include "Scripting/LuaScriptSystem.h"

// ─────────────────────────────────────────────────────────────────────────────
//  Public entry point
// ─────────────────────────────────────────────────────────────────────────────

int Application::Run()
{
    try { Init(); }
    catch (const std::exception& e)
    {
        std::cerr << "[ERROR] Initialization failed: " << e.what() << "\n";
        return -1;
    }

    uint64_t prevTime = SDL_GetTicks();

    while (m_running)
    {
        const uint64_t currTime = SDL_GetTicks();
        const float    dt       = static_cast<float>(currTime - prevTime) / 1000.0f;
        prevTime = currTime;

        PollEvents();
        HandleSceneViewResize();
        ProcessInput(dt);

        m_editor.BeginFrame();
        if (m_lookActive)
        {
            // While in FPS mode imgui should not steal keyboard or mouse.
            ImGui::GetIO().WantCaptureKeyboard = false;
            ImGui::GetIO().WantCaptureMouse    = false;
        }

        VulkanRenderer::Get().BeginFrame();
        if (!VulkanRenderer::Get().IsFrameStarted())
        {
            // Swapchain was rebuilt — skip this frame and re-sync fb sizes.
            m_editor.EndFrame();
            auto& sc  = VulkanSwapchain::Get();
            uint32_t fw = sc.GetExtent().width  / 2;
            uint32_t fh = sc.GetExtent().height / 2;
            if (fw != m_debugFb.GetExtent().width || fh != m_debugFb.GetExtent().height)
            {
                m_debugFb.Recreate(fw, fh);
                m_sunShafts.Resize(fw, fh);
                m_occlusionPass.Resize(fw, fh);
                m_volumetricLight.Resize(fw, fh);
                m_volumetricFog.Resize(fw, fh);
            }
            continue;
        }

        UpdateLightBuffer();

        auto cmd = VulkanRenderer::Get().GetCommandBuffer();

        SyncECSToScene();
        UpdateAnimations(dt);

        PhysicsSystem::Get().SyncActors(m_registry);
        PhysicsSystem::Get().Step(dt);
        PhysicsSystem::Get().WriteBack(m_registry);
        PhysicsSystem::Get().DispatchEvents(m_registry);
        LuaScriptSystem::Get().Update(m_registry, dt);

        RenderShadowPasses(cmd);
        RenderSceneView(cmd);
        RenderPostProcess(cmd);
        RenderMainPass(cmd);

        m_editor.Draw(m_camera, m_registry, dt);
        m_editor.Render(cmd);
        VulkanRenderer::Get().EndFrame();
    }

    Shutdown();
    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Initialization
// ─────────────────────────────────────────────────────────────────────────────

void Application::Init()
{
    SDL_Init(SDL_INIT_VIDEO);

    m_window = SDL_CreateWindow("EderGraphics", 800, 600,
        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    if (!m_window)
        throw std::runtime_error("SDL_CreateWindow failed");

    // Camera — FPS mode, slightly elevated start position
    m_camera.fpsMode = true;
    m_camera.fpsPos  = { 0.0f, 2.0f, 12.0f };
    m_camera.SetOrientation(0.0f, 0.0f);
    SDL_SetWindowRelativeMouseMode(m_window, false);

    VulkanInstance::Get().Init(m_window);
    VulkanSwapchain::Get().Init(m_window);
    VulkanRenderer::Get().Init();
    VulkanRenderer::Get().SetWindow(m_window);
    m_editor.Init(m_window);

    // Main PBR pipeline
    m_pipeline.Create(
        "shaders/triangle.vert.spv",
        "shaders/triangle.frag.spv",
        VulkanSwapchain::Get().GetFormat(),
        VulkanRenderer::Get().GetDepthFormat());

    InitMaterials();

    // Shadow maps — must exist before lights.Build() so samplers are available
    m_shadowMap.Create(1024);
    m_shadowPipeline.Create(m_shadowMap.GetFormat());
    m_spotShadowMap.Create(1024);
    m_pointShadowMap.Create(512);
    m_pointShadowPipeline.Create(m_pointShadowMap.GetFormat());

    m_lights.Build(m_pipeline);
    m_lights.BindShadowMap     (m_shadowMap.GetArrayView(),        m_shadowMap.GetSampler());
    m_lights.BindSpotShadowMap (m_spotShadowMap.GetArrayView(),    m_spotShadowMap.GetSampler());
    m_lights.BindPointShadowMap(m_pointShadowMap.GetCubeArrayView(), m_pointShadowMap.GetSampler());

    InitPostProcess();

    m_boneSSBO.Create(m_pipeline);
    m_editor.SetSceneViewFramebuffer(&m_debugFb);
    PhysicsSystem::Get().Init();
    LuaScriptSystem::Get().Init();
    WireEditorCallbacks();
}

void Application::InitMaterials()
{
    MaterialLayout layout;
    layout.AddVec4 ("albedo")
          .AddFloat("roughness")
          .AddFloat("metallic")
          .AddFloat("emissiveIntensity")
          .AddFloat("alphaThreshold");   // 0 = opaque/blend, >0 = cutout

    MaterialManager::Get().Add("default", layout, m_pipeline);
    m_floorMat.Build(layout, m_pipeline);
    m_glassMat.Build(layout, m_pipeline);
    m_glassMat2.Build(layout, m_pipeline);
    m_glassMat3.Build(layout, m_pipeline);

    // Default — warm off-white
    auto& def = MaterialManager::Get().GetDefault();
    def.SetVec4 ("albedo",            glm::vec4(1.0f, 0.92f, 0.78f, 1.0f));
    def.SetFloat("roughness",         0.55f);
    def.SetFloat("metallic",          0.0f);
    def.SetFloat("emissiveIntensity", 0.0f);

    // Floor — cool grey
    m_floorMat.SetVec4 ("albedo",            glm::vec4(0.55f, 0.58f, 0.62f, 1.0f));
    m_floorMat.SetFloat("roughness",         0.85f);
    m_floorMat.SetFloat("metallic",          0.0f);
    m_floorMat.SetFloat("emissiveIntensity", 0.0f);

    // Glass — translucent blue
    m_glassMat.SetVec4 ("albedo",            glm::vec4(0.40f, 0.70f, 1.0f,  0.35f));
    m_glassMat.SetFloat("roughness",         0.05f);
    m_glassMat.SetFloat("metallic",          0.0f);
    m_glassMat.SetFloat("emissiveIntensity", 0.0f);
    m_glassMat.opacity = 0.35f;

    // Glass 2 — translucent green
    m_glassMat2.SetVec4 ("albedo",            glm::vec4(0.30f, 1.0f,  0.45f, 0.40f));
    m_glassMat2.SetFloat("roughness",         0.05f);
    m_glassMat2.SetFloat("metallic",          0.0f);
    m_glassMat2.SetFloat("emissiveIntensity", 0.0f);
    m_glassMat2.opacity = 0.40f;

    // Glass 3 — translucent amber
    m_glassMat3.SetVec4 ("albedo",            glm::vec4(1.0f,  0.65f, 0.10f, 0.45f));
    m_glassMat3.SetFloat("roughness",         0.05f);
    m_glassMat3.SetFloat("metallic",          0.0f);
    m_glassMat3.SetFloat("emissiveIntensity", 0.0f);
    m_glassMat3.opacity = 0.45f;

    // Shared texture
    try { m_albedoTex.Load("assets/bush01.png"); }
    catch (const std::exception& e)
    {
        std::cerr << "[WARNING] " << e.what() << " — using default texture\n";
        m_albedoTex.CreateDefault();
    }

    def.BindTexture(0, m_albedoTex);
    m_floorMat.BindTexture(0, m_albedoTex);
    m_glassMat.BindTexture(0, m_albedoTex);
    m_glassMat2.BindTexture(0, m_albedoTex);
    m_glassMat3.BindTexture(0, m_albedoTex);
}

void Application::InitPostProcess()
{
    auto& sc     = VulkanSwapchain::Get();
    auto& rd     = VulkanRenderer::Get();
    uint32_t w   = sc.GetExtent().width  / 2;
    uint32_t h   = sc.GetExtent().height / 2;
    auto depthFmt = rd.GetDepthFormat();

    m_debugFb.Create(w, h, vk::Format::eB8G8R8A8Unorm, depthFmt);
    m_debugOverlay.Create(sc.GetFormat(), depthFmt);
    m_skybox.Create(sc.GetFormat(), depthFmt);
    m_gizmo.Create(m_debugFb.GetColorFormat(), depthFmt);
    m_occlusionPass.Create(w, h);
    m_sunShafts.Create(m_debugFb.GetColorFormat(), w, h);
    m_volumetricLight.Create(m_debugFb.GetColorFormat(), w, h,
                             *m_pipeline.GetLightDescriptorSetLayout());
    m_volumetricFog.Create(m_debugFb.GetColorFormat(), w, h,
                           *m_pipeline.GetLightDescriptorSetLayout());
}

// ─────────────────────────────────────────────────────────────────────────────//  Scene operations
// ──────────────────────────────────────────────────────────────────────────────

void Application::WireEditorCallbacks()
{
    m_editor.SetNewSceneCallback([this]() { NewScene(); });

    m_editor.SetSaveSceneCallback([this]()
    {
        if (m_currentScenePath.empty())
            SaveSceneAs(m_currentSceneName);
        else
            SaveScene();
    });

    m_editor.SetSaveAsCallback  ([this](const std::string& name) { SaveSceneAs(name); });
    m_editor.SetOpenSceneCallback([this](const std::string& path) { LoadScene(path); });

    m_editor.SetBuildPakCallback([this](const std::string& outPak,
                                        const std::string& initialScene,
                                        const std::string& gameName)
    {
        BuildPak(outPak, initialScene, gameName);
    });

    // Play: auto-save current scene then reload it so the game starts fresh
    m_editor.SetPlayCallback([this]()
    {
        if (!m_currentScenePath.empty())
            SaveScene();
        m_editor.AppendBuildLog("[Play] Game started.");
    });

    // Stop: nothing special needed in editor mode
    m_editor.SetStopCallback([this]()
    {
        m_editor.AppendBuildLog("[Stop] Game stopped.");
    });

    m_editor.SetCurrentSceneName(m_currentSceneName);
}

void Application::NewScene()
{
    // Clear GPU scene + ECS state
    VulkanInstance::Get().GetDevice().waitIdle();
    m_scene.Clear();
    m_registry.Clear();
    m_lastMeshGuid.clear();
    m_lastAnimMeshGuid.clear();
    m_lastMaterialName.clear();
    m_lastMatTexGuid.clear();

    m_currentScenePath = "";
    m_currentSceneName = "Untitled";
    m_editor.SetCurrentSceneName(m_currentSceneName);
}

void Application::SaveScene()
{
    if (m_currentScenePath.empty()) { SaveSceneAs(m_currentSceneName); return; }
    Krayon::SceneSerializer::Save(m_currentScenePath, m_registry, m_currentSceneName);
}

void Application::SaveSceneAs(const std::string& name)
{
    namespace fs = std::filesystem;
    auto& AM = Krayon::AssetManager::Get();
    if (AM.GetWorkDir().empty()) return;

    // Store under <workDir>/scenes/<name>.scene
    const fs::path scenesDir = fs::path(AM.GetWorkDir()) / "scenes";
    std::error_code ec;
    fs::create_directories(scenesDir, ec);

    // Deduplicate filename
    std::string stem = name;
    fs::path    absFile;
    int         suffix = 0;
    do {
        absFile = scenesDir / (stem + ".scene");
        if (!fs::exists(absFile)) break;
        // If it's the same file we're overwriting, just use it
        if (absFile.string() == m_currentScenePath) break;
        stem = name + "_" + std::to_string(++suffix);
    } while (true);

    if (Krayon::SceneSerializer::Save(absFile.string(), m_registry, stem))
    {
        m_currentScenePath = absFile.string();
        m_currentSceneName = stem;
        AM.RegisterSceneFile(m_currentScenePath, stem);
        m_editor.SetCurrentSceneName(m_currentSceneName);
    }
}

void Application::LoadScene(const std::string& absPath)
{
    namespace fs = std::filesystem;
    if (!fs::exists(absPath)) return;

    // Wait for GPU idle before clearing scene objects
    VulkanInstance::Get().GetDevice().waitIdle();
    m_scene.Clear();
    m_registry.Clear();
    m_lastMeshGuid.clear();
    m_lastAnimMeshGuid.clear();
    m_lastMaterialName.clear();
    m_lastMatTexGuid.clear();

    std::string loadedName;
    if (Krayon::SceneSerializer::Load(absPath, m_registry, &loadedName))
    {
        m_currentScenePath = absPath;
        m_currentSceneName = loadedName.empty()
            ? fs::path(absPath).stem().string()
            : loadedName;
        m_editor.SetCurrentSceneName(m_currentSceneName);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  BuildPak
//  Packs every registered asset in Game/Content into a single .pak file and
//  embeds a game.conf entry that records the initial scene path and game name.
//  Progress is streamed to the Editor build log so the user can follow along.
// ─────────────────────────────────────────────────────────────────────────────

void Application::BuildPak(const std::string& /*outPakPathHint*/,
                            const std::string& initialScene,
                            const std::string& gameName)
{
    namespace fs = std::filesystem;

    auto& AM        = Krayon::AssetManager::Get();
    const auto& all = AM.GetAll();

    // Compute output dir: one folder above the AssetManager content dir.
    //   AM.GetWorkDir() == "Game/Content"  (relative to build dir)
    //   parent           == "Game"
    //   dist dir         == "Game/<GameName>"
    const fs::path buildDir  = fs::current_path();
    const fs::path amContent = fs::path(AM.GetWorkDir()).is_absolute()
                               ? fs::path(AM.GetWorkDir())
                               : buildDir / AM.GetWorkDir();
    const std::string gname  = gameName.empty() ? "EderGame" : gameName;
    const fs::path distDir   = amContent.parent_path() / gname;
    const std::string outPakPath = (distDir / "Game.pak").string();

    // Rescan content folder to catch any files added since last scan
    AM.Scan();

    // Build the file-asset map: relative-path → absolute-path
    std::unordered_map<std::string, std::string> fileMap;
    fileMap.reserve(all.size());
    const std::string workDir = AM.GetWorkDir();

    for (const auto& [guid, meta] : all)
    {
        // Skip .pak entries themselves and any unknown types
        if (meta.type == Krayon::AssetType::Unknown) continue;
        if (meta.type == Krayon::AssetType::PAK)      continue;

        const std::string absPath = workDir + "/" + meta.path;
        if (fs::exists(absPath))
            fileMap[meta.path] = absPath;
    }

    // Build in-memory entries: game.conf + assets.manifest
    Krayon::GameConfig config;
    config.gameName     = gameName.empty() ? "EderGame" : gameName;
    config.initialScene = initialScene;

    const std::string cfgText = config.Serialize();
    std::unordered_map<std::string, std::vector<uint8_t>> memMap;
    memMap["game.conf"] = std::vector<uint8_t>(cfgText.begin(), cfgText.end());

    // Build GUID manifest: one asset per line — "<guid_hex>\t<type>\t<name>\t<path>"
    {
        std::ostringstream manifest;
        for (const auto& [guid, meta] : all)
        {
            if (meta.type == Krayon::AssetType::Unknown) continue;
            if (meta.type == Krayon::AssetType::PAK)     continue;
            manifest << std::hex << guid << std::dec
                     << '\t' << Krayon::AssetTypeToString(meta.type)
                     << '\t' << meta.name
                     << '\t' << meta.path
                     << '\n';
        }
        const std::string ms = manifest.str();
        memMap["assets.manifest"] = std::vector<uint8_t>(ms.begin(), ms.end());
    }

    // Ensure output directory exists
    const fs::path outDir = fs::path(outPakPath).parent_path();
    if (!outDir.empty())
    {
        std::error_code ec;
        fs::create_directories(outDir, ec);
    }

    // Delete old pak so it is always fully regenerated
    {
        std::error_code ec;
        if (fs::exists(outPakPath, ec))
        {
            fs::remove(outPakPath, ec);
            m_editor.AppendBuildLog("[Build] Removed old Game.pak");
        }
    }

    m_editor.AppendBuildLog("[Build] Starting — " + std::to_string(fileMap.size()) + " assets + game.conf");
    m_editor.AppendBuildLog("[Build] Output: " + outPakPath);
    m_editor.AppendBuildLog("[Build] Initial scene: " + (initialScene.empty() ? "(none)" : initialScene));

    try
    {
        Krayon::KRCompiler::Build(
            outPakPath,
            fileMap,
            memMap,
            [this](int done, int total, const std::string& name)
            {
                if (!name.empty())
                {
                    const std::string line = "  [" + std::to_string(done + 1) + "/" +
                                             std::to_string(total) + "]  " + name;
                    m_editor.AppendBuildLog(line);
                }
            });

        m_editor.AppendBuildLog("[Build] Done!  " + outPakPath);

        // ── Launch background thread: compile EderPlayer + package dist ──────────
        m_editor.AppendBuildLog("[Compile] Building EderPlayer.exe...");
        m_editor.SetBuildRunning(true);

        if (m_buildThread.joinable())
            m_buildThread.join();

        const std::string capturedOutPak = outPakPath;

        m_buildThread = std::thread([this, capturedOutPak]() mutable
        {
            namespace fs = std::filesystem;

            // cmake --build . --target EderPlayer  (CWD is already the build dir)
            const std::string cmd = "cmake --build . --target EderPlayer 2>&1";
            FILE* pipe = _popen(cmd.c_str(), "r");
            if (pipe)
            {
                char buf[512];
                while (fgets(buf, sizeof(buf), pipe))
                {
                    std::string line(buf);
                    while (!line.empty() &&
                           (line.back() == '\n' || line.back() == '\r'))
                        line.pop_back();
                    if (!line.empty())
                        m_editor.AppendBuildLog(line);
                }
                const int ret = _pclose(pipe);
                if (ret != 0)
                {
                    m_editor.AppendBuildLog(
                        "[Compile] FAILED (exit=" + std::to_string(ret) + ")");
                    m_editor.SetBuildRunning(false);
                    return;
                }
            }
            else
            {
                m_editor.AppendBuildLog("[Compile] ERROR: could not run cmake");
                m_editor.SetBuildRunning(false);
                return;
            }
            m_editor.AppendBuildLog("[Compile] EderPlayer built OK.");

            // ── Copy dist files ─────────────────────────────────────────────────
            const fs::path bldDir = fs::current_path();
            const fs::path pakSrc = fs::path(capturedOutPak).is_absolute()
                                    ? fs::path(capturedOutPak)
                                    : bldDir / capturedOutPak;
            // pak is already at distDir/Game.pak; outDir == pakSrc.parent_path()
            const fs::path outDir = pakSrc.parent_path();
            std::error_code ec;
            fs::create_directories(outDir, ec);

            auto copyFile = [&](const fs::path& src, const std::string& dstName)
            {
                fs::copy_file(src, outDir / dstName,
                              fs::copy_options::overwrite_existing, ec);
                if (ec)
                    m_editor.AppendBuildLog(
                        "[Package] WARN: could not copy " + src.filename().string());
                else
                    m_editor.AppendBuildLog("[Package] " + dstName);
            };

            copyFile(bldDir / "EderPlayer.exe",         "EderPlayer.exe");
            copyFile(bldDir / "EderGraphics.dll",        "EderGraphics.dll");
            copyFile(bldDir / "SDL3.dll",                "SDL3.dll");
            copyFile(bldDir / "assimp-vc143-mt.dll",     "assimp-vc143-mt.dll");
            copyFile(bldDir / "PhysX_64.dll",            "PhysX_64.dll");
            copyFile(bldDir / "PhysXCommon_64.dll",      "PhysXCommon_64.dll");
            copyFile(bldDir / "PhysXFoundation_64.dll",  "PhysXFoundation_64.dll");
            copyFile(bldDir / "PhysXCooking_64.dll",     "PhysXCooking_64.dll");
            copyFile(bldDir / "lua54.dll",               "lua54.dll");
            // Game.pak is already written directly to outDir — no copy needed

            m_editor.AppendBuildLog("[Package] Done -> " + outDir.string());
            m_editor.SetBuildRunning(false);
        });
        m_buildThread.detach();
    }
    catch (const std::exception& e)
    {
        m_editor.AppendBuildLog(std::string("[Build] ERROR: ") + e.what());
    }
}

// ──────────────────────────────────────────────────────────────────────────────//  Shutdown
// ─────────────────────────────────────────────────────────────────────────────

void Application::Shutdown()
{
    // Wait for any running build thread before tearing down Vulkan resources
    if (m_buildThread.joinable())
        m_buildThread.join();

    VulkanInstance::Get().GetDevice().waitIdle();

    m_editor.Shutdown();
    m_gizmo.Destroy();
    m_boneSSBO.Destroy();
    m_occlusionPass.Destroy();
    m_volumetricLight.Destroy();
    m_volumetricFog.Destroy();
    m_sunShafts.Destroy();
    m_debugOverlay.Destroy();
    m_skybox.Destroy();
    m_debugFb.Destroy();
    m_pointShadowPipeline.Destroy();
    m_pointShadowMap.Destroy();
    m_spotShadowMap.Destroy();
    m_shadowPipeline.Destroy();
    m_shadowMap.Destroy();
    m_lights.Destroy();
    m_scene.Destroy();
    MaterialManager::Get().Destroy();
    MeshManager::Get().Destroy();
    m_floorMat.Destroy();
    m_albedoTex.Destroy();
    m_pipeline.Destroy();

    PhysicsSystem::Get().Shutdown();
    LuaScriptSystem::Get().Shutdown();
    SDL_DestroyWindow(m_window);
    SDL_Quit();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Per-frame: input & events
// ─────────────────────────────────────────────────────────────────────────────

void Application::PollEvents()
{
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        m_editor.ProcessEvent(event);

        switch (event.type)
        {
        case SDL_EVENT_QUIT:
            m_running = false;
            break;
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            VulkanRenderer::Get().SetFramebufferResized();
            break;
        case SDL_EVENT_KEY_DOWN:
            if (event.key.scancode == SDL_SCANCODE_ESCAPE)
                m_running = false;
            break;
        default:
            break;
        }
    }
}

void Application::ProcessInput(float dt)
{
    // RMB state is polled rather than event-driven to avoid losing keyboard
    // events when toggling relative mouse mode inside the event loop.
    float mx, my;
    bool  rmb = (SDL_GetMouseState(&mx, &my) & SDL_BUTTON_RMASK) != 0;

    if (rmb && !m_lookActive)
    {
        m_lookActive = true;
        SDL_SetWindowRelativeMouseMode(m_window, true);
        SDL_RaiseWindow(m_window);
        SDL_GetRelativeMouseState(&mx, &my);   // flush accumulated delta
        m_mouseDX = m_mouseDY = 0.0f;
    }
    else if (!rmb && m_lookActive)
    {
        m_lookActive = false;
        SDL_SetWindowRelativeMouseMode(m_window, false);
        SDL_GetRelativeMouseState(&mx, &my);   // flush
        m_mouseDX = m_mouseDY = 0.0f;
    }
    else if (m_lookActive)
    {
        SDL_GetRelativeMouseState(&m_mouseDX, &m_mouseDY);
    }
    else
    {
        m_mouseDX = m_mouseDY = 0.0f;
    }

    if (!m_lookActive) return;

    m_camera.FPSLook(m_mouseDX, m_mouseDY);

    const bool*     keys  = SDL_GetKeyboardState(nullptr);
    constexpr float speed = 8.0f;
    glm::vec3 fwd   = m_camera.GetForward();
    glm::vec3 right = m_camera.GetRight();
    glm::vec3 fwdXZ = glm::normalize(glm::vec3(fwd.x, 0.0f, fwd.z));

    if (keys[SDL_SCANCODE_W])     m_camera.fpsPos += fwdXZ  * speed * dt;
    if (keys[SDL_SCANCODE_S])     m_camera.fpsPos -= fwdXZ  * speed * dt;
    if (keys[SDL_SCANCODE_A])     m_camera.fpsPos -= right   * speed * dt;
    if (keys[SDL_SCANCODE_D])     m_camera.fpsPos += right   * speed * dt;
    if (keys[SDL_SCANCODE_SPACE]) m_camera.fpsPos.y += speed * dt;
    if (keys[SDL_SCANCODE_LCTRL])  m_camera.fpsPos.y -= speed * dt;
}

void Application::HandleSceneViewResize()
{
    uint32_t svW = 0, svH = 0;
    m_editor.GetSceneViewSize(svW, svH);

    if (svW <= 4 || svH <= 4) return;
    if (svW == m_debugFb.GetExtent().width && svH == m_debugFb.GetExtent().height) return;

    VulkanInstance::Get().GetDevice().waitIdle();
    m_editor.ReleaseSceneViewFramebuffer();
    m_debugFb.Recreate(svW, svH);
    m_sunShafts.Resize(svW, svH);
    m_occlusionPass.Resize(svW, svH);
    m_volumetricLight.Resize(svW, svH);
    m_volumetricFog.Resize(svW, svH);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Per-frame: ECS synchronization
// ─────────────────────────────────────────────────────────────────────────────

void Application::UpdateLightBuffer()
{
    auto& sc     = VulkanSwapchain::Get();
    float aspect = static_cast<float>(sc.GetExtent().width) /
                   static_cast<float>(sc.GetExtent().height);

    // Reset per-frame state
    m_hasDir           = false;
    m_hasSpotShadow    = false;
    m_hasPointShadow   = false;
    m_activeDirDir     = glm::normalize(glm::vec3(-1.0f, -1.0f, -0.4f));
    m_activeDirColor   = glm::vec3(1.0f, 0.9f, 0.7f);
    m_activeDirIntensity = 1.0f;

    m_lights.ClearLights();

    int spotSlot = 0, pointSlot = 0;
    m_registry.Each<LightComponent>([&](Entity e, LightComponent& l)
    {
        if (!m_registry.Has<TransformComponent>(e)) return;
        const auto& tr = m_registry.Get<TransformComponent>(e);

        if (l.type == LightType::Directional)
        {
glm::mat4 mat = TransformSystem::GetWorldMatrix(e, m_registry);
            glm::vec3 dir = glm::normalize(glm::vec3(mat * glm::vec4(0, -1, 0, 0)));

            if (!m_hasDir)
            {
                m_activeDirDir       = dir;
                m_activeDirColor     = l.color;
                m_activeDirIntensity = l.intensity;
                m_hasDir             = true;
            }

            // Fade out as the sun crosses below the horizon
            float sunHorizon = glm::clamp(-dir.y * 5.0f + 1.0f, 0.0f, 1.0f);
            DirectionalLight dl{};
            dl.direction = dir;
            dl.color     = l.color;
            dl.intensity = l.intensity * sunHorizon;
            m_lights.AddDirectional(dl);
        }
        else if (l.type == LightType::Point)
        {
            PointLight pl{};
            pl.position  = glm::vec3(TransformSystem::GetWorldMatrix(e, m_registry)[3]);
            pl.color     = l.color;
            pl.intensity = l.intensity;
            pl.radius    = l.range;

            if (l.castShadow && pointSlot < 1)
            {
                pl.shadowIdx       = 0;
                m_hasPointShadow   = true;
                m_activePointPos   = pl.position;
                m_activePointFar   = l.range;
                m_lights.SetPointFarPlane(0, l.range);
                ++pointSlot;
            }
            else { pl.shadowIdx = -1; }

            m_lights.AddPoint(pl);
        }
        else if (l.type == LightType::Spot)
        {
            glm::mat4 mat = TransformSystem::GetWorldMatrix(e, m_registry);
            glm::vec3 dir = glm::normalize(glm::vec3(mat * glm::vec4(0, -1, 0, 0)));

            SpotLight sl{};
            sl.position  = glm::vec3(mat[3]);
            sl.direction = dir;
            sl.innerCos  = std::cos(glm::radians(l.innerConeAngle));
            sl.outerCos  = std::cos(glm::radians(l.outerConeAngle));
            sl.color     = l.color;
            sl.intensity = l.intensity;
            sl.radius    = l.range;

            if (l.castShadow && spotSlot < 1)
            {
                sl.shadowIdx          = 0;
                m_hasSpotShadow       = true;
                m_activeSpotPos       = sl.position;
                m_activeSpotDir       = dir;
                m_activeSpotOuterCos  = sl.outerCos;
                m_activeSpotFar       = l.range;
                m_activeSpotMatrix    = VulkanSpotShadowMap::ComputeMatrix(
                    m_activeSpotPos, m_activeSpotDir, m_activeSpotOuterCos, 0.3f, m_activeSpotFar);
                m_lights.SetSpotMatrix(0, m_activeSpotMatrix);
                ++spotSlot;
            }
            else { sl.shadowIdx = -1; }

            m_lights.AddSpot(sl);
        }
    });

    m_shadowMap.ComputeCascades(
        m_camera.GetView(), m_camera.GetProjection(aspect),
        m_activeDirDir, m_camera.nearPlane, m_camera.farPlane,
        m_cascadeMatrices, m_cascadeSplits);

    m_lights.SetCascadeData(m_cascadeMatrices, m_cascadeSplits);
    m_lights.SetCameraForward(m_camera.GetForward());
    m_lights.Update(m_camera.GetPosition());
    m_lights.SetSkyAmbient(glm::vec3(0.04f), glm::vec3(0.04f));
}

void Application::SyncECSToScene()
{
    // 1. Remove SceneObjects whose entity no longer has a MeshRendererComponent
    auto& objs = m_scene.GetObjects();
    objs.erase(std::remove_if(objs.begin(), objs.end(),
        [&](const SceneObject& o) {
            bool remove = o.entityId != 0 && !m_registry.Has<MeshRendererComponent>(o.entityId);
            if (remove) {
                m_lastMeshGuid    .erase(o.entityId);
                m_lastAnimMeshGuid.erase(o.entityId);
                m_lastMaterialName.erase(o.entityId);
            }
            return remove;
        }), objs.end());

    // 2. Add SceneObjects for entities that gained a MeshRendererComponent,
    //    OR hot-swap the mesh when the GUID changed (drag-drop from Asset Browser).
    m_registry.Each<MeshRendererComponent>([&](Entity e, MeshRendererComponent& mr)
    {
        // Resolve the load key: prefer GUID, fall back to bare path
        std::string loadPath;
        if (mr.meshGuid != 0)
        {
            const auto* meta = Krayon::AssetManager::Get().FindByGuid(mr.meshGuid);
            if (meta) { loadPath = meta->path; mr.meshPath = loadPath; }
        }
        if (loadPath.empty()) loadPath = mr.meshPath;
        if (loadPath.empty()) return;

        // Find existing SceneObject for this entity (if any)
        // NOTE: Re-fetch GetObjects() each time — a previous Add() may have
        //       reallocated the vector, invalidating any earlier pointer.
        SceneObject* existingObj = nullptr;
        for (auto& o : m_scene.GetObjects())
            if (o.entityId == e) { existingObj = &o; break; }

        // Change detection by GUID (or path if no GUID yet)
        const uint64_t trackGuid = mr.meshGuid ? mr.meshGuid
            : Krayon::AssetManager::Get().GetGuid(loadPath);
        auto it = m_lastMeshGuid.find(e);
        const bool meshChanged = (it == m_lastMeshGuid.end() || it->second != trackGuid);

        // ── .mat asset → runtime Material sync ───────────────────────────────
        // When a materialGuid is assigned, ensure MaterialManager has a runtime
        // Material for it and push the .mat surface params to the GPU material.
        // This runs every frame the entity has a guid, so live edits in the
        // Material Editor are reflected immediately.
        if (mr.materialGuid != 0)
        {
            Krayon::MaterialAsset matAsset;
            if (Krayon::AssetManager::Get().ReadMaterialAsset(mr.materialGuid, matAsset))
            {
                if (!matAsset.name.empty())
                {
                    // Create a runtime material entry if it doesn't exist yet
                    if (!MaterialManager::Get().Has(matAsset.name))
                    {
                        MaterialLayout matLayout;
                        matLayout.AddVec4 ("albedo")
                                 .AddFloat("roughness")
                                 .AddFloat("metallic")
                                 .AddFloat("emissiveIntensity")
                                 .AddFloat("alphaThreshold");
                        MaterialManager::Get().Add(matAsset.name, matLayout, m_pipeline);
                    }
                    // Push surface params from the .mat file to the GPU material
                    Material& rMat = MaterialManager::Get().Get(matAsset.name);
                    rMat.SetVec4 ("albedo",
                        glm::vec4(matAsset.albedo[0], matAsset.albedo[1],
                                  matAsset.albedo[2], matAsset.albedo[3]));
                    rMat.SetFloat("roughness", matAsset.roughness);
                    rMat.SetFloat("metallic",  matAsset.metallic);
                    const float ei = std::max({ matAsset.emissive[0],
                                                matAsset.emissive[1],
                                                matAsset.emissive[2] });
                    rMat.SetFloat("emissiveIntensity", ei);
                    rMat.SetFloat("alphaThreshold",    0.0f);

                    // ── Albedo texture hot-swap (slot 0) ─────────────
                    if (matAsset.albedoTexGuid != 0)
                    {
                        auto& lastTex = m_lastMatTexGuid[matAsset.name];
                        if (lastTex != matAsset.albedoTexGuid)
                        {
                            const Krayon::AssetMeta* texMeta =
                                Krayon::AssetManager::Get().FindByGuid(matAsset.albedoTexGuid);
                            if (texMeta)
                            {
                                VulkanTexture& tex = TextureManager::Get().Load(texMeta->path);
                                rMat.BindTexture(0, tex);
                                lastTex = matAsset.albedoTexGuid;
                            }
                        }
                    }

                    mr.materialName = matAsset.name;  // keep name in sync
                }
            }
        }

        // ── Material hot-swap (independent of mesh change) ───────────────────
        const std::string& curMatName = mr.materialName;
        auto matIt = m_lastMaterialName.find(e);
        const bool matChanged = existingObj &&
            (matIt == m_lastMaterialName.end() || matIt->second != curMatName);
        if (matChanged)
        {
            existingObj->material  = &MaterialManager::Get().Get(curMatName);
            m_lastMaterialName[e]  = curMatName;
        }

        if (existingObj && !meshChanged) return;

        // Load (or re-use cached) mesh
        Material&   mat  = MaterialManager::Get().Get(mr.materialName);
        VulkanMesh& mesh = MeshManager::Get().Load(loadPath);
        m_lastMeshGuid[e]     = trackGuid;
        m_lastMaterialName[e] = mr.materialName;

        if (existingObj)
        {
            existingObj->mesh     = &mesh;
            existingObj->material = &mat;
            return;
        }

        // New entity — create SceneObject
        SceneObject& obj = m_scene.Add(mesh, mat);
        obj.entityId = e;
        m_lastMaterialName[e] = mr.materialName;

        if (m_registry.Has<TransformComponent>(e))
        {
            const auto& t    = m_registry.Get<TransformComponent>(e);
            obj.transform.position = t.position;
            obj.transform.rotation = t.rotation;
            obj.transform.scale    = t.scale;
        }
    });

    // 3. Mirror world transform → SceneObject::transform every frame
    //    (TransformComponent values are LOCAL; SceneObject needs world space)
    //    Re-obtain the vector reference — Add() may have reallocated it.
    for (auto& obj : m_scene.GetObjects())
    {
        if (obj.entityId == 0 || !m_registry.Has<TransformComponent>(obj.entityId)) continue;

        obj.isSkinned = m_registry.Has<AnimationComponent>(obj.entityId);

        glm::mat4 world = TransformSystem::GetWorldMatrix(obj.entityId, m_registry);

        // Decompose world matrix into T / R(YXZ deg) / S
        obj.transform.position = glm::vec3(world[3]);
        obj.transform.scale.x  = glm::length(glm::vec3(world[0]));
        obj.transform.scale.y  = glm::length(glm::vec3(world[1]));
        obj.transform.scale.z  = glm::length(glm::vec3(world[2]));
        glm::mat4 rot = world;
        rot[0] = glm::vec4(glm::vec3(world[0]) / obj.transform.scale.x, 0.0f);
        rot[1] = glm::vec4(glm::vec3(world[1]) / obj.transform.scale.y, 0.0f);
        rot[2] = glm::vec4(glm::vec3(world[2]) / obj.transform.scale.z, 0.0f);
        rot[3] = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        float yRad, xRad, zRad;
        glm::extractEulerAngleYXZ(rot, yRad, xRad, zRad);
        obj.transform.rotation = glm::degrees(glm::vec3(xRad, yRad, zRad));
    }

    // 4. Sync alphaMode → alphaThreshold UBO field
    for (auto& obj : m_scene.GetObjects())
    {
        if (!obj.material) continue;
        float threshold = (obj.material->alphaMode == Material::AlphaMode::AlphaTest)
                          ? obj.material->alphaCutoff : 0.0f;
        obj.material->SetFloat("alphaThreshold", threshold);
    }
}

void Application::UpdateAnimations(float dt)
{
    std::vector<glm::mat4> boneMatrices(MAX_BONES, glm::mat4(1.0f));

    m_registry.Each<AnimationComponent>([&](Entity e, AnimationComponent& anim)
    {
        if (!m_registry.Has<MeshRendererComponent>(e)) return;
        const auto& mr = m_registry.Get<MeshRendererComponent>(e);

        // Resolve load path from GUID first, fall back to stored path
        std::string loadPath;
        if (mr.meshGuid != 0)
        {
            const auto* meta = Krayon::AssetManager::Get().FindByGuid(mr.meshGuid);
            if (meta) loadPath = meta->path;
        }
        if (loadPath.empty()) loadPath = mr.meshPath;
        if (loadPath.empty() || !MeshManager::Get().Has(loadPath)) return;

        // ── Detect mesh swap via GUID → reset playback state ──────────────────
        const uint64_t trackGuid = mr.meshGuid ? mr.meshGuid
            : Krayon::AssetManager::Get().GetGuid(loadPath);
        auto it = m_lastAnimMeshGuid.find(e);
        if (it == m_lastAnimMeshGuid.end() || it->second != trackGuid)
        {
            anim.currentTime = 0.0f;
            anim.activeIndex = -1;
            anim.prevIndex   = -1;
            anim.prevTime    = 0.0f;
            anim.blendTime   = 0.0f;
            anim.playing     = true;
            m_lastAnimMeshGuid[e] = trackGuid;
        }

        VulkanMesh& mesh = MeshManager::Get().Load(loadPath);
        if (mesh.GetBoneCount() == 0 || mesh.GetAnimationCount() == 0) return;

        int clipIdx = glm::clamp(anim.animIndex, 0,
            static_cast<int>(mesh.GetAnimationCount()) - 1);

        // Detect clip change → trigger crossfade
        if (anim.activeIndex != clipIdx)
        {
            anim.prevIndex   = (anim.activeIndex >= 0) ? anim.activeIndex : clipIdx;
            anim.prevTime    = anim.currentTime;
            anim.currentTime = 0.0f;
            anim.activeIndex = clipIdx;
            anim.blendTime   = 0.0f;
            anim.playing     = true;
        }

        if (!anim.playing) return;

        const float step          = dt * anim.speed;
        const float activeDuration = mesh.GetAnimationDuration(
            static_cast<uint32_t>(anim.activeIndex));

        anim.currentTime += step;
        if (!anim.loop && anim.currentTime >= activeDuration)
        {
            anim.currentTime = activeDuration;
            anim.playing     = false;
        }

        if (anim.prevIndex >= 0 && anim.blendTime < anim.blendDuration)
        {
            anim.prevTime  += step;
            anim.blendTime += step;
        }

        std::vector<glm::mat4> boneMats;
        mesh.ComputeBoneTransforms(static_cast<uint32_t>(anim.activeIndex),
                                   anim.currentTime, boneMats);

        const float blendFactor = (anim.prevIndex >= 0 && anim.blendDuration > 0.0f)
            ? glm::clamp(anim.blendTime / anim.blendDuration, 0.0f, 1.0f)
            : 1.0f;

        if (blendFactor < 1.0f)
        {
            std::vector<glm::mat4> prevMats;
            mesh.ComputeBoneTransforms(static_cast<uint32_t>(anim.prevIndex),
                                       anim.prevTime, prevMats);

            const uint32_t boneCount = static_cast<uint32_t>(
                std::min(boneMats.size(), prevMats.size()));
            for (uint32_t i = 0; i < boneCount; ++i)
                boneMats[i] = prevMats[i] + blendFactor * (boneMats[i] - prevMats[i]);
        }
        else
        {
            anim.prevIndex = -1;
            anim.prevTime  = 0.0f;
        }

        for (uint32_t i = 0; i < static_cast<uint32_t>(boneMats.size()) && i < MAX_BONES; ++i)
            boneMatrices[i] = boneMats[i];

        // Store per-entity so DrawSkinned can upload the right matrices
        m_entityBoneMatrices[e] = boneMatrices;
    });

    // Upload identity for static (non-skinned) draws
    static const std::vector<glm::mat4> s_identity(MAX_BONES, glm::mat4(1.0f));
    m_boneSSBO.Upload(s_identity);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Render passes
// ─────────────────────────────────────────────────────────────────────────────

void Application::RenderShadowPasses(vk::CommandBuffer cmd)
{
    // Cascaded directional shadow (4 splits)
    for (uint32_t c = 0; c < VulkanShadowMap::NUM_CASCADES; ++c)
    {
        m_shadowMap.BeginRendering(cmd, c);
        m_shadowPipeline.Bind(cmd);
        m_boneSSBO.BindToSet(cmd, *m_shadowPipeline.GetLayout(), 0);
        m_scene.DrawShadow(cmd, m_shadowPipeline, m_cascadeMatrices[c]);
        m_shadowMap.EndRendering(cmd);
    }
    m_shadowMap.TransitionToShaderRead(cmd);

    // Spot shadow (slot 0)
    if (m_hasSpotShadow)
    {
        m_spotShadowMap.BeginRendering(cmd, 0);
        m_shadowPipeline.Bind(cmd);
        m_boneSSBO.BindToSet(cmd, *m_shadowPipeline.GetLayout(), 0);
        m_scene.DrawShadow(cmd, m_shadowPipeline, m_activeSpotMatrix);
        m_spotShadowMap.EndRendering(cmd);
    }
    m_spotShadowMap.TransitionToShaderRead(cmd);

    // Point shadow — 6 cubemap faces (slot 0)
    if (m_hasPointShadow)
    {
        auto faceMats = VulkanPointShadowMap::ComputeFaceMatrices(
            m_activePointPos, 0.1f, m_activePointFar);

        for (uint32_t face = 0; face < 6; ++face)
        {
            m_pointShadowMap.BeginRendering(cmd, 0, face);
            m_pointShadowPipeline.Bind(cmd);
            m_boneSSBO.BindToSet(cmd, m_pointShadowPipeline.GetLayout(), 0);
            m_scene.DrawShadowPoint(cmd, m_pointShadowPipeline,
                faceMats[face], m_activePointPos, m_activePointFar);
            m_pointShadowMap.EndRendering(cmd);
        }
    }
    m_pointShadowMap.TransitionToShaderRead(cmd, 0);
}

void Application::RenderSceneView(vk::CommandBuffer cmd)
{
    const float aspect = SceneViewAspect();

    m_debugFb.BeginRendering(cmd);

    // Opaque geometry
    m_pipeline.Bind(cmd);
    m_boneSSBO.Bind(cmd, *m_pipeline.GetLayout());
    m_scene.Draw(cmd, m_pipeline, m_camera, aspect, m_lights);

    // Draw skinned (animated) objects one at a time with per-entity bone matrices
    m_pipeline.Bind(cmd);
    m_scene.DrawSkinned(cmd, m_pipeline, m_camera, aspect, m_lights,
        [this, &cmd](uint32_t entityId)
        {
            auto it = m_entityBoneMatrices.find(entityId);
            if (it != m_entityBoneMatrices.end())
                m_boneSSBO.Upload(it->second);
            m_boneSSBO.Bind(cmd, *m_pipeline.GetLayout());
        });

    // Skybox — after opaques so depth test skips covered pixels
    m_skybox.Draw(cmd, m_camera.GetView(), m_camera.GetProjection(aspect), -m_activeDirDir);

    // Transparents — after skybox so skybox doesn't overwrite them
    m_pipeline.BindTransparent(cmd);
    m_scene.DrawTransparent(cmd, m_pipeline, m_camera, aspect, m_lights);

    // Light gizmos on top
    glm::mat4 vp = m_camera.GetProjection(aspect) * m_camera.GetView();
    m_gizmo.Draw(cmd, m_registry, vp, m_editor.GetSelected());

    m_debugFb.EndRendering(cmd);
    m_debugFb.TransitionToShaderRead(cmd);
}

void Application::RenderPostProcess(vk::CommandBuffer cmd)
{
    const float aspect = SceneViewAspect();
    m_postFb = &m_debugFb;

    // ── Volumetric Lighting ───────────────────────────────────────────────────
    {
        LightComponent* volComp = nullptr;
        m_registry.Each<LightComponent>([&](Entity e, LightComponent& l) {
            if (l.volumetricEnabled && !volComp) volComp = &l;
        });

        if (volComp)
        {
            glm::vec3 sunWorldDir = m_hasDir
                ? glm::normalize(-m_activeDirDir)
                : glm::vec3(0.0f, 1.0f, 0.0f);

            // Fade directional scatter below the horizon
            float sunAbove = m_hasDir
                ? glm::clamp(sunWorldDir.y * 5.0f + 1.0f, 0.0f, 1.0f)
                : 0.0f;

            glm::mat4 view  = m_camera.GetView();
            glm::mat4 proj  = m_camera.GetProjection(aspect);
            glm::mat4 invVP = glm::inverse(proj * view);

            m_volumetricLight.Draw(cmd,
                m_debugFb.GetColorView(),  m_debugFb.GetSampler(),
                m_debugFb.GetDepthView(),  m_debugFb.GetSampler(),
                m_shadowMap.GetArrayView(), m_shadowMap.GetSampler(),
                invVP, m_cascadeMatrices, m_cascadeSplits,
                sunWorldDir,
                m_hasDir ? m_activeDirColor     : glm::vec3(0.0f),
                m_hasDir ? m_activeDirIntensity : 0.0f,
                m_camera.GetPosition(),
                m_lights.GetDescriptorSet(),
                volComp->volNumSteps,
                volComp->volDensity,
                volComp->volAbsorption,
                volComp->volG,
                volComp->volIntensity * sunAbove,
                volComp->volMaxDistance,
                volComp->volJitter,
                volComp->volTint);

            m_volumetricLight.GetOutput().TransitionToShaderRead(cmd);
            m_postFb = &m_volumetricLight.GetOutput();
        }
    }

    // ── Volumetric Fog ────────────────────────────────────────────────────────
    {
        VolumetricFogComponent* fogComp = nullptr;
        m_registry.Each<VolumetricFogComponent>([&](Entity e, VolumetricFogComponent& f) {
            if (f.enabled && !fogComp) fogComp = &f;
        });

        if (fogComp)
        {
            glm::mat4 view  = m_camera.GetView();
            glm::mat4 proj  = m_camera.GetProjection(aspect);
            glm::mat4 invVP = glm::inverse(proj * view);

            glm::vec3 sunTowardDir = m_hasDir
                ? glm::normalize(-m_activeDirDir)
                : glm::vec3(0.0f, 1.0f, 0.0f);
            float sunIntensity = m_hasDir ? m_activeDirIntensity : 0.0f;

            m_volumetricFog.Draw(cmd,
                m_postFb->GetColorView(), m_postFb->GetSampler(),
                m_debugFb.GetDepthView(), m_debugFb.GetSampler(),
                invVP, m_camera.GetPosition(),
                fogComp->fogColor,        fogComp->density,
                fogComp->horizonColor,    fogComp->heightFalloff,
                fogComp->sunScatterColor, fogComp->scatterStrength,
                sunTowardDir,             sunIntensity,
                fogComp->heightOffset,
                fogComp->maxFogAmount,
                fogComp->fogStart,
                fogComp->fogEnd,
                m_lights.GetDescriptorSet());

            m_volumetricFog.GetOutput().TransitionToShaderRead(cmd);
            m_postFb = &m_volumetricFog.GetOutput();
        }
    }

    // ── Sun Shafts ────────────────────────────────────────────────────────────
    {
        LightComponent* shaftsComp = nullptr;
        m_registry.Each<LightComponent>([&](Entity e, LightComponent& l) {
            if (l.sunShaftsEnabled && l.type == LightType::Directional && !shaftsComp)
                shaftsComp = &l;
        });

        if (shaftsComp && m_activeDirDir != glm::vec3(0.0f))
        {
            glm::mat4 vp          = m_camera.GetProjection(aspect) * m_camera.GetView();
            glm::vec3 sunWorldDir = glm::normalize(-m_activeDirDir);
            glm::vec4 sunClip     = vp * glm::vec4(sunWorldDir * 1000.0f, 1.0f);

            bool sunInFront = (sunClip.w > 0.0f) &&
                              (glm::dot(m_camera.GetForward(), sunWorldDir) > 0.0f);
            glm::vec2 sunUV = sunInFront
                ? glm::vec2(sunClip.x / sunClip.w, sunClip.y / sunClip.w) * 0.5f + 0.5f
                : glm::vec2(-10.0f);   // off-screen → god-ray intensity = 0

            float sunHeight = glm::normalize(-m_activeDirDir).y;

            m_occlusionPass.Draw(cmd,
                m_debugFb.GetDepthView(), m_debugFb.GetSampler(),
                sunUV, shaftsComp->shaftsSunRadius);

            m_sunShafts.Draw(cmd,
                m_postFb->GetColorView(),  m_postFb->GetSampler(),
                m_occlusionPass.GetView(), m_debugFb.GetDepthView(),
                sunUV,
                shaftsComp->shaftsDensity,    shaftsComp->shaftsBloomScale,
                shaftsComp->shaftsDecay,      shaftsComp->shaftsWeight,
                shaftsComp->shaftsExposure,   shaftsComp->shaftsTint,
                sunHeight);

            m_sunShafts.GetOutput().TransitionToShaderRead(cmd);
            m_editor.SetSceneViewFramebuffer(&m_sunShafts.GetOutput());
        }
        else
        {
            m_editor.SetSceneViewFramebuffer(m_postFb);
        }
    }
}

void Application::RenderMainPass(vk::CommandBuffer cmd)
{
    auto& sc     = VulkanSwapchain::Get();
    float aspect = static_cast<float>(sc.GetExtent().width) /
                   static_cast<float>(sc.GetExtent().height);

    VulkanRenderer::Get().BeginMainPass();

    // Opaque geometry
    m_pipeline.Bind(cmd);
    m_boneSSBO.Bind(cmd, *m_pipeline.GetLayout());
    m_scene.Draw(cmd, m_pipeline, m_camera, aspect, m_lights);

    // Skinned objects
    m_pipeline.Bind(cmd);
    m_scene.DrawSkinned(cmd, m_pipeline, m_camera, aspect, m_lights,
        [this, &cmd](uint32_t entityId)
        {
            auto it = m_entityBoneMatrices.find(entityId);
            if (it != m_entityBoneMatrices.end())
                m_boneSSBO.Upload(it->second);
            m_boneSSBO.Bind(cmd, *m_pipeline.GetLayout());
        });

    // Skybox
    m_skybox.Draw(cmd, m_camera.GetView(), m_camera.GetProjection(aspect), -m_activeDirDir);

    // Transparents
    m_pipeline.BindTransparent(cmd);
    m_boneSSBO.Bind(cmd, *m_pipeline.GetLayout());
    m_scene.DrawTransparent(cmd, m_pipeline, m_camera, aspect, m_lights);

    m_debugOverlay.Draw(cmd, m_debugFb, m_shadowMap);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Utility
// ─────────────────────────────────────────────────────────────────────────────

float Application::SceneViewAspect() const
{
    return static_cast<float>(m_debugFb.GetExtent().width) /
           static_cast<float>(m_debugFb.GetExtent().height);
}

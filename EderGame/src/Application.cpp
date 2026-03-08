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
#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#  include <shellapi.h>
#endif

#include "Core/MaterialLayout.h"
#include "Core/MaterialManager.h"
#include "Core/MeshManager.h"
#include "Core/TextureManager.h"
#include "ECS/Components/VolumetricFogComponent.h"
#include "ECS/Components/AnimationComponent.h"
#include "ECS/Components/CameraComponent.h"
#include "ECS/Components/HierarchyComponent.h"
#include "ECS/Systems/TransformSystem.h"
#include "Renderer/VulkanRenderer.h"
#include "Renderer/Vulkan/VulkanInstance.h"
#include "Renderer/Vulkan/VulkanSwapchain.h"
#include "Physics/PhysicsSystem.h"
#include "Scripting/LuaScriptSystem.h"
#include "Audio/AudioSystem.h"
#include "UI/UISystem.h"
#include <IO/AssetManager.h>
#include <IO/SceneSerializer.h>
#include <IO/DebugDraw.h>

int Application::Run()
{
    try { Init(); }
    catch (const std::exception& e)
    {
        std::cerr << "[ERROR] Initialization failed: " << e.what() << "\n";
        return -1;
    }

    uint64_t prevTime = SDL_GetPerformanceCounter();
    const uint64_t perfFreq = SDL_GetPerformanceFrequency();
    static constexpr float PHYSICS_DT        = 1.0f / 60.0f;
    static constexpr float MAX_DT            = 0.1f;
    static constexpr int   MAX_PHYSICS_STEPS = 5;
    float physAccum = 0.0f;

    while (m_running)
    {
        const uint64_t currTime = SDL_GetPerformanceCounter();
        float dt = static_cast<float>(currTime - prevTime) / static_cast<float>(perfFreq);
        dt = std::min(dt, MAX_DT);
        prevTime = currTime;

        PollEvents();

        if (m_playingInline && m_editor.GetPlayState() == PlayState::Playing)
        {
            {
                std::string next = LuaScriptSystem::Get().ConsumePendingScene();
                if (!next.empty())
                {
                    LuaScriptSystem::Get().Shutdown();
                    PhysicsSystem::Get().Shutdown();
                    AudioSystem::Get().Shutdown();
                    m_registry.Clear();
                    m_scene.GetObjects().clear();
                    const auto bytes = Krayon::AssetManager::Get().GetBytes(next);
                    if (!bytes.empty())
                        Krayon::SceneSerializer::LoadFromBytes(bytes, m_registry);
                    else
                        Krayon::SceneSerializer::Load(next, m_registry);
                    PhysicsSystem::Get().Init();
                    LuaScriptSystem::Get().Init();
                    AudioSystem::Get().Init();
                    physAccum = 0.0f;
                }
            }

            physAccum += dt;

            int steps = 0;
            while (physAccum >= PHYSICS_DT && steps < MAX_PHYSICS_STEPS)
            {
                LuaScriptSystem::Get().Update(m_registry, PHYSICS_DT);
                PhysicsSystem::Get().SyncActors(m_registry);
                PhysicsSystem::Get().SyncControllers(m_registry);
                PhysicsSystem::Get().Step(PHYSICS_DT);
                PhysicsSystem::Get().WriteBack(m_registry);
                PhysicsSystem::Get().WriteBackControllers(m_registry);
                PhysicsSystem::Get().DispatchEvents(m_registry);
                physAccum -= PHYSICS_DT;
                ++steps;
            }

            {
                glm::vec3 fwd = m_camera.GetForward() * -1.0f;;
                glm::vec3 up  = m_camera.GetUp();
                AudioSystem::Get().SetListenerTransform(m_camera.fpsPos, fwd, up);
            }

            AudioSystem::Get().Update(m_registry, dt);

            if (steps >= MAX_PHYSICS_STEPS)
                physAccum = 0.0f;
        }

        HandleSceneViewResize();
        ProcessInput(dt);

        m_editor.BeginFrame();
        if (m_lookActive)
        {
            ImGui::GetIO().WantCaptureKeyboard = false;
            ImGui::GetIO().WantCaptureMouse    = false;
        }

        VulkanRenderer::Get().BeginFrame();
        if (!VulkanRenderer::Get().IsFrameStarted())
        {
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

        {
            ImVec2 svPos = m_editor.GetSceneViewContentPos();
            UISystem::Get().SetViewportOffset(svPos.x, svPos.y);
        }
        UISystem::Get().Update(dt);

        if (m_playerProcess)
            UpdatePlayerWindowPos();

        RenderShadowPasses(cmd);
        RenderSceneView(cmd);
        RenderPostProcess(cmd);
        RenderMainPass(cmd);

        m_editor.Draw(m_camera, m_registry, dt);
        m_editor.Render(cmd);
        VulkanRenderer::Get().EndFrame();
        Krayon::DebugDraw::Get().Tick(dt);
    }

    Shutdown();
    return 0;
}

void Application::Init()
{
    SDL_Init(SDL_INIT_VIDEO);

    m_window = SDL_CreateWindow(
        ("EderEngine — " + m_projectName).c_str(), 800, 600,
        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    if (!m_window)
        throw std::runtime_error("SDL_CreateWindow failed");

#ifdef _WIN32
    // Enable OS-level drag-and-drop onto the window
    {
        HWND hwnd = (HWND)SDL_GetPointerProperty(
            SDL_GetWindowProperties(m_window),
            SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
        if (hwnd) DragAcceptFiles(hwnd, TRUE);
    }
#endif

    m_camera.fpsMode = true;
    m_camera.fpsPos  = { 0.0f, 2.0f, 12.0f };
    m_camera.SetOrientation(0.0f, 0.0f);
    SDL_SetWindowRelativeMouseMode(m_window, false);

    VulkanInstance::Get().Init(m_window);
    VulkanSwapchain::Get().Init(m_window);
    VulkanRenderer::Get().Init();
    VulkanRenderer::Get().SetWindow(m_window);
    m_editor.Init(m_window);

    m_pipeline.Create(
        "shaders/triangle.vert.spv",
        "shaders/triangle.frag.spv",
        VulkanSwapchain::Get().GetFormat(),
        VulkanRenderer::Get().GetDepthFormat());

    InitMaterials();

    m_shadowMap.Create(1024);
    m_shadowPipeline.Create(m_shadowMap.GetFormat());
    m_spotShadowMap.Create(1024);
    m_pointShadowMap.Create(512);
    m_pointShadowPipeline.Create(m_pointShadowMap.GetFormat());

    m_lights.Build(m_pipeline);
    m_lights.BindShadowMap     (m_shadowMap.GetArrayView(),          m_shadowMap.GetSampler());
    m_lights.BindSpotShadowMap (m_spotShadowMap.GetArrayView(),      m_spotShadowMap.GetSampler());
    m_lights.BindPointShadowMap(m_pointShadowMap.GetCubeArrayView(), m_pointShadowMap.GetSampler());

    InitPostProcess();

    m_boneSSBO.Create(m_pipeline);
    m_editor.SetSceneViewFramebuffer(&m_debugFb);

    UISystem::Get().Init();
    UISystem::Get().SetWindow(m_window);
    m_uiRenderer.Create(vk::Format::eB8G8R8A8Unorm, VulkanRenderer::Get().GetDepthFormat());

    PhysicsSystem::Get().Init();
    LuaScriptSystem::Get().Init();
    AudioSystem::Get().Init();
    WireEditorCallbacks();
}

void Application::InitMaterials()
{
    MaterialLayout layout;
    layout.AddVec4 ("albedo")
          .AddFloat("roughness")
          .AddFloat("metallic")
          .AddFloat("emissiveIntensity")
          .AddFloat("alphaThreshold");

    MaterialManager::Get().Add("default", layout, m_pipeline);
    m_floorMat.Build(layout, m_pipeline);
    m_glassMat.Build(layout, m_pipeline);
    m_glassMat2.Build(layout, m_pipeline);
    m_glassMat3.Build(layout, m_pipeline);

    auto& def = MaterialManager::Get().GetDefault();
    def.SetVec4 ("albedo",            glm::vec4(1.0f, 0.92f, 0.78f, 1.0f));
    def.SetFloat("roughness",         0.55f);
    def.SetFloat("metallic",          0.0f);
    def.SetFloat("emissiveIntensity", 0.0f);

    m_floorMat.SetVec4 ("albedo",            glm::vec4(0.55f, 0.58f, 0.62f, 1.0f));
    m_floorMat.SetFloat("roughness",         0.85f);
    m_floorMat.SetFloat("metallic",          0.0f);
    m_floorMat.SetFloat("emissiveIntensity", 0.0f);

    m_glassMat.SetVec4 ("albedo",            glm::vec4(0.40f, 0.70f, 1.0f,  0.35f));
    m_glassMat.SetFloat("roughness",         0.05f);
    m_glassMat.SetFloat("metallic",          0.0f);
    m_glassMat.SetFloat("emissiveIntensity", 0.0f);
    m_glassMat.opacity = 0.35f;

    m_glassMat2.SetVec4 ("albedo",            glm::vec4(0.30f, 1.0f,  0.45f, 0.40f));
    m_glassMat2.SetFloat("roughness",         0.05f);
    m_glassMat2.SetFloat("metallic",          0.0f);
    m_glassMat2.SetFloat("emissiveIntensity", 0.0f);
    m_glassMat2.opacity = 0.40f;

    m_glassMat3.SetVec4 ("albedo",            glm::vec4(1.0f,  0.65f, 0.10f, 0.45f));
    m_glassMat3.SetFloat("roughness",         0.05f);
    m_glassMat3.SetFloat("metallic",          0.0f);
    m_glassMat3.SetFloat("emissiveIntensity", 0.0f);
    m_glassMat3.opacity = 0.45f;

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
    auto& sc      = VulkanSwapchain::Get();
    auto& rd      = VulkanRenderer::Get();
    uint32_t w    = sc.GetExtent().width  / 2;
    uint32_t h    = sc.GetExtent().height / 2;
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

void Application::RebuildPostProcessPasses()
{
    VulkanInstance::Get().GetDevice().waitIdle();

    m_ppPasses.clear();
    m_ppPasses.reserve(m_ppGraph.effects.size());

    uint32_t w = m_debugFb.GetExtent().width;
    uint32_t h = m_debugFb.GetExtent().height;

    for (const auto& fx : m_ppGraph.effects)
    {
        auto pass = std::make_unique<VulkanPostProcessPass>();
        try {
            pass->Create(m_debugFb.GetColorFormat(), w, h, fx.fragShaderPath);
            m_ppPasses.push_back(std::move(pass));
        }
        catch (const std::exception& e) {
            m_editor.AppendBuildLog(std::string("[PostProcess] Failed to create '")
                + fx.name + "': " + e.what());
            m_ppPasses.push_back(nullptr);  
        }
    }

    m_ppDirty = false;
}

void Application::WireEditorCallbacks()
{
    m_editor.SetNewSceneCallback([this]() { NewScene(); });

    m_editor.SetPostProcessGraph(&m_ppGraph, [this]() { m_ppDirty = true; });

    m_editor.SetSaveSceneCallback([this]()
    {
        if (m_currentScenePath.empty())
            SaveSceneAs(m_currentSceneName);
        else
            SaveScene();
    });

    m_editor.SetSaveAsCallback   ([this](const std::string& name) { SaveSceneAs(name); });
    m_editor.SetOpenSceneCallback([this](const std::string& path) { LoadScene(path); });

    m_editor.SetBuildPakCallback([this](const std::string& outPak,
                                        const std::string& initialScene,
                                        const std::string& gameName)
    {
        BuildPak(outPak, initialScene, gameName);
    });

    m_editor.SetPlayCallback([this]() { StartPlayMode(); });
    m_editor.SetStopCallback([this]() { StopPlayMode(); });
    m_editor.SetCurrentSceneName(m_currentSceneName);

    m_editor.SetMeshSubmeshCountQuery([](uint64_t guid) -> uint32_t {
        if (guid == 0) return 0;
        const auto* meta = Krayon::AssetManager::Get().FindByGuid(guid);
        if (!meta || !MeshManager::Get().Has(meta->path)) return 0;
        try { return MeshManager::Get().Load(meta->path).GetSubmeshCount(); }
        catch (...) { return 0; }
    });

    m_editor.SetMeshSubmeshNameQuery([](uint64_t guid, uint32_t index) -> std::string {
        if (guid == 0) return {};
        const auto* meta = Krayon::AssetManager::Get().FindByGuid(guid);
        if (!meta || !MeshManager::Get().Has(meta->path)) return {};
        try {
            auto& mesh = MeshManager::Get().Load(meta->path);
            if (index >= mesh.GetSubmeshCount()) return {};
            return mesh.GetSubmeshInfo(index).name;
        }
        catch (...) { return {}; }
    });
}

void Application::StartPlayMode()
{
    const bool wantEmbedded = (m_editor.GetPlayTarget() == PlayTarget::Embedded);

    if (wantEmbedded)
    {
        namespace fs = std::filesystem;

        const std::string workdirAbs = fs::absolute(
            Krayon::AssetManager::Get().GetWorkDir()).string();
        m_tempScenePath = (fs::path(workdirAbs) / "__playsnapshot__.scene").string();
        Krayon::SceneSerializer::Save(m_tempScenePath, m_registry, "__playsnapshot__");

        PhysicsSystem::Get().Init();
        LuaScriptSystem::Get().Init();
        AudioSystem::Get().Init();
        m_playingInline = true;

        m_editor.AppendBuildLog("[Play] Running inline in editor.");
        return;
    }

#ifdef _WIN32
    {
        namespace fs = std::filesystem;

        if (!m_currentScenePath.empty()) SaveScene();

        const std::string workdirAbs = fs::absolute(
            Krayon::AssetManager::Get().GetWorkDir()).string();

        std::string scenePath = m_currentScenePath;
        if (scenePath.empty())
        {
            m_tempScenePath = (fs::path(workdirAbs) / "__preview__.scene").string();
            Krayon::SceneSerializer::Save(m_tempScenePath, m_registry, "__preview__");
            scenePath = m_tempScenePath;
        }
        else
        {
            m_tempScenePath.clear();
        }

        char exeBuf[MAX_PATH] = {};
        GetModuleFileNameA(nullptr, exeBuf, MAX_PATH);
        const fs::path playerExe = fs::path(exeBuf).parent_path() / "EderPlayer.exe";

        if (!fs::exists(playerExe))
        {
            m_editor.AppendBuildLog("[Play] ERROR: EderPlayer.exe not found at: " +
                                    playerExe.string());
            m_editor.ForceStop();
            return;
        }

        const std::string cmdLine = "\"" + playerExe.string() + "\""
            + " --preview"
            + " --workdir \"" + workdirAbs + "\""
            + " --scene \""  + scenePath  + "\"";

        STARTUPINFOA si = {};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi = {};

        std::vector<char> cmdBuf(cmdLine.begin(), cmdLine.end());
        cmdBuf.push_back('\0');

        if (!CreateProcessA(nullptr, cmdBuf.data(),
                            nullptr, nullptr, FALSE, 0,
                            nullptr, nullptr, &si, &pi))
        {
            m_editor.AppendBuildLog("[Play] ERROR: Failed to launch EderPlayer.exe "
                                    "(GetLastError=" + std::to_string(GetLastError()) + ").");
            m_editor.ForceStop();
            return;
        }

        m_playerProcess = reinterpret_cast<void*>(pi.hProcess);
        CloseHandle(pi.hThread);

        m_editor.AppendBuildLog("[Play] EderPlayer launched standalone (PID "
                                + std::to_string(pi.dwProcessId) + ").");
    }
#else
    m_editor.AppendBuildLog("[Play] Standalone mode is only supported on Windows.");
    m_editor.ForceStop();
#endif
}

void Application::StopPlayMode()
{
    if (m_playingInline)
    {
        PhysicsSystem::Get().Shutdown();
        LuaScriptSystem::Get().Shutdown();
        AudioSystem::Get().Shutdown();
        m_playingInline = false;

        if (!m_tempScenePath.empty())
        {
            VulkanInstance::Get().GetDevice().waitIdle();
            m_scene.Clear();
            m_registry.Clear();
            m_lastMeshGuid.clear();
            m_lastAnimMeshGuid.clear();
            m_lastMaterialName.clear();
            m_lastMatTexGuid.clear();

            std::string name;
            Krayon::SceneSerializer::Load(m_tempScenePath, m_registry, &name);
            std::filesystem::remove(m_tempScenePath);
            m_tempScenePath.clear();
        }

        m_editor.AppendBuildLog("[Stop] Game stopped.");
        return;
    }

#ifdef _WIN32
    if (m_playerProcess)
    {
        TerminateProcess(reinterpret_cast<HANDLE>(m_playerProcess), 0);
        CloseHandle(reinterpret_cast<HANDLE>(m_playerProcess));
        m_playerProcess = nullptr;
    }
    if (!m_tempScenePath.empty())
    {
        std::filesystem::remove(m_tempScenePath);
        m_tempScenePath.clear();
    }
    m_editor.AppendBuildLog("[Stop] EderPlayer stopped.");
#endif
}

void Application::UpdatePlayerWindowPos()
{
#ifdef _WIN32
    HANDLE hProcess = reinterpret_cast<HANDLE>(m_playerProcess);
    if (!hProcess) return;

    if (WaitForSingleObject(hProcess, 0) == WAIT_OBJECT_0)
    {
        CloseHandle(hProcess);
        m_playerProcess = nullptr;
        m_editor.ForceStop();
        if (!m_tempScenePath.empty())
        {
            std::filesystem::remove(m_tempScenePath);
            m_tempScenePath.clear();
        }
        m_editor.AppendBuildLog("[Play] EderPlayer exited.");
    }
#endif
}

void Application::NewScene()
{
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
    Krayon::SceneSerializer::Save(m_currentScenePath, m_registry, m_currentSceneName, &m_ppGraph);
}

void Application::SaveSceneAs(const std::string& name)
{
    namespace fs = std::filesystem;
    auto& AM = Krayon::AssetManager::Get();
    if (AM.GetWorkDir().empty()) return;

    const fs::path scenesDir = fs::path(AM.GetWorkDir()) / "scenes";
    std::error_code ec;
    fs::create_directories(scenesDir, ec);

    std::string stem = name;
    fs::path    absFile;
    int         suffix = 0;
    do {
        absFile = scenesDir / (stem + ".scene");
        if (!fs::exists(absFile)) break;
        if (absFile.string() == m_currentScenePath) break;
        stem = name + "_" + std::to_string(++suffix);
    } while (true);

    if (Krayon::SceneSerializer::Save(absFile.string(), m_registry, stem, &m_ppGraph))
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

    VulkanInstance::Get().GetDevice().waitIdle();
    m_scene.Clear();
    m_registry.Clear();
    m_lastMeshGuid.clear();
    m_lastAnimMeshGuid.clear();
    m_lastMaterialName.clear();
    m_lastMatTexGuid.clear();

    std::string loadedName;
    m_ppGraph = {};
    if (Krayon::SceneSerializer::Load(absPath, m_registry, &loadedName, &m_ppGraph))
    {
        m_currentScenePath = absPath;
        m_currentSceneName = loadedName.empty()
            ? fs::path(absPath).stem().string()
            : loadedName;
        m_editor.SetCurrentSceneName(m_currentSceneName);
        m_ppDirty = true;
    }
}

void Application::BuildPak(const std::string&,
                            const std::string& initialScene,
                            const std::string& gameName)
{
    namespace fs = std::filesystem;

    auto& AM        = Krayon::AssetManager::Get();
    const auto& all = AM.GetAll();

    const fs::path buildDir  = fs::current_path();
    const fs::path amContent = fs::path(AM.GetWorkDir()).is_absolute()
                               ? fs::path(AM.GetWorkDir())
                               : buildDir / AM.GetWorkDir();
    const std::string gname  = gameName.empty() ? "EderGame" : gameName;
    const fs::path distDir   = amContent.parent_path() / gname;
    const std::string outPakPath = (distDir / "Game.pak").string();

    AM.Scan();

    std::unordered_map<std::string, std::string> fileMap;
    fileMap.reserve(all.size());
    const std::string workDir = AM.GetWorkDir();

    for (const auto& [guid, meta] : all)
    {
        if (meta.type == Krayon::AssetType::Unknown) continue;
        if (meta.type == Krayon::AssetType::PAK)     continue;

        const std::string absPath = workDir + "/" + meta.path;
        if (fs::exists(absPath))
            fileMap[meta.path] = absPath;
    }

    Krayon::GameConfig config;
    config.gameName     = gameName.empty() ? "EderGame" : gameName;
    config.initialScene = initialScene;

    const std::string cfgText = config.Serialize();
    std::unordered_map<std::string, std::vector<uint8_t>> memMap;
    memMap["game.conf"] = std::vector<uint8_t>(cfgText.begin(), cfgText.end());

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

    const fs::path outDir = fs::path(outPakPath).parent_path();
    if (!outDir.empty())
    {
        std::error_code ec;
        fs::create_directories(outDir, ec);
    }

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

        m_editor.AppendBuildLog("[Compile] Building EderPlayer.exe...");
        m_editor.SetBuildRunning(true);

        if (m_buildThread.joinable())
            m_buildThread.join();

        const std::string capturedOutPak = outPakPath;

        m_buildThread = std::thread([this, capturedOutPak]() mutable
        {
            namespace fs = std::filesystem;

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

            const fs::path bldDir = fs::current_path();
            const fs::path pakSrc = fs::path(capturedOutPak).is_absolute()
                                    ? fs::path(capturedOutPak)
                                    : bldDir / capturedOutPak;
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
            copyFile(bldDir / "lua54.dll",               "lua54.dll");

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

void Application::Shutdown()
{
    if (m_buildThread.joinable())
        m_buildThread.join();

    VulkanInstance::Get().GetDevice().waitIdle();

    m_editor.Shutdown();
    m_ppPasses.clear();
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
    AudioSystem::Get().Shutdown();
    UISystem::Get().Shutdown();
    m_uiRenderer.Destroy();
    SDL_DestroyWindow(m_window);
    SDL_Quit();
}

void Application::PollEvents()
{
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        m_editor.ProcessEvent(event);
        UISystem::Get().HandleEvent(event);

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
        case SDL_EVENT_MOUSE_MOTION:
            if (m_lookActive)
            {
                m_mouseDX += event.motion.xrel;
                m_mouseDY += event.motion.yrel;
            }
            break;
        case SDL_EVENT_DROP_BEGIN:
            std::cout << "[Drop] BEGIN\n";
            m_pendingDropFiles.clear();
            break;
        case SDL_EVENT_DROP_FILE:
            std::cout << "[Drop] FILE: " << (event.drop.data ? event.drop.data : "<null>") << "\n";
            if (event.drop.data)
                m_pendingDropFiles.push_back(event.drop.data);
            break;
        case SDL_EVENT_DROP_COMPLETE:
            std::cout << "[Drop] COMPLETE, files=" << m_pendingDropFiles.size() << "\n";
            if (!m_pendingDropFiles.empty())
            {
                m_editor.HandleFileDropBatch(m_pendingDropFiles);
                m_pendingDropFiles.clear();
            }
            break;
        default:
            break;
        }
    }
}

void Application::ProcessInput(float dt)
{
    float mx, my;
    bool  rmb = (SDL_GetMouseState(&mx, &my) & SDL_BUTTON_RMASK) != 0;

    if (rmb && !m_lookActive)
    {
        m_lookActive = true;
        SDL_SetWindowRelativeMouseMode(m_window, true);
        SDL_RaiseWindow(m_window);
        m_mouseDX = m_mouseDY = 0.0f;
    }
    else if (!rmb && m_lookActive)
    {
        m_lookActive = false;
        SDL_SetWindowRelativeMouseMode(m_window, false);
        m_mouseDX = m_mouseDY = 0.0f;
    }

    if (!m_lookActive)
    {
        m_mouseDX = m_mouseDY = 0.0f;
        return;
    }

    m_camera.FPSLook(m_mouseDX, m_mouseDY);
    m_mouseDX = m_mouseDY = 0.0f;

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
    if (keys[SDL_SCANCODE_LCTRL]) m_camera.fpsPos.y -= speed * dt;
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
    for (auto& pass : m_ppPasses)
        pass->Resize(svW, svH);
}

void Application::UpdateLightBuffer()
{
    auto& sc     = VulkanSwapchain::Get();
    float aspect = static_cast<float>(sc.GetExtent().width) /
                   static_cast<float>(sc.GetExtent().height);

    m_hasDir             = false;
    m_hasSpotShadow      = false;
    m_hasPointShadow     = false;
    m_activeDirDir       = glm::normalize(glm::vec3(-1.0f, -1.0f, -0.4f));
    m_activeDirColor     = glm::vec3(1.0f, 0.9f, 0.7f);
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
                pl.shadowIdx     = 0;
                m_hasPointShadow = true;
                m_activePointPos = pl.position;
                m_activePointFar = l.range;
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
                sl.shadowIdx         = 0;
                m_hasSpotShadow      = true;
                m_activeSpotPos      = sl.position;
                m_activeSpotDir      = dir;
                m_activeSpotOuterCos = sl.outerCos;
                m_activeSpotFar      = l.range;
                m_activeSpotMatrix   = VulkanSpotShadowMap::ComputeMatrix(
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
    auto& objs = m_scene.GetObjects();
    objs.erase(std::remove_if(objs.begin(), objs.end(),
        [&](const SceneObject& o) {
            bool remove = o.entityId != 0 && !m_registry.Has<MeshRendererComponent>(o.entityId);
            if (remove) {
                m_lastMeshGuid        .erase(o.entityId);
                m_lastAnimMeshGuid    .erase(o.entityId);
                m_lastMaterialName    .erase(o.entityId);
                m_lastSubMeshMaterials.erase(o.entityId);
            }
            return remove;
        }), objs.end());

    m_registry.Each<MeshRendererComponent>([&](Entity e, MeshRendererComponent& mr)
    {
        std::string loadPath;
        if (mr.meshGuid != 0)
        {
            const auto* meta = Krayon::AssetManager::Get().FindByGuid(mr.meshGuid);
            if (meta) { loadPath = meta->path; mr.meshPath = loadPath; }
        }
        if (loadPath.empty()) loadPath = mr.meshPath;
        if (loadPath.empty()) return;

        SceneObject* existingObj = nullptr;
        for (auto& o : m_scene.GetObjects())
            if (o.entityId == e) { existingObj = &o; break; }

        const uint64_t trackGuid = mr.meshGuid ? mr.meshGuid
            : Krayon::AssetManager::Get().GetGuid(loadPath);
        auto it = m_lastMeshGuid.find(e);
        const bool meshChanged = (it == m_lastMeshGuid.end() || it->second != trackGuid);

        if (mr.materialGuid != 0)
        {
            Krayon::MaterialAsset matAsset;
            if (Krayon::AssetManager::Get().ReadMaterialAsset(mr.materialGuid, matAsset))
            {
                if (!matAsset.name.empty())
                {
                    const std::string matKey = "__mat_" + std::to_string(mr.materialGuid);
                    if (!MaterialManager::Get().Has(matKey))
                    {
                        MaterialLayout matLayout;
                        matLayout.AddVec4 ("albedo")
                                 .AddFloat("roughness")
                                 .AddFloat("metallic")
                                 .AddFloat("emissiveIntensity")
                                 .AddFloat("alphaThreshold");
                        MaterialManager::Get().Add(matKey, matLayout, m_pipeline);
                    }
                    Material& rMat = MaterialManager::Get().Get(matKey);
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

                    if (matAsset.albedoTexGuid != 0)
                    {
                        auto& lastTex = m_lastMatTexGuid[matKey];
                        if (lastTex != matAsset.albedoTexGuid)
                        {
                            const Krayon::AssetMeta* texMeta =
                                Krayon::AssetManager::Get().FindByGuid(matAsset.albedoTexGuid);
                            if (texMeta)
                            {
                                try
                                {
                                    VulkanTexture& tex = TextureManager::Get().Load(texMeta->path);
                                    rMat.BindTexture(0, tex);
                                    lastTex = matAsset.albedoTexGuid;
                                }
                                catch (const std::exception&) {}
                            }
                        }
                    }

                    mr.materialName = matKey;
                }
            }
        }

        
        for (size_t si = 0; si < mr.subMeshMaterialGuids.size(); si++)
        {
            uint64_t smGuid = mr.subMeshMaterialGuids[si];
            if (smGuid == 0) continue;

            const auto* smMeta = Krayon::AssetManager::Get().FindByGuid(smGuid);
            if (!smMeta) continue;

            std::string resolvedName;

            Krayon::MaterialAsset smAsset;
            if (Krayon::AssetManager::Get().ReadMaterialAsset(smGuid, smAsset) && !smAsset.name.empty())
            {
                
                resolvedName = "__mat_" + std::to_string(smGuid);
                if (!MaterialManager::Get().Has(resolvedName))
                {
                    MaterialLayout smLayout;
                    smLayout.AddVec4 ("albedo")
                            .AddFloat("roughness")
                            .AddFloat("metallic")
                            .AddFloat("emissiveIntensity")
                            .AddFloat("alphaThreshold");
                    MaterialManager::Get().Add(resolvedName, smLayout, m_pipeline);
                }
                Material& smMat = MaterialManager::Get().Get(resolvedName);
                smMat.SetVec4 ("albedo", glm::vec4(smAsset.albedo[0], smAsset.albedo[1],
                                                   smAsset.albedo[2], smAsset.albedo[3]));
                smMat.SetFloat("roughness",         smAsset.roughness);
                smMat.SetFloat("metallic",          smAsset.metallic);
                smMat.SetFloat("emissiveIntensity",
                    std::max({ smAsset.emissive[0], smAsset.emissive[1], smAsset.emissive[2] }));
                smMat.SetFloat("alphaThreshold", 0.0f);
                if (smAsset.albedoTexGuid != 0)
                {
                    auto& lastTex = m_lastMatTexGuid[resolvedName];
                    if (lastTex != smAsset.albedoTexGuid)
                    {
                        const Krayon::AssetMeta* tm =
                            Krayon::AssetManager::Get().FindByGuid(smAsset.albedoTexGuid);
                        if (tm)
                        {
                            try {
                                VulkanTexture& tex = TextureManager::Get().Load(tm->path);
                                smMat.BindTexture(0, tex);
                                lastTex = smAsset.albedoTexGuid;
                            } catch (const std::exception&) {}
                        }
                    }
                }
            }
            else if (smMeta->type == Krayon::AssetType::Texture)
            {
                
                
                
                resolvedName = "auto_" + smMeta->name;
                if (!MaterialManager::Get().Has(resolvedName))
                {
                    MaterialLayout smLayout;
                    smLayout.AddVec4 ("albedo")
                            .AddFloat("roughness")
                            .AddFloat("metallic")
                            .AddFloat("emissiveIntensity")
                            .AddFloat("alphaThreshold");
                    MaterialManager::Get().Add(resolvedName, smLayout, m_pipeline);
                    Material& smMat = MaterialManager::Get().Get(resolvedName);
                    smMat.SetVec4 ("albedo", glm::vec4(1.0f));
                    smMat.SetFloat("roughness",          0.5f);
                    smMat.SetFloat("metallic",           0.0f);
                    smMat.SetFloat("emissiveIntensity",  0.0f);
                    smMat.SetFloat("alphaThreshold",     0.0f);
                }
                Material& smMat = MaterialManager::Get().Get(resolvedName);
                auto& lastTex = m_lastMatTexGuid[resolvedName];
                if (lastTex != smGuid)
                {
                    try {
                        VulkanTexture& tex = TextureManager::Get().Load(smMeta->path);
                        smMat.BindTexture(0, tex);
                        lastTex = smGuid;
                    } catch (const std::exception&) {}
                }
            }
            else { continue; }  

            
            if (si < mr.subMeshMaterialNames.size())
                mr.subMeshMaterialNames[si] = resolvedName;
            else
            {
                mr.subMeshMaterialNames.resize(si + 1);
                mr.subMeshMaterialNames[si] = resolvedName;
            }
        }

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

        Material& mat = MaterialManager::Get().Get(mr.materialName);
        VulkanMesh* meshPtr = nullptr;
        try { meshPtr = &MeshManager::Get().Load(loadPath); }
        catch (const std::exception&) { return; }
        VulkanMesh& mesh = *meshPtr;
        m_lastMeshGuid[e]     = trackGuid;
        m_lastMaterialName[e] = mr.materialName;

        if (existingObj)
        {
            existingObj->mesh            = &mesh;
            existingObj->material        = &mat;
            existingObj->subMeshMaterials.clear();  
            m_lastSubMeshMaterials.erase(e);        
            // ...existing code...
            return;
        }

        SceneObject& obj = m_scene.Add(mesh, mat);
        obj.entityId = e;
        m_lastMaterialName[e] = mr.materialName;
        // ...existing code...

        if (m_registry.Has<TransformComponent>(e))
            obj.worldMatrix = TransformSystem::GetWorldMatrix(e, m_registry);
    });

    
    m_registry.Each<MeshRendererComponent>([&](Entity e, MeshRendererComponent& mr)
    {
        if (mr.subMeshMaterialNames.empty()) return;

        SceneObject* obj = nullptr;
        for (auto& o : m_scene.GetObjects())
            if (o.entityId == e) { obj = &o; break; }
        if (!obj || !obj->mesh) return;

        const auto& names = mr.subMeshMaterialNames;
        auto& last = m_lastSubMeshMaterials[e];
        if (last == names) return;
        last = names;

        uint32_t smCount = obj->mesh->GetSubmeshCount();
        obj->subMeshMaterials.resize(smCount, nullptr);
        for (uint32_t si = 0; si < smCount; si++)
        {
            const std::string& name = (si < (uint32_t)names.size()) ? names[si] : "";
            if (!name.empty() && MaterialManager::Get().Has(name))
                obj->subMeshMaterials[si] = &MaterialManager::Get().Get(name);
            else
                obj->subMeshMaterials[si] = nullptr;
        }
    });

    for (auto& obj : m_scene.GetObjects())
    {
        if (obj.entityId == 0 || !m_registry.Has<TransformComponent>(obj.entityId)) continue;

        obj.isSkinned   = m_registry.Has<AnimationComponent>(obj.entityId);
        obj.worldMatrix = TransformSystem::GetWorldMatrix(obj.entityId, m_registry);
    }

    for (auto& obj : m_scene.GetObjects())
    {
        if (!obj.material) continue;
        float threshold = (obj.material->alphaMode == Material::AlphaMode::AlphaTest)
                          ? obj.material->alphaCutoff : 0.0f;
        obj.material->SetFloat("alphaThreshold", threshold);
    }

    if (m_playingInline)
    {
        m_registry.Each<CameraComponent>([&](Entity e, CameraComponent& cam)
        {
            if (!cam.isActive) return;
            if (!m_registry.Has<TransformComponent>(e)) return;
            glm::mat4 world = TransformSystem::GetWorldMatrix(e, m_registry);
            glm::vec3 pos   = glm::vec3(world[3]);
            float sz        = glm::length(glm::vec3(world[2]));
            glm::vec3 fwd   = (sz > 0.f) ? (-glm::vec3(world[2]) / sz) : glm::vec3(0, 0, -1);
            m_camera.fpsMode   = true;
            m_camera.fpsPos    = pos;
            m_camera.fov       = cam.fov;
            m_camera.nearPlane = cam.nearPlane;
            m_camera.farPlane  = cam.farPlane;
            m_camera.SetOrientation(std::atan2(-fwd.x, -fwd.z),
                                    std::asin(glm::clamp(fwd.y, -1.0f, 1.0f)));
        });
    }
}

void Application::UpdateAnimations(float dt)
{
    // Remove SSBOs for entities that no longer have an AnimationComponent.
    // waitIdle ensures the GPU is done with the buffer before freeing it.
    {
        std::vector<uint32_t> toErase;
        for (auto& [eid, _] : m_entityBoneSSBO)
            if (!m_registry.Has<AnimationComponent>(eid))
                toErase.push_back(eid);
        if (!toErase.empty())
        {
            VulkanInstance::Get().GetDevice().waitIdle();
            for (auto eid : toErase)
            {
                m_entityBoneSSBO[eid]->Destroy();
                m_entityBoneSSBO.erase(eid);
            }
        }
    }

    static const std::vector<glm::mat4> s_identity(MAX_BONES, glm::mat4(1.0f));

    m_registry.Each<AnimationComponent>([&](Entity e, AnimationComponent& anim)
    {
        // Reset per-entity bone buffer to identity before writing this entity's bones
        std::vector<glm::mat4> boneMatrices(s_identity);
        if (!m_registry.Has<MeshRendererComponent>(e)) return;
        const auto& mr = m_registry.Get<MeshRendererComponent>(e);

        std::string loadPath;
        if (mr.meshGuid != 0)
        {
            const auto* meta = Krayon::AssetManager::Get().FindByGuid(mr.meshGuid);
            if (meta) loadPath = meta->path;
        }
        if (loadPath.empty()) loadPath = mr.meshPath;
        if (loadPath.empty() || !MeshManager::Get().Has(loadPath)) return;

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

        const float step           = dt * anim.speed;
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

        m_entityBoneMatrices[e] = boneMatrices;

        // Create a per-entity BoneSSBO on first use and upload this frame's matrices
        auto& ssbo = m_entityBoneSSBO[e];
        if (!ssbo)
        {
            ssbo = std::make_unique<BoneSSBO>();
            ssbo->Create(m_pipeline);
        }
        ssbo->Upload(boneMatrices);
    });
}

void Application::RenderShadowPasses(vk::CommandBuffer cmd)
{
    for (uint32_t c = 0; c < VulkanShadowMap::NUM_CASCADES; ++c)
    {
        m_shadowMap.BeginRendering(cmd, c);
        m_shadowPipeline.Bind(cmd);
        m_boneSSBO.BindToSet(cmd, *m_shadowPipeline.GetLayout(), 0);
        m_scene.DrawShadow(cmd, m_shadowPipeline, m_cascadeMatrices[c]);
        m_shadowMap.EndRendering(cmd);
    }
    m_shadowMap.TransitionToShaderRead(cmd);

    if (m_hasSpotShadow)
    {
        m_spotShadowMap.BeginRendering(cmd, 0);
        m_shadowPipeline.Bind(cmd);
        m_boneSSBO.BindToSet(cmd, *m_shadowPipeline.GetLayout(), 0);
        m_scene.DrawShadow(cmd, m_shadowPipeline, m_activeSpotMatrix);
        m_spotShadowMap.EndRendering(cmd);
    }
    m_spotShadowMap.TransitionToShaderRead(cmd);

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

    m_pipeline.Bind(cmd);
    m_boneSSBO.Bind(cmd, *m_pipeline.GetLayout());
    m_scene.Draw(cmd, m_pipeline, m_camera, aspect, m_lights);

    m_pipeline.Bind(cmd);
    m_scene.DrawSkinned(cmd, m_pipeline, m_camera, aspect, m_lights,
        [this, &cmd](uint32_t entityId)
        {
            auto it = m_entityBoneSSBO.find(entityId);
            if (it != m_entityBoneSSBO.end() && it->second)
                it->second->Bind(cmd, *m_pipeline.GetLayout());
            else
                m_boneSSBO.Bind(cmd, *m_pipeline.GetLayout()); // fallback: identity
        });

    m_skybox.Draw(cmd, m_camera.GetView(), m_camera.GetProjection(aspect), -m_activeDirDir);

    m_pipeline.BindTransparent(cmd);
    m_scene.DrawTransparent(cmd, m_pipeline, m_camera, aspect, m_lights);

    glm::mat4 vp = m_camera.GetProjection(aspect) * m_camera.GetView();

    Entity selected = m_editor.GetSelected();

    m_gizmo.Draw(cmd, m_registry, vp, selected);

    m_uiRenderer.Draw(cmd, m_debugFb.GetExtent().width, m_debugFb.GetExtent().height);

    m_debugFb.EndRendering(cmd);
    m_debugFb.TransitionToShaderRead(cmd);
}

void Application::RenderPostProcess(vk::CommandBuffer cmd)
{
    const float aspect = SceneViewAspect();
    m_postFb = &m_debugFb;

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
                : glm::vec2(-10.0f);

            float sunHeight = glm::normalize(-m_activeDirDir).y;

            m_occlusionPass.Draw(cmd,
                m_debugFb.GetDepthView(), m_debugFb.GetSampler(),
                sunUV, shaftsComp->shaftsSunRadius, aspect);

            m_sunShafts.Draw(cmd,
                m_postFb->GetColorView(),  m_postFb->GetSampler(),
                m_occlusionPass.GetView(), m_debugFb.GetDepthView(),
                sunUV,
                shaftsComp->shaftsDensity,    shaftsComp->shaftsBloomScale,
                shaftsComp->shaftsDecay,      shaftsComp->shaftsWeight,
                shaftsComp->shaftsExposure,   shaftsComp->shaftsTint,
                sunHeight, aspect);

            m_sunShafts.GetOutput().TransitionToShaderRead(cmd);
            m_postFb = &m_sunShafts.GetOutput();
        }
    }

    
    if (m_ppDirty) RebuildPostProcessPasses();

    for (size_t i = 0; i < m_ppPasses.size(); ++i)
    {
        if (!m_ppGraph.effects[i].enabled) continue;
        if (!m_ppPasses[i]) continue;

        m_ppPasses[i]->Draw(cmd,
            m_postFb->GetColorView(),    m_postFb->GetSampler(),
            m_debugFb.GetDepthView(),    m_debugFb.GetSampler(),
            m_ppGraph.effects[i].params);
        m_ppPasses[i]->GetOutput().TransitionToShaderRead(cmd);
        m_postFb = &m_ppPasses[i]->GetOutput();
    }

    m_editor.SetSceneViewFramebuffer(m_postFb);
}

void Application::RenderMainPass(vk::CommandBuffer cmd)
{
    VulkanRenderer::Get().BeginMainPass();
    m_debugOverlay.Draw(cmd, m_debugFb, m_shadowMap);
}

float Application::SceneViewAspect() const
{
    return static_cast<float>(m_debugFb.GetExtent().width) /
           static_cast<float>(m_debugFb.GetExtent().height);
}
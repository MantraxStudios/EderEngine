#include "Editor.h"
#include <imgui/imgui.h>
#include <imgui/imgui_impl_sdl3.h>
#include <imgui/imgui_impl_vulkan.h>
#include "Renderer/Vulkan/VulkanInstance.h"
#include "Renderer/Vulkan/VulkanSwapchain.h"

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

void Editor::Init(SDL_Window* window)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui::StyleColorsDark();
    ImGui_ImplSDL3_InitForVulkan(window);

    auto& vi = VulkanInstance::Get();
    auto& sc = VulkanSwapchain::Get();
    VkFormat colorFmt = (VkFormat)sc.GetFormat();

    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion          = VK_API_VERSION_1_3;
    info.Instance            = *vi.GetInstance();
    info.PhysicalDevice      = *vi.GetPhysicalDevice();
    info.Device              = *vi.GetDevice();
    info.QueueFamily         = vi.GetGraphicsIndex();
    info.Queue               = *vi.GetGraphicsQueue();
    info.DescriptorPoolSize  = 100;
    info.MinImageCount       = 2;
    info.ImageCount          = static_cast<uint32_t>(sc.GetImages().size());
    info.UseDynamicRendering = true;
    info.PipelineInfoMain.PipelineRenderingCreateInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
    info.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount    = 1;
    info.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &colorFmt;
    ImGui_ImplVulkan_Init(&info);
}

void Editor::Shutdown()
{
    sceneView.ReleaseTexture();
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
}

// ─────────────────────────────────────────────────────────────────────────────
// Frame
// ─────────────────────────────────────────────────────────────────────────────

void Editor::ProcessEvent(const SDL_Event& event)
{
    ImGui_ImplSDL3_ProcessEvent(&event);
}

void Editor::BeginFrame()
{
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

void Editor::EndFrame()
{
    ImGui::EndFrame();
}

void Editor::Draw(Camera& cam, Scene& scene, float dt)
{
    stats    .Update(dt);
    camera   .SetCamera(&cam);
    hierarchy.SetScene(&scene);
    inspector.SetScene(&scene);
    inspector.SetSelected(hierarchy.GetSelected());

    DrawMenuBar();
    DrawDockspace();

    if (stats    .open) stats    .OnDraw();
    if (camera   .open) camera   .OnDraw();
    if (hierarchy.open) hierarchy.OnDraw();
    if (inspector.open) inspector.OnDraw();
    if (sceneView.open) sceneView.OnDraw();
    if (showDemo)       ImGui::ShowDemoWindow(&showDemo);
}

void Editor::Render(VkCommandBuffer cmd)
{
    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
}

// ─────────────────────────────────────────────────────────────────────────────
// SceneView helpers
// ─────────────────────────────────────────────────────────────────────────────

void Editor::SetSceneViewFramebuffer(VulkanFramebuffer* fb)
{
    sceneView.SetFramebuffer(fb);
}

void Editor::ReleaseSceneViewFramebuffer()
{
    sceneView.ReleaseTexture();
}

void Editor::GetSceneViewSize(uint32_t& w, uint32_t& h) const
{
    ImVec2 s = sceneView.GetDesiredSize();
    w = static_cast<uint32_t>(s.x);
    h = static_cast<uint32_t>(s.y);
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

bool Editor::WantCaptureMouse()    const { return ImGui::GetIO().WantCaptureMouse; }
bool Editor::WantCaptureKeyboard() const { return ImGui::GetIO().WantCaptureKeyboard; }

void Editor::DrawMenuBar()
{
    if (!ImGui::BeginMainMenuBar()) return;

    if (ImGui::BeginMenu("View"))
    {
        ImGui::MenuItem("Stats",       nullptr, &stats    .open);
        ImGui::MenuItem("Camera",      nullptr, &camera   .open);
        ImGui::MenuItem("Hierarchy",   nullptr, &hierarchy.open);
        ImGui::MenuItem("Inspector",   nullptr, &inspector.open);
        ImGui::MenuItem("Scene View",  nullptr, &sceneView.open);
        ImGui::Separator();
        ImGui::MenuItem("ImGui Demo",  nullptr, &showDemo);
        ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
}

void Editor::DrawDockspace()
{
    ImGuiViewport* vp = ImGui::GetMainViewport();

    // Dejar espacio para la barra de menú
    float menuH = ImGui::GetFrameHeight();
    ImGui::SetNextWindowPos (ImVec2(vp->Pos.x,  vp->Pos.y  + menuH));
    ImGui::SetNextWindowSize(ImVec2(vp->Size.x, vp->Size.y - menuH));
    ImGui::SetNextWindowViewport(vp->ID);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDocking          |
        ImGuiWindowFlags_NoTitleBar         |
        ImGuiWindowFlags_NoCollapse         |
        ImGuiWindowFlags_NoResize           |
        ImGuiWindowFlags_NoMove             |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus         |
        ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,  0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("##Dockspace", nullptr, flags);
    ImGui::PopStyleVar(3);

    // PassthruCentralNode: el área central sin panel muestra el render de Vulkan
    ImGui::DockSpace(ImGui::GetID("MainDock"), ImVec2(0, 0),
                     ImGuiDockNodeFlags_PassthruCentralNode);

    ImGui::End();
}

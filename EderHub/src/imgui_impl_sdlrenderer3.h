#pragma once
// ImGui SDL_Renderer3 backend (SDL3 version)
// Compatible with SDL3 + ImGui

#include <imgui/imgui.h>
#include <SDL3/SDL.h>

IMGUI_IMPL_API bool ImGui_ImplSDLRenderer3_Init(SDL_Renderer* renderer);
IMGUI_IMPL_API void ImGui_ImplSDLRenderer3_Shutdown();
IMGUI_IMPL_API void ImGui_ImplSDLRenderer3_NewFrame();
IMGUI_IMPL_API void ImGui_ImplSDLRenderer3_RenderDrawData(ImDrawData* draw_data, SDL_Renderer* renderer);
IMGUI_IMPL_API bool ImGui_ImplSDLRenderer3_CreateFontsTexture();
IMGUI_IMPL_API void ImGui_ImplSDLRenderer3_DestroyFontsTexture();

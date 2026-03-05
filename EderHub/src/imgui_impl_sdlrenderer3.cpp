// ImGui SDL_Renderer3 backend — minimal but complete implementation for EderHub.
// Uses SDL_RenderGeometry (SDL3) to render ImGui draw data.

#include "imgui_impl_sdlrenderer3.h"
#include <vector>

// ── Backend data ──────────────────────────────────────────────────────────────
struct ImGui_ImplSDLRenderer3_Data
{
    SDL_Renderer* Renderer    = nullptr;
    SDL_Texture*  FontTexture = nullptr;
};

static ImGui_ImplSDLRenderer3_Data* GetBD()
{
    return ImGui::GetCurrentContext()
        ? static_cast<ImGui_ImplSDLRenderer3_Data*>(ImGui::GetIO().BackendRendererUserData)
        : nullptr;
}

// ── Init / Shutdown ───────────────────────────────────────────────────────────
bool ImGui_ImplSDLRenderer3_Init(SDL_Renderer* renderer)
{
    ImGuiIO& io = ImGui::GetIO();
    IM_ASSERT(io.BackendRendererUserData == nullptr && "Already initialized");

    auto* bd       = IM_NEW(ImGui_ImplSDLRenderer3_Data)();
    bd->Renderer   = renderer;
    io.BackendRendererUserData = bd;
    io.BackendRendererName     = "imgui_impl_sdlrenderer3";
    io.BackendFlags           |= ImGuiBackendFlags_RendererHasVtxOffset;
    return true;
}

void ImGui_ImplSDLRenderer3_Shutdown()
{
    ImGui_ImplSDLRenderer3_DestroyFontsTexture();
    ImGuiIO& io = ImGui::GetIO();
    IM_DELETE(GetBD());
    io.BackendRendererUserData = nullptr;
    io.BackendRendererName     = nullptr;
}

void ImGui_ImplSDLRenderer3_NewFrame()
{
    if (!GetBD()->FontTexture)
        ImGui_ImplSDLRenderer3_CreateFontsTexture();
}

// ── Font texture ──────────────────────────────────────────────────────────────
bool ImGui_ImplSDLRenderer3_CreateFontsTexture()
{
    ImGuiIO& io = ImGui::GetIO();
    auto*    bd = GetBD();

    unsigned char* pixels = nullptr;
    int w = 0, h = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);

    // SDL_PIXELFORMAT_ABGR8888 maps to R,G,B,A in memory on little-endian (Windows)
    // which matches what GetTexDataAsRGBA32 returns.
    bd->FontTexture = SDL_CreateTexture(bd->Renderer,
        SDL_PIXELFORMAT_ABGR8888,
        SDL_TEXTUREACCESS_STATIC, w, h);
    if (!bd->FontTexture)
        return false;

    SDL_UpdateTexture(bd->FontTexture, nullptr, pixels, w * 4);
    SDL_SetTextureBlendMode(bd->FontTexture, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(bd->FontTexture, SDL_SCALEMODE_LINEAR);

    io.Fonts->SetTexID(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(bd->FontTexture)));
    return true;
}

void ImGui_ImplSDLRenderer3_DestroyFontsTexture()
{
    ImGuiIO& io = ImGui::GetIO();
    auto*    bd = GetBD();
    if (bd && bd->FontTexture)
    {
        SDL_DestroyTexture(bd->FontTexture);
        bd->FontTexture = nullptr;
        io.Fonts->SetTexID(0);
    }
}

// ── Render ────────────────────────────────────────────────────────────────────
void ImGui_ImplSDLRenderer3_RenderDrawData(ImDrawData* draw_data, SDL_Renderer* renderer)
{
    const float fb_w = draw_data->DisplaySize.x * draw_data->FramebufferScale.x;
    const float fb_h = draw_data->DisplaySize.y * draw_data->FramebufferScale.y;
    if (fb_w <= 0.f || fb_h <= 0.f) return;

    // Save state
    SDL_Rect old_viewport, old_clip;
    bool     old_clip_en = SDL_RenderClipEnabled(renderer);
    SDL_GetRenderViewport(renderer, &old_viewport);
    SDL_GetRenderClipRect(renderer, &old_clip);

    // Reusable per-frame buffers
    static std::vector<SDL_Vertex> s_verts;
    static std::vector<int>        s_indices;

    for (int n = 0; n < draw_data->CmdListsCount; ++n)
    {
        const ImDrawList* cl = draw_data->CmdLists[n];

        // Convert ImDrawVert → SDL_Vertex
        const int vtx_count = cl->VtxBuffer.Size;
        s_verts.resize(vtx_count);
        for (int i = 0; i < vtx_count; ++i)
        {
            const ImDrawVert& src = cl->VtxBuffer[i];
            s_verts[i].position  = { src.pos.x, src.pos.y };
            s_verts[i].tex_coord = { src.uv.x,  src.uv.y  };
            s_verts[i].color = {
                static_cast<float>((src.col >>  0) & 0xFF) / 255.f,
                static_cast<float>((src.col >>  8) & 0xFF) / 255.f,
                static_cast<float>((src.col >> 16) & 0xFF) / 255.f,
                static_cast<float>((src.col >> 24) & 0xFF) / 255.f,
            };
        }

        // Convert ImDrawIdx (uint16) → int
        const int idx_count = cl->IdxBuffer.Size;
        s_indices.resize(idx_count);
        for (int i = 0; i < idx_count; ++i)
            s_indices[i] = static_cast<int>(cl->IdxBuffer[i]);

        for (const ImDrawCmd& cmd : cl->CmdBuffer)
        {
            if (cmd.UserCallback)
            {
                if (cmd.UserCallback == ImDrawCallback_ResetRenderState)
                    SDL_SetRenderViewport(renderer, nullptr);
                else
                    cmd.UserCallback(cl, &cmd);
                continue;
            }

            // Clip rect in framebuffer space
            const ImVec2 off   = draw_data->DisplayPos;
            const ImVec2 scale = draw_data->FramebufferScale;
            float cx0 = (cmd.ClipRect.x - off.x) * scale.x;
            float cy0 = (cmd.ClipRect.y - off.y) * scale.y;
            float cx1 = (cmd.ClipRect.z - off.x) * scale.x;
            float cy1 = (cmd.ClipRect.w - off.y) * scale.y;
            if (cx0 < 0.f) cx0 = 0.f;
            if (cy0 < 0.f) cy0 = 0.f;
            SDL_Rect clip = {
                static_cast<int>(cx0), static_cast<int>(cy0),
                static_cast<int>(cx1 - cx0), static_cast<int>(cy1 - cy0)
            };
            SDL_SetRenderClipRect(renderer, &clip);

            SDL_Texture* tex = reinterpret_cast<SDL_Texture*>(
                static_cast<intptr_t>(cmd.GetTexID()));

            SDL_RenderGeometry(
                renderer, tex,
                s_verts.data()   + cmd.VtxOffset, vtx_count - static_cast<int>(cmd.VtxOffset),
                s_indices.data() + cmd.IdxOffset,  static_cast<int>(cmd.ElemCount));
        }
    }

    // Restore state
    SDL_SetRenderViewport(renderer, &old_viewport);
    SDL_SetRenderClipRect(renderer, old_clip_en ? &old_clip : nullptr);
}

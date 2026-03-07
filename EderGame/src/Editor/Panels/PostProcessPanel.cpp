#include "PostProcessPanel.h"
#include <imgui/imgui.h>
#include <IO/AssetManager.h>
#include <filesystem>
#include <algorithm>
#include <cstring>

// Shared helper: draws a shader drop slot (same visual style as InspectorPanel::AssetDropField)
static bool ShaderDropField(const char* label, std::string& shaderPath)
{
    using namespace Krayon;
    bool changed = false;

    std::string stem = shaderPath.empty() ? "(none)"
        : std::filesystem::path(shaderPath).stem().string();

    ImGui::PushID(label);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", label);
    ImGui::SameLine(110);

    bool canDrop = false;
    if (ImGui::GetDragDropPayload() &&
        ImGui::GetDragDropPayload()->IsDataType("ASSET_GUID"))
    {
        uint64_t g = *reinterpret_cast<const uint64_t*>(ImGui::GetDragDropPayload()->Data);
        const AssetMeta* m = AssetManager::Get().FindByGuid(g);
        canDrop = m && (m->type == AssetType::Shader || m->type == AssetType::Unknown);
    }

    ImVec4 bgCol = canDrop
        ? ImVec4(0.20f, 0.50f, 0.20f, 0.85f)
        : ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
    ImGui::PushStyleColor(ImGuiCol_Button,        bgCol);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.20f, 0.25f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  bgCol);

    std::string btnLabel = stem;
    if (btnLabel.size() > 22) btnLabel = btnLabel.substr(0, 19) + "...";
    btnLabel += "  \xce\xb2";
    ImGui::Button(btnLabel.c_str(), ImVec2(ImGui::GetContentRegionAvail().x, 0));
    ImGui::PopStyleColor(3);

    if (!shaderPath.empty() && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", shaderPath.c_str());

    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ASSET_GUID"))
        {
            uint64_t g = *reinterpret_cast<const uint64_t*>(p->Data);
            const AssetMeta* m = AssetManager::Get().FindByGuid(g);
            if (m && (m->type == AssetType::Shader || m->type == AssetType::Unknown))
            {
                shaderPath = m->path;
                changed = true;
            }
        }
        ImGui::EndDragDropTarget();
    }
    ImGui::PopID();
    return changed;
}

void PostProcessPanel::OnDraw()
{
    if (!ImGui::Begin(Title(), &open))
    {
        ImGui::End();
        return;
    }

    if (!m_graph)
    {
        ImGui::TextDisabled("No PostProcessGraph bound.");
        ImGui::End();
        return;
    }

    auto& effects = m_graph->effects;

    // ── Toolbar ───────────────────────────────────────────────────────────────
    if (ImGui::Button("+ Add Effect"))
    {
        m_addName[0]   = '\0';
        m_addShader[0] = '\0';
        std::strncpy(m_addName,   "New Effect",                    sizeof(m_addName)   - 1);
        std::strncpy(m_addShader, "shaders/passthrough.frag.spv",  sizeof(m_addShader) - 1);
        ImGui::OpenPopup("##AddEffect");
    }

    if (m_selected >= 0 && m_selected < (int)effects.size())
    {
        ImGui::SameLine();
        if (ImGui::Button("Remove"))
        {
            effects.erase(effects.begin() + m_selected);
            m_selected = std::min(m_selected, (int)effects.size() - 1);
            if (m_onChanged) m_onChanged();
        }
        ImGui::SameLine();
        if (ImGui::ArrowButton("##Up", ImGuiDir_Up) && m_selected > 0)
        {
            std::swap(effects[m_selected], effects[m_selected - 1]);
            --m_selected;
            if (m_onChanged) m_onChanged();
        }
        ImGui::SameLine();
        if (ImGui::ArrowButton("##Down", ImGuiDir_Down) && m_selected < (int)effects.size() - 1)
        {
            std::swap(effects[m_selected], effects[m_selected + 1]);
            ++m_selected;
            if (m_onChanged) m_onChanged();
        }
    }

    ImGui::Separator();

    // ── Effect list ───────────────────────────────────────────────────────────
    ImGui::BeginChild("##EffectList", ImVec2(0, 160), true);
    for (int i = 0; i < (int)effects.size(); ++i)
    {
        auto& fx = effects[i];
        bool selected = (i == m_selected);

        // Enabled checkbox
        bool en = fx.enabled;
        ImGui::PushID(i);
        if (ImGui::Checkbox("##en", &en))
        {
            fx.enabled = en;
            if (m_onChanged) m_onChanged();
        }
        ImGui::SameLine();

        char label[160];
        snprintf(label, sizeof(label), "%d. %s", i + 1, fx.name.c_str());
        if (ImGui::Selectable(label, selected, ImGuiSelectableFlags_AllowDoubleClick))
            m_selected = i;

        ImGui::PopID();
    }
    ImGui::EndChild();

    // ── Selected effect inspector ─────────────────────────────────────────────
    if (m_selected >= 0 && m_selected < (int)effects.size())
    {
        ImGui::Separator();
        auto& fx = effects[m_selected];

        ImGui::PushItemWidth(-1);

        // Name
        char nameBuf[128];
        std::strncpy(nameBuf, fx.name.c_str(), sizeof(nameBuf) - 1);
        nameBuf[sizeof(nameBuf) - 1] = '\0';
        ImGui::TextDisabled("Name");
        if (ImGui::InputText("##name", nameBuf, sizeof(nameBuf)))
        {
            fx.name = nameBuf;
            if (m_onChanged) m_onChanged();
        }

        // Shader path — drag a .spv from the Asset Browser or type manually
        ImGui::PopItemWidth();
        if (ShaderDropField("Shader (.spv)", fx.fragShaderPath))
            if (m_onChanged) m_onChanged();

        // Manual text fallback
        char shaderBuf[512];
        std::strncpy(shaderBuf, fx.fragShaderPath.c_str(), sizeof(shaderBuf) - 1);
        shaderBuf[sizeof(shaderBuf) - 1] = '\0';
        ImGui::SetNextItemWidth(-80);
        if (ImGui::InputText("##shaderText", shaderBuf, sizeof(shaderBuf)))
            fx.fragShaderPath = shaderBuf;
        ImGui::SameLine();
        if (ImGui::Button("Reload"))
            if (m_onChanged) m_onChanged();

        ImGui::PushItemWidth(-1);

        // Param count
        int pc = fx.paramCount;
        ImGui::TextDisabled("Param count (0–16)");
        if (ImGui::SliderInt("##pc", &pc, 0, 16))
        {
            fx.paramCount = pc;
            if (m_onChanged) m_onChanged();
        }

        // Param sliders
        if (fx.paramCount > 0)
        {
            ImGui::TextDisabled("Parameters");
            for (int p = 0; p < fx.paramCount; ++p)
            {
                char pid[16];
                snprintf(pid, sizeof(pid), "p%d", p);
                if (ImGui::DragFloat(pid, &fx.params[p], 0.01f))
                    if (m_onChanged) m_onChanged();
            }
        }

        ImGui::PopItemWidth();
    }

    // ── Add effect popup ───────────────────────────────────────────────────────
    if (ImGui::BeginPopup("##AddEffect"))
    {
        ImGui::TextDisabled("New effect");
        ImGui::Separator();
        ImGui::PushItemWidth(300);
        ImGui::InputText("Name##an",          m_addName,   sizeof(m_addName));
        ImGui::InputText("Shader (.spv)##as", m_addShader, sizeof(m_addShader));
        ImGui::PopItemWidth();

        if (ImGui::Button("Add"))
        {
            Krayon::PostProcessEffect fx{};
            fx.name           = m_addName;
            fx.fragShaderPath = m_addShader;
            fx.enabled        = true;
            fx.paramCount     = 0;
            effects.push_back(fx);
            m_selected = static_cast<int>(effects.size()) - 1;
            if (m_onChanged) m_onChanged();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }

    ImGui::End();
}

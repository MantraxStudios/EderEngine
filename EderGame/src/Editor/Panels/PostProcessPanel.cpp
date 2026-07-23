#include "PostProcessPanel.h"
#include <imgui/imgui.h>
#include <IO/AssetManager.h>
#include <filesystem>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <map>

namespace fs = std::filesystem;

// ─────────────────────────────────────────────────────────────────────────────
//  Built-in effect registry
//  Maps a shader to a display name + labelled parameters (label, min, max,
//  default, optional enum labels). This is what makes the panel feel like
//  Unity: named effects with meaningful sliders instead of raw p0..pN.
// ─────────────────────────────────────────────────────────────────────────────
struct PPParamDesc
{
    const char* label;
    float       minV, maxV, defV;
    const char* enumLabels = nullptr;  // "Off\0Reinhard\0ACES\0" => combo, value = index
};

struct PPEffectDesc
{
    const char*              name;      // display name in the Add menu
    const char*              shader;    // content-relative .spv path
    const char*              desc;      // tooltip
    std::vector<PPParamDesc> params;
};

static const std::vector<PPEffectDesc>& Builtins()
{
    static const std::vector<PPEffectDesc> table = {
        { "Color Grading", "shaders/colorgrade.frag.spv",
          "Exposure, tonemapping, contrast, saturation and white balance.",
          {
            { "Exposure",    0.0f, 4.0f,  1.0f },
            { "Contrast",    0.0f, 2.0f,  1.0f },
            { "Saturation",  0.0f, 2.0f,  1.0f },
            { "Temperature",-1.0f, 1.0f,  0.0f },
            { "Tint",       -1.0f, 1.0f,  0.0f },
            { "Tonemap",     0.0f, 2.0f,  2.0f, "Off\0Reinhard\0ACES\0" },
            { "Gamma",       0.5f, 2.5f,  1.0f },
            { "Brightness", -0.5f, 0.5f,  0.0f },
          } },
        { "Bloom", "shaders/bloom.frag.spv",
          "Single-pass approximate glow on bright pixels.",
          {
            { "Threshold", 0.0f, 2.0f, 0.8f },
            { "Intensity", 0.0f, 3.0f, 0.6f },
            { "Radius",    1.0f, 8.0f, 3.0f },
          } },
        { "Depth of Field", "shaders/dof.frag.spv",
          "Single-pass depth blur. Focus is in depth-buffer space (0..1).",
          {
            { "Focus Depth", 0.0f,  1.0f,  0.5f  },
            { "Focus Range", 0.001f,0.3f,  0.02f },
            { "Blur",        0.0f,  8.0f,  3.0f  },
          } },
        { "Vignette", "shaders/vignette.frag.spv",
          "Darkens the screen edges, with optional desaturation.",
          {
            { "Strength",   0.0f, 1.0f, 0.5f },
            { "Softness",   0.1f, 0.8f, 0.3f },
            { "Desaturate", 0.0f, 1.0f, 0.0f },
          } },
        { "Chromatic Aberration", "shaders/chromatic.frag.spv",
          "Radial RGB fringe that grows toward the edges.",
          {
            { "Amount",      0.0f, 0.02f, 0.004f },
            { "Edge Falloff",0.0f, 3.0f,  1.5f   },
          } },
        { "Film Grain", "shaders/grain.frag.spv",
          "Static hashed noise overlay (no time uniform available).",
          {
            { "Intensity", 0.0f, 0.5f, 0.08f },
            { "Size",      0.5f, 4.0f, 1.0f  },
          } },
        { "Sharpen", "shaders/sharpen.frag.spv",
          "Unsharp-mask edge sharpening.",
          {
            { "Amount", 0.0f, 2.0f, 0.4f },
          } },
        { "Scanlines", "shaders/scanlines.frag.spv",
          "CRT-style horizontal scanlines.",
          {
            { "Count",     100.0f, 1080.0f, 400.0f },
            { "Intensity", 0.0f,   0.6f,    0.15f  },
          } },
    };
    return table;
}

// Match an effect's shader path to a built-in by filename (robust to
// workdir-relative vs. absolute paths).
static const PPEffectDesc* FindBuiltin(const std::string& shaderPath)
{
    if (shaderPath.empty()) return nullptr;
    std::string file = fs::path(shaderPath).filename().string();
    for (const auto& e : Builtins())
        if (fs::path(e.shader).filename().string() == file)
            return &e;
    return nullptr;
}

static Krayon::PostProcessEffect MakeEffect(const PPEffectDesc& d)
{
    Krayon::PostProcessEffect fx{};
    fx.name           = d.name;
    fx.fragShaderPath = d.shader;
    fx.enabled        = true;
    fx.paramCount     = static_cast<int>(d.params.size());
    for (size_t i = 0; i < d.params.size() && i < 16; ++i)
        fx.params[i] = d.params[i].defV;
    return fx;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Preset (.pp) serialization — same key/value style as the [postprocess]
//  block in .scene files, but stand-alone so presets are reusable across scenes.
// ─────────────────────────────────────────────────────────────────────────────
static std::string PresetsDir()
{
    return Krayon::AssetManager::Get().GetWorkDir() + "/postprocess";
}

// Shared shader drop slot (custom effects only)
static bool ShaderDropField(const char* label, std::string& shaderPath)
{
    using namespace Krayon;
    bool changed = false;
    std::string stem = shaderPath.empty() ? "(none)"
        : fs::path(shaderPath).stem().string();

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

    ImVec4 bgCol = canDrop ? ImVec4(0.20f, 0.50f, 0.20f, 0.85f)
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

// ─────────────────────────────────────────────────────────────────────────────
//  Presets
// ─────────────────────────────────────────────────────────────────────────────
void PostProcessPanel::RefreshPresets()
{
    m_presetFiles.clear();
    std::error_code ec;
    fs::path dir = PresetsDir();
    if (fs::exists(dir, ec))
    {
        for (const auto& e : fs::directory_iterator(dir, ec))
            if (e.is_regular_file() && e.path().extension() == ".pp")
                m_presetFiles.push_back(e.path().string());
        std::sort(m_presetFiles.begin(), m_presetFiles.end());
    }
    m_presetsScanned = true;
}

bool PostProcessPanel::SavePreset(const std::string& name)
{
    if (!m_graph || name.empty()) return false;
    std::error_code ec;
    fs::create_directories(PresetsDir(), ec);

    fs::path file = fs::path(PresetsDir()) / (name + ".pp");
    std::ofstream f(file);
    if (!f) return false;

    f << "# EderEngine post-process preset\n";
    f << "effect.count=" << m_graph->effects.size() << "\n";
    for (size_t i = 0; i < m_graph->effects.size(); ++i)
    {
        const auto& fx = m_graph->effects[i];
        f << "effect." << i << ".name="       << fx.name          << "\n";
        f << "effect." << i << ".shader="     << fx.fragShaderPath << "\n";
        f << "effect." << i << ".enabled="    << (fx.enabled ? 1 : 0) << "\n";
        f << "effect." << i << ".paramcount=" << fx.paramCount    << "\n";
        for (int p = 0; p < fx.paramCount && p < 16; ++p)
            f << "effect." << i << ".param." << p << "=" << fx.params[p] << "\n";
    }
    RefreshPresets();
    return true;
}

bool PostProcessPanel::LoadPreset(const std::string& absPath)
{
    if (!m_graph) return false;
    std::ifstream f(absPath);
    if (!f) return false;

    std::map<std::string, std::string> kv;
    std::string line;
    while (std::getline(f, line))
    {
        if (line.empty() || line[0] == '#') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        kv[line.substr(0, eq)] = line.substr(eq + 1);
    }
    if (!kv.count("effect.count")) return false;

    int count = std::stoi(kv["effect.count"]);
    m_graph->effects.clear();
    m_graph->effects.resize(std::max(count, 0));
    for (int i = 0; i < count; ++i)
    {
        std::string pfx = "effect." + std::to_string(i) + ".";
        auto& fx = m_graph->effects[i];
        if (kv.count(pfx + "name"))       fx.name           = kv[pfx + "name"];
        if (kv.count(pfx + "shader"))     fx.fragShaderPath = kv[pfx + "shader"];
        if (kv.count(pfx + "enabled"))    fx.enabled        = kv[pfx + "enabled"] != "0";
        if (kv.count(pfx + "paramcount")) fx.paramCount     = std::stoi(kv[pfx + "paramcount"]);
        for (int p = 0; p < fx.paramCount && p < 16; ++p)
        {
            std::string k = pfx + "param." + std::to_string(p);
            if (kv.count(k)) fx.params[p] = std::stof(kv[k]);
        }
    }
    m_selected = m_graph->effects.empty() ? -1 : 0;
    if (m_onChanged) m_onChanged();
    return true;
}

void PostProcessPanel::LoadDefaultStack()
{
    if (!m_graph) return;
    m_graph->effects.clear();

    const auto& b = Builtins();
    // Color Grading (ACES, slight contrast/saturation)
    auto cg = MakeEffect(b[0]);
    cg.params[1] = 1.05f;   // contrast
    cg.params[2] = 1.10f;   // saturation
    m_graph->effects.push_back(cg);
    // Subtle vignette
    for (const auto& e : b) if (std::string(e.name) == "Vignette") {
        auto v = MakeEffect(e); v.params[0] = 0.45f; m_graph->effects.push_back(v); break;
    }
    // Subtle film grain
    for (const auto& e : b) if (std::string(e.name) == "Film Grain") {
        auto g = MakeEffect(e); g.params[0] = 0.04f; m_graph->effects.push_back(g); break;
    }

    m_selected = 0;
    if (m_onChanged) m_onChanged();
}

// ─────────────────────────────────────────────────────────────────────────────
//  UI
// ─────────────────────────────────────────────────────────────────────────────
void PostProcessPanel::DrawPresetBar()
{
    if (!m_presetsScanned) RefreshPresets();

    ImGui::TextDisabled("PRESET");
    ImGui::SameLine();

    float w = ImGui::GetContentRegionAvail().x;
    ImGui::SetNextItemWidth(w * 0.45f);
    if (ImGui::BeginCombo("##presetsel", "Load preset..."))
    {
        for (const auto& path : m_presetFiles)
        {
            std::string label = fs::path(path).stem().string();
            if (ImGui::Selectable(label.c_str()))
                LoadPreset(path);
        }
        if (m_presetFiles.empty())
            ImGui::TextDisabled("(none in Content/postprocess)");
        ImGui::EndCombo();
    }

    ImGui::SameLine();
    if (ImGui::Button("Save As...")) ImGui::OpenPopup("##SavePreset");
    ImGui::SameLine();
    if (ImGui::Button("Default"))    LoadDefaultStack();
    ImGui::SameLine();
    if (ImGui::Button("Refresh"))    RefreshPresets();

    if (ImGui::BeginPopup("##SavePreset"))
    {
        ImGui::TextDisabled("Save preset as");
        ImGui::SetNextItemWidth(240);
        bool enter = ImGui::InputText("##ppname", m_savePresetName, sizeof(m_savePresetName),
                                      ImGuiInputTextFlags_EnterReturnsTrue);
        if (ImGui::Button("Save") || enter)
        {
            if (SavePreset(m_savePresetName)) ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void PostProcessPanel::DrawAddMenu()
{
    if (ImGui::Button("+ Add Effect")) ImGui::OpenPopup("##AddEffect");

    if (ImGui::BeginPopup("##AddEffect"))
    {
        ImGui::TextDisabled("BUILT-IN");
        ImGui::Separator();
        for (const auto& d : Builtins())
        {
            if (ImGui::MenuItem(d.name))
            {
                m_graph->effects.push_back(MakeEffect(d));
                m_selected = static_cast<int>(m_graph->effects.size()) - 1;
                if (m_onChanged) m_onChanged();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", d.desc);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Custom Shader..."))
        {
            std::strncpy(m_addName,   "New Effect",                   sizeof(m_addName)   - 1);
            std::strncpy(m_addShader, "shaders/passthrough.frag.spv", sizeof(m_addShader) - 1);
            ImGui::CloseCurrentPopup();
            ImGui::OpenPopup("##AddCustom");
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("##AddCustom"))
    {
        ImGui::TextDisabled("Custom effect");
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
            m_graph->effects.push_back(fx);
            m_selected = static_cast<int>(m_graph->effects.size()) - 1;
            if (m_onChanged) m_onChanged();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void PostProcessPanel::DrawEffectInspector(int index)
{
    auto& fx = m_graph->effects[index];
    const PPEffectDesc* builtin = FindBuiltin(fx.fragShaderPath);

    ImGui::Separator();

    // Name
    char nameBuf[128];
    std::strncpy(nameBuf, fx.name.c_str(), sizeof(nameBuf) - 1);
    nameBuf[sizeof(nameBuf) - 1] = '\0';
    ImGui::TextDisabled("Name");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##name", nameBuf, sizeof(nameBuf)))
    {
        fx.name = nameBuf;
        if (m_onChanged) m_onChanged();
    }

    if (builtin)
    {
        // ── Built-in: labelled sliders ──────────────────────────────────────
        ImGui::TextDisabled("%s", builtin->desc);
        ImGui::Spacing();

        int pc = std::min<int>(static_cast<int>(builtin->params.size()), 16);
        if (fx.paramCount < pc) fx.paramCount = pc;   // keep count in sync

        for (int i = 0; i < pc; ++i)
        {
            const PPParamDesc& pd = builtin->params[i];
            ImGui::PushID(i);
            ImGui::SetNextItemWidth(-1);
            bool changed = false;
            if (pd.enumLabels)
            {
                int v = static_cast<int>(fx.params[i] + 0.5f);
                if (ImGui::Combo(pd.label, &v, pd.enumLabels))
                { fx.params[i] = static_cast<float>(v); changed = true; }
            }
            else
            {
                changed = ImGui::SliderFloat(pd.label, &fx.params[i], pd.minV, pd.maxV);
            }
            if (changed && m_onChanged) m_onChanged();
            ImGui::PopID();
        }

        ImGui::Spacing();
        if (ImGui::SmallButton("Reset to defaults"))
        {
            for (int i = 0; i < pc; ++i) fx.params[i] = builtin->params[i].defV;
            if (m_onChanged) m_onChanged();
        }
    }
    else
    {
        // ── Custom shader: raw path + generic params ────────────────────────
        if (ShaderDropField("Shader (.spv)", fx.fragShaderPath))
            if (m_onChanged) m_onChanged();

        char shaderBuf[512];
        std::strncpy(shaderBuf, fx.fragShaderPath.c_str(), sizeof(shaderBuf) - 1);
        shaderBuf[sizeof(shaderBuf) - 1] = '\0';
        ImGui::SetNextItemWidth(-80);
        if (ImGui::InputText("##shaderText", shaderBuf, sizeof(shaderBuf)))
            fx.fragShaderPath = shaderBuf;
        ImGui::SameLine();
        if (ImGui::Button("Reload"))
            if (m_onChanged) m_onChanged();

        int pc = fx.paramCount;
        ImGui::TextDisabled("Param count (0-16)");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderInt("##pc", &pc, 0, 16))
        {
            fx.paramCount = pc;
            if (m_onChanged) m_onChanged();
        }
        for (int p = 0; p < fx.paramCount; ++p)
        {
            char pid[16];
            snprintf(pid, sizeof(pid), "p%d", p);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::DragFloat(pid, &fx.params[p], 0.01f))
                if (m_onChanged) m_onChanged();
        }
    }
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

    DrawPresetBar();
    ImGui::Separator();

    auto& effects = m_graph->effects;

    // ── Toolbar ───────────────────────────────────────────────────────────────
    DrawAddMenu();
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
    ImGui::BeginChild("##EffectList", ImVec2(0, 150), true);
    for (int i = 0; i < (int)effects.size(); ++i)
    {
        auto& fx = effects[i];
        ImGui::PushID(i);
        bool en = fx.enabled;
        if (ImGui::Checkbox("##en", &en))
        {
            fx.enabled = en;
            if (m_onChanged) m_onChanged();
        }
        ImGui::SameLine();

        char label[160];
        snprintf(label, sizeof(label), "%d. %s", i + 1, fx.name.c_str());
        if (ImGui::Selectable(label, i == m_selected))
            m_selected = i;
        ImGui::PopID();
    }
    if (effects.empty())
        ImGui::TextDisabled("No effects. Click '+ Add Effect' or load a preset.");
    ImGui::EndChild();

    // ── Selected effect inspector ─────────────────────────────────────────────
    if (m_selected >= 0 && m_selected < (int)effects.size())
        DrawEffectInspector(m_selected);

    ImGui::End();
}

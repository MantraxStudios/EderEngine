#include "UISystem.h"
#include "UIRenderer.h"
#include <algorithm>
#include <iostream>
#include <cmath>

UISystem& UISystem::Get()
{
    static UISystem s_instance;
    return s_instance;
}

void UISystem::Init()
{
    m_elements.clear();
    m_nextId   = 1;
    m_dragging = 0;
}

void UISystem::Shutdown()
{
    m_elements.clear();
}

void UISystem::SetScreenSize(float w, float h)
{
    if (m_screenW != w || m_screenH != h)
    {
        m_screenW = w;
        m_screenH = h;
        UpdateScaling();
    }
}

void UISystem::UpdateScaling()
{
    float scaleX = m_screenW / VIRT_W;
    float scaleY = m_screenH / VIRT_H;
    m_scale   = std::min(scaleX, scaleY);
    m_offsetX = (m_screenW - m_scale * VIRT_W) * 0.5f;
    m_offsetY = (m_screenH - m_scale * VIRT_H) * 0.5f;
}

float UISystem::VirtualToScreenX(float vx) const { return vx * m_scale + m_offsetX; }
float UISystem::VirtualToScreenY(float vy) const { return vy * m_scale + m_offsetY; }
float UISystem::ScreenToVirtualX(float sx) const { return (sx - m_offsetX) / m_scale; }
float UISystem::ScreenToVirtualY(float sy) const { return (sy - m_offsetY) / m_scale; }

glm::vec2 UISystem::ComputeAnchorOrigin(UIAnchor anchor) const
{
    switch (anchor)
    {
    case UIAnchor::TopLeft:      return { 0.f,          0.f          };
    case UIAnchor::TopCenter:    return { VIRT_W * 0.5f, 0.f          };
    case UIAnchor::TopRight:     return { VIRT_W,        0.f          };
    case UIAnchor::MiddleLeft:   return { 0.f,           VIRT_H * 0.5f};
    case UIAnchor::MiddleCenter: return { VIRT_W * 0.5f, VIRT_H * 0.5f};
    case UIAnchor::MiddleRight:  return { VIRT_W,         VIRT_H * 0.5f};
    case UIAnchor::BottomLeft:   return { 0.f,            VIRT_H       };
    case UIAnchor::BottomCenter: return { VIRT_W * 0.5f,  VIRT_H       };
    case UIAnchor::BottomRight:  return { VIRT_W,          VIRT_H       };
    }
    return { 0.f, 0.f };
}

glm::vec4 UISystem::ComputeVirtualRect(const UIElement& elem) const
{
    glm::vec2 origin = ComputeAnchorOrigin(elem.anchor);
    return { origin.x + elem.position.x,
             origin.y + elem.position.y,
             elem.size.x,
             elem.size.y };
}

bool UISystem::HitTest(float px, float py, const UIElement& elem) const
{
    float vx = ScreenToVirtualX(px);
    float vy = ScreenToVirtualY(py);
    glm::vec4 r = ComputeVirtualRect(elem);
    return vx >= r.x && vx <= r.x + r.z && vy >= r.y && vy <= r.y + r.w;
}

void UISystem::Update(float dt)
{
    for (auto& [id, elem] : m_elements)
    {
        if (elem.type == UIElementType::InputField && elem.focused)
        {
            elem.cursorTimer += dt;
            if (elem.cursorTimer >= 0.5f)
            {
                elem.cursorTimer -= 0.5f;
                elem.cursorVisible = !elem.cursorVisible;
            }
        }
    }
}

void UISystem::HandleEvent(const SDL_Event& event)
{
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
    {
        float mx = (float)event.button.x;
        float my = (float)event.button.y;

        UIElement* newFocus = nullptr;

        auto sorted = GetSortedElements();
        for (int i = (int)sorted.size() - 1; i >= 0; --i)
        {
            UIElement* e = sorted[i];
            if (!e->visible) continue;
            if (!HitTest(mx, my, *e)) continue;

            if (e->type == UIElementType::Button && event.button.button == SDL_BUTTON_LEFT)
            {
                if (e->onClick) e->onClick();
            }
            else if (e->type == UIElementType::Slider && event.button.button == SDL_BUTTON_LEFT)
            {
                m_dragging = e->id;
                glm::vec4 r = ComputeVirtualRect(*e);
                float vx = ScreenToVirtualX(mx);
                float t  = (vx - r.x) / r.z;
                t = std::clamp(t, 0.f, 1.f);
                e->value = e->minValue + t * (e->maxValue - e->minValue);
                if (e->onChanged) e->onChanged(e->value);
            }
            else if (e->type == UIElementType::InputField)
            {
                newFocus = e;
            }
            break;
        }

        for (auto& [id, elem] : m_elements)
        {
            if (elem.type == UIElementType::InputField)
            {
                bool wasFocused = elem.focused;
                elem.focused = (&elem == newFocus);
                if (elem.focused && !wasFocused)
                {
                    elem.cursorTimer   = 0.f;
                    elem.cursorVisible = true;
                    SDL_StartTextInput(nullptr);
                }
                else if (!elem.focused && wasFocused)
                {
                    SDL_StopTextInput(nullptr);
                }
            }
        }
    }
    else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP)
    {
        if (event.button.button == SDL_BUTTON_LEFT)
            m_dragging = 0;
    }
    else if (event.type == SDL_EVENT_MOUSE_MOTION)
    {
        if (m_dragging != 0)
        {
            auto it = m_elements.find(m_dragging);
            if (it != m_elements.end())
            {
                UIElement& e = it->second;
                glm::vec4 r  = ComputeVirtualRect(e);
                float vx = ScreenToVirtualX((float)event.motion.x);
                float t  = (vx - r.x) / r.z;
                t = std::clamp(t, 0.f, 1.f);
                e.value = e.minValue + t * (e.maxValue - e.minValue);
                if (e.onChanged) e.onChanged(e.value);
            }
        }
    }
    else if (event.type == SDL_EVENT_TEXT_INPUT)
    {
        for (auto& [id, elem] : m_elements)
        {
            if (elem.type == UIElementType::InputField && elem.focused)
            {
                elem.inputText += event.text.text;
                break;
            }
        }
    }
    else if (event.type == SDL_EVENT_KEY_DOWN)
    {
        for (auto& [id, elem] : m_elements)
        {
            if (elem.type == UIElementType::InputField && elem.focused)
            {
                if (event.key.key == SDLK_BACKSPACE && !elem.inputText.empty())
                {
                    elem.inputText.pop_back();
                }
                else if (event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER)
                {
                    if (elem.onSubmit) elem.onSubmit(elem.inputText);
                    elem.focused = false;
                    SDL_StopTextInput(nullptr);
                }
                else if (event.key.key == SDLK_ESCAPE)
                {
                    elem.focused = false;
                    SDL_StopTextInput(nullptr);
                }
                break;
            }
        }
    }
}

uint32_t UISystem::CreateElement(const UIElement& elem)
{
    UIElement e = elem;
    e.id        = m_nextId++;
    uint32_t id = e.id;
    m_elements[id] = std::move(e);
    return id;
}

UIElement* UISystem::GetElement(uint32_t id)
{
    auto it = m_elements.find(id);
    return it != m_elements.end() ? &it->second : nullptr;
}

void UISystem::Destroy(uint32_t id)
{
    m_elements.erase(id);
}

void UISystem::DestroyAll()
{
    m_elements.clear();
}

std::vector<UIElement*> UISystem::GetSortedElements()
{
    std::vector<UIElement*> out;
    out.reserve(m_elements.size());
    for (auto& [id, e] : m_elements)
        out.push_back(&e);
    std::sort(out.begin(), out.end(), [](const UIElement* a, const UIElement* b) {
        return a->zOrder < b->zOrder;
    });
    return out;
}

void UISystem::BindLua(sol::state& lua)
{
    sol::table UI = lua.create_named_table("UI");

    sol::table Anchor = lua.create_named_table("UIAnchor");
    Anchor["TopLeft"]      = (int)UIAnchor::TopLeft;
    Anchor["TopCenter"]    = (int)UIAnchor::TopCenter;
    Anchor["TopRight"]     = (int)UIAnchor::TopRight;
    Anchor["MiddleLeft"]   = (int)UIAnchor::MiddleLeft;
    Anchor["MiddleCenter"] = (int)UIAnchor::MiddleCenter;
    Anchor["MiddleRight"]  = (int)UIAnchor::MiddleRight;
    Anchor["BottomLeft"]   = (int)UIAnchor::BottomLeft;
    Anchor["BottomCenter"] = (int)UIAnchor::BottomCenter;
    Anchor["BottomRight"]  = (int)UIAnchor::BottomRight;

    auto parseColor = [](sol::optional<sol::table> t, glm::vec4 def) -> glm::vec4
    {
        if (!t) return def;
        sol::table c = *t;
        return { c.get_or("r", def.r), c.get_or("g", def.g),
                 c.get_or("b", def.b), c.get_or("a", def.a) };
    };

    auto parseAnchor = [](sol::optional<int> v) -> UIAnchor
    {
        if (!v) return UIAnchor::TopLeft;
        return (UIAnchor)std::clamp(*v, 0, 8);
    };

    auto makeCb0 = [](sol::function fn) -> std::function<void()> {
        return [fn]() mutable {
            auto r = fn();
            if (!r.valid()) { sol::error err = r; std::cerr << "[UI] callback: " << err.what() << "\n"; }
        };
    };
    auto makeCb1f = [](sol::function fn) -> std::function<void(float)> {
        return [fn](float v) mutable {
            auto r = fn(v);
            if (!r.valid()) { sol::error err = r; std::cerr << "[UI] callback: " << err.what() << "\n"; }
        };
    };
    auto makeCb1s = [](sol::function fn) -> std::function<void(const std::string&)> {
        return [fn](const std::string& s) mutable {
            auto r = fn(s);
            if (!r.valid()) { sol::error err = r; std::cerr << "[UI] callback: " << err.what() << "\n"; }
        };
    };

    UI["image"] = [parseColor, parseAnchor](sol::table t) -> int
    {
        UIElement e;
        e.type        = UIElementType::Image;
        e.anchor      = parseAnchor(t.get<sol::optional<int>>("anchor"));
        e.position    = { t.get_or("x", 0.f), t.get_or("y", 0.f) };
        e.size        = { t.get_or("width", 100.f), t.get_or("height", 100.f) };
        e.color       = parseColor(t.get<sol::optional<sol::table>>("color"), { 1,1,1,1 });
        e.texturePath = t.get_or<std::string>("texture", "");
        e.zOrder      = t.get_or("zOrder", 0);
        e.visible     = t.get_or("visible", true);
        return (int)UISystem::Get().CreateElement(e);
    };

    UI["text"] = [parseColor, parseAnchor](sol::table t) -> int
    {
        UIElement e;
        e.type      = UIElementType::Text;
        e.anchor    = parseAnchor(t.get<sol::optional<int>>("anchor"));
        e.position  = { t.get_or("x", 0.f), t.get_or("y", 0.f) };
        e.size      = { t.get_or("width", 200.f), t.get_or("height", 30.f) };
        e.text      = t.get_or<std::string>("text", "");
        e.textColor = parseColor(t.get<sol::optional<sol::table>>("textColor"), { 1,1,1,1 });
        e.fontSize  = t.get_or("fontSize", 18.f);
        e.zOrder    = t.get_or("zOrder", 0);
        e.visible   = t.get_or("visible", true);
        return (int)UISystem::Get().CreateElement(e);
    };

    UI["button"] = [parseColor, parseAnchor, makeCb0](sol::table t) -> int
    {
        UIElement e;
        e.type      = UIElementType::Button;
        e.anchor    = parseAnchor(t.get<sol::optional<int>>("anchor"));
        e.position  = { t.get_or("x", 0.f), t.get_or("y", 0.f) };
        e.size      = { t.get_or("width", 200.f), t.get_or("height", 50.f) };
        e.color     = parseColor(t.get<sol::optional<sol::table>>("color"), { 0.2f,0.4f,0.8f,1.f });
        e.text      = t.get_or<std::string>("text", "Button");
        e.textColor = parseColor(t.get<sol::optional<sol::table>>("textColor"), { 1,1,1,1 });
        e.fontSize  = t.get_or("fontSize", 18.f);
        e.zOrder    = t.get_or("zOrder", 0);
        e.visible   = t.get_or("visible", true);
        if (auto cb = t.get<sol::optional<sol::function>>("onClick"); cb)
            e.onClick = makeCb0(*cb);
        return (int)UISystem::Get().CreateElement(e);
    };

    UI["slider"] = [parseColor, parseAnchor, makeCb1f](sol::table t) -> int
    {
        UIElement e;
        e.type        = UIElementType::Slider;
        e.anchor      = parseAnchor(t.get<sol::optional<int>>("anchor"));
        e.position    = { t.get_or("x", 0.f), t.get_or("y", 0.f) };
        e.size        = { t.get_or("width", 300.f), t.get_or("height", 20.f) };
        e.color       = parseColor(t.get<sol::optional<sol::table>>("color"), { 0.15f,0.15f,0.15f,1.f });
        e.fillColor   = parseColor(t.get<sol::optional<sol::table>>("fillColor"), { 0.3f,0.6f,1.f,1.f });
        e.handleColor = parseColor(t.get<sol::optional<sol::table>>("handleColor"), { 1,1,1,1 });
        e.value       = t.get_or("value", 0.5f);
        e.minValue    = t.get_or("minValue", 0.f);
        e.maxValue    = t.get_or("maxValue", 1.f);
        e.zOrder      = t.get_or("zOrder", 0);
        e.visible     = t.get_or("visible", true);
        if (auto cb = t.get<sol::optional<sol::function>>("onChanged"); cb)
            e.onChanged = makeCb1f(*cb);
        return (int)UISystem::Get().CreateElement(e);
    };

    UI["inputField"] = [parseColor, parseAnchor, makeCb1s](sol::table t) -> int
    {
        UIElement e;
        e.type        = UIElementType::InputField;
        e.anchor      = parseAnchor(t.get<sol::optional<int>>("anchor"));
        e.position    = { t.get_or("x", 0.f), t.get_or("y", 0.f) };
        e.size        = { t.get_or("width", 300.f), t.get_or("height", 40.f) };
        e.color       = parseColor(t.get<sol::optional<sol::table>>("color"), { 0.1f,0.1f,0.1f,0.9f });
        e.textColor   = parseColor(t.get<sol::optional<sol::table>>("textColor"), { 1,1,1,1 });
        e.placeholder = t.get_or<std::string>("placeholder", "");
        e.fontSize    = t.get_or("fontSize", 18.f);
        e.zOrder      = t.get_or("zOrder", 0);
        e.visible     = t.get_or("visible", true);
        if (auto cb = t.get<sol::optional<sol::function>>("onSubmit"); cb)
            e.onSubmit = makeCb1s(*cb);
        return (int)UISystem::Get().CreateElement(e);
    };

    UI["setVisible"] = [](int id, bool v) {
        auto* e = UISystem::Get().GetElement((uint32_t)id);
        if (e) e->visible = v;
    };

    UI["setPosition"] = [](int id, float x, float y) {
        auto* e = UISystem::Get().GetElement((uint32_t)id);
        if (e) e->position = { x, y };
    };

    UI["setSize"] = [](int id, float w, float h) {
        auto* e = UISystem::Get().GetElement((uint32_t)id);
        if (e) e->size = { w, h };
    };

    UI["setColor"] = [parseColor](int id, sol::table c) {
        auto* e = UISystem::Get().GetElement((uint32_t)id);
        if (e) e->color = parseColor(sol::optional<sol::table>(c), e->color);
    };

    UI["setTextColor"] = [parseColor](int id, sol::table c) {
        auto* e = UISystem::Get().GetElement((uint32_t)id);
        if (e) e->textColor = parseColor(sol::optional<sol::table>(c), e->textColor);
    };

    UI["setOnClick"] = [makeCb0](int id, sol::function fn) {
        auto* e = UISystem::Get().GetElement((uint32_t)id);
        if (e && e->type == UIElementType::Button) e->onClick = makeCb0(fn);
    };

    UI["setOnChanged"] = [makeCb1f](int id, sol::function fn) {
        auto* e = UISystem::Get().GetElement((uint32_t)id);
        if (e && e->type == UIElementType::Slider) e->onChanged = makeCb1f(fn);
    };

    UI["setOnSubmit"] = [makeCb1s](int id, sol::function fn) {
        auto* e = UISystem::Get().GetElement((uint32_t)id);
        if (e && e->type == UIElementType::InputField) e->onSubmit = makeCb1s(fn);
    };

    UI["setText"] = [](int id, const std::string& s) {
        auto* e = UISystem::Get().GetElement((uint32_t)id);
        if (e) e->text = s;
    };

    UI["getText"] = [](int id) -> std::string {
        auto* e = UISystem::Get().GetElement((uint32_t)id);
        return e ? e->text : "";
    };

    UI["setFontSize"] = [](int id, float s) {
        auto* e = UISystem::Get().GetElement((uint32_t)id);
        if (e) e->fontSize = s;
    };

    UI["setValue"] = [](int id, float v) {
        auto* e = UISystem::Get().GetElement((uint32_t)id);
        if (e && e->type == UIElementType::Slider)
            e->value = std::clamp(v, e->minValue, e->maxValue);
    };

    UI["getValue"] = [](int id) -> float {
        auto* e = UISystem::Get().GetElement((uint32_t)id);
        return (e && e->type == UIElementType::Slider) ? e->value : 0.f;
    };

    UI["getInputText"] = [](int id) -> std::string {
        auto* e = UISystem::Get().GetElement((uint32_t)id);
        return (e && e->type == UIElementType::InputField) ? e->inputText : "";
    };

    UI["setInputText"] = [](int id, const std::string& s) {
        auto* e = UISystem::Get().GetElement((uint32_t)id);
        if (e && e->type == UIElementType::InputField) e->inputText = s;
    };

    UI["setAnchor"] = [](int id, int anchor) {
        auto* e = UISystem::Get().GetElement((uint32_t)id);
        if (e) e->anchor = (UIAnchor)std::clamp(anchor, 0, 8);
    };

    UI["setZOrder"] = [](int id, int z) {
        auto* e = UISystem::Get().GetElement((uint32_t)id);
        if (e) e->zOrder = z;
    };

    UI["destroy"] = [](int id) {
        UISystem::Get().Destroy((uint32_t)id);
    };

    UI["destroyAll"] = []() {
        UISystem::Get().DestroyAll();
    };

    UI["isVisible"] = [](int id) -> bool {
        auto* e = UISystem::Get().GetElement((uint32_t)id);
        return e ? e->visible : false;
    };

    UI["loadFont"] = [](const std::string& path, float size) {
        UIRenderer_LoadFont(path, size);
    };

    UI["screenWidth"]  = []() -> float { return UISystem::Get().GetScreenW(); };
    UI["screenHeight"] = []() -> float { return UISystem::Get().GetScreenH(); };
    UI["virtualWidth"]  = []() -> float { return UISystem::VIRT_W; };
    UI["virtualHeight"] = []() -> float { return UISystem::VIRT_H; };
}

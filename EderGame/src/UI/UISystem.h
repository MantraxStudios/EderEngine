#pragma once
#define SOL_ALL_SAFETIES_ON 1
#define SOL_USING_CXX_OPTIONAL 1
#include <sol.hpp>
#include <SDL3/SDL.h>
#include <vector>
#include <unordered_map>
#include <memory>
#include "UITypes.h"

class UISystem
{
public:
    static UISystem& Get();

    void Init();
    void Shutdown();
    void Update(float dt);
    void HandleEvent(const SDL_Event& event);

    void SetScreenSize(float w, float h);

    uint32_t CreateElement(const UIElement& elem);
    UIElement* GetElement(uint32_t id);
    void Destroy(uint32_t id);
    void DestroyAll();

    std::vector<UIElement*> GetSortedElements();

    glm::vec2 ComputeAnchorOrigin(UIAnchor anchor) const;
    glm::vec4 ComputeVirtualRect(const UIElement& elem) const;

    void BindLua(sol::state& lua);

    float GetScale()   const { return m_scale;   }
    float GetOffsetX() const { return m_offsetX; }
    float GetOffsetY() const { return m_offsetY; }
    float GetScreenW() const { return m_screenW; }
    float GetScreenH() const { return m_screenH; }

    static constexpr float VIRT_W = 1920.f;
    static constexpr float VIRT_H = 1080.f;

private:
    UISystem()  = default;
    ~UISystem() = default;

    void UpdateScaling();
    bool HitTest(float px, float py, const UIElement& elem) const;
    float VirtualToScreenX(float vx) const;
    float VirtualToScreenY(float vy) const;
    float ScreenToVirtualX(float sx) const;
    float ScreenToVirtualY(float sy) const;

    uint32_t m_nextId  = 1;
    uint32_t m_dragging = 0;

    std::unordered_map<uint32_t, UIElement> m_elements;

    float m_screenW = 1920.f;
    float m_screenH = 1080.f;
    float m_scale   = 1.f;
    float m_offsetX = 0.f;
    float m_offsetY = 0.f;
};

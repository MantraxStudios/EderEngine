#pragma once
#include <glm/glm.hpp>
#include <string>
#include <functional>
#include <cstdint>

enum class UIAnchor : uint8_t
{
    TopLeft = 0,
    TopCenter,
    TopRight,
    MiddleLeft,
    MiddleCenter,
    MiddleRight,
    BottomLeft,
    BottomCenter,
    BottomRight
};

enum class UIElementType : uint8_t
{
    Image,
    Text,
    Button,
    Slider,
    InputField
};

struct UIElement
{
    uint32_t       id           = 0;
    UIElementType  type         = UIElementType::Text;
    UIAnchor       anchor       = UIAnchor::TopLeft;
    glm::vec2      position     = { 0.f, 0.f };
    glm::vec2      size         = { 100.f, 30.f };
    glm::vec4      color        = { 1.f, 1.f, 1.f, 1.f };
    bool           visible      = true;
    int            zOrder       = 0;

    std::string    text;
    glm::vec4      textColor    = { 1.f, 1.f, 1.f, 1.f };
    float          fontSize     = 18.f;

    std::string    texturePath;

    float          value        = 0.5f;
    float          minValue     = 0.f;
    float          maxValue     = 1.f;
    glm::vec4      fillColor    = { 0.3f, 0.6f, 1.0f, 1.0f };
    glm::vec4      handleColor  = { 1.f, 1.f, 1.f, 1.f };

    std::string    inputText;
    std::string    placeholder;
    bool           focused      = false;
    float          cursorTimer  = 0.f;
    bool           cursorVisible = false;

    std::function<void()>                   onClick;
    std::function<void(float)>              onChanged;
    std::function<void(const std::string&)> onSubmit;
};

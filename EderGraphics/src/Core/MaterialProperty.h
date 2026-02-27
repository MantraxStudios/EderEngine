#pragma once
#include <variant>
#include <string>
#include <cstdint>
#include <glm/glm.hpp>

enum class MaterialPropertyType
{
    Float, Int, Vec2, Vec3, Vec4, Mat4
};

using MaterialPropertyValue = std::variant<
    float, int32_t, glm::vec2, glm::vec3, glm::vec4, glm::mat4
>;

inline size_t MaterialPropertySize(MaterialPropertyType type)
{
    switch (type)
    {
        case MaterialPropertyType::Float: return sizeof(float);
        case MaterialPropertyType::Int:   return sizeof(int32_t);
        case MaterialPropertyType::Vec2:  return sizeof(glm::vec2);
        case MaterialPropertyType::Vec3:  return sizeof(glm::vec3);
        case MaterialPropertyType::Vec4:  return sizeof(glm::vec4);
        case MaterialPropertyType::Mat4:  return sizeof(glm::mat4);
    }
    return 0;
}

inline size_t MaterialPropertyAlignment(MaterialPropertyType type)
{
    switch (type)
    {
        case MaterialPropertyType::Float: return 4;
        case MaterialPropertyType::Int:   return 4;
        case MaterialPropertyType::Vec2:  return 8;
        case MaterialPropertyType::Vec3:  return 16;
        case MaterialPropertyType::Vec4:  return 16;
        case MaterialPropertyType::Mat4:  return 16;
    }
    return 4;
}

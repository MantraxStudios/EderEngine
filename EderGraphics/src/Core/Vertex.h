#pragma once
#include <glm/glm.hpp>
#include <vulkan/vulkan.hpp>
#include <array>

struct Vertex
{
    glm::vec3  position;
    glm::vec3  normal;
    glm::vec2  uv;
    glm::vec3  tangent;
    glm::vec3  bitangent;
    glm::vec4  color;
    glm::uvec4 boneIndices = glm::uvec4(0);  // up to 4 bone influences per vertex
    glm::vec4  boneWeights = glm::vec4(0.0f); // corresponding blend weights (0 = no skinning)

    static vk::VertexInputBindingDescription GetBindingDescription()
    {
        vk::VertexInputBindingDescription binding{};
        binding.binding   = 0;
        binding.stride    = sizeof(Vertex);
        binding.inputRate = vk::VertexInputRate::eVertex;
        return binding;
    }

    static std::array<vk::VertexInputAttributeDescription, 8> GetAttributeDescriptions()
    {
        std::array<vk::VertexInputAttributeDescription, 8> attrs{};

        attrs[0].binding  = 0;
        attrs[0].location = 0;
        attrs[0].format   = vk::Format::eR32G32B32Sfloat;
        attrs[0].offset   = offsetof(Vertex, position);

        attrs[1].binding  = 0;
        attrs[1].location = 1;
        attrs[1].format   = vk::Format::eR32G32B32Sfloat;
        attrs[1].offset   = offsetof(Vertex, normal);

        attrs[2].binding  = 0;
        attrs[2].location = 2;
        attrs[2].format   = vk::Format::eR32G32Sfloat;
        attrs[2].offset   = offsetof(Vertex, uv);

        attrs[3].binding  = 0;
        attrs[3].location = 3;
        attrs[3].format   = vk::Format::eR32G32B32Sfloat;
        attrs[3].offset   = offsetof(Vertex, tangent);

        attrs[4].binding  = 0;
        attrs[4].location = 4;
        attrs[4].format   = vk::Format::eR32G32B32Sfloat;
        attrs[4].offset   = offsetof(Vertex, bitangent);

        attrs[5].binding  = 0;
        attrs[5].location = 5;
        attrs[5].format   = vk::Format::eR32G32B32A32Sfloat;
        attrs[5].offset   = offsetof(Vertex, color);

        // Bone indices — uint32 x4 (location 6)
        attrs[6].binding  = 0;
        attrs[6].location = 6;
        attrs[6].format   = vk::Format::eR32G32B32A32Uint;
        attrs[6].offset   = offsetof(Vertex, boneIndices);

        // Bone weights — float x4 (location 7)
        attrs[7].binding  = 0;
        attrs[7].location = 7;
        attrs[7].format   = vk::Format::eR32G32B32A32Sfloat;
        attrs[7].offset   = offsetof(Vertex, boneWeights);

        return attrs;
    }
};

#pragma once
#include "Core/DLLHeader.h"
#include <vulkan/vulkan_raii.hpp>
#include "Renderer/Vulkan/VulkanBuffer.h"
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <cstdint>

class Registry;

// Draws wireframe light gizmos (cross + rings / cone) into any
// active framebuffer render pass via LINE_LIST pipeline.
class EDERGRAPHICS_API VulkanGizmo
{
public:
    void Create (vk::Format colorFormat, vk::Format depthFormat);
    void Destroy();

    // Call between BeginRendering / EndRendering of the target framebuffer.
    // selectedEntity: entity to highlight (0 = none)
    void Draw(vk::CommandBuffer cmd,
              const Registry&  registry,
              const glm::mat4& viewProj,
              uint32_t         selectedEntity = 0);

private:
    struct Vertex   { float x, y, z; };
    struct PushData { glm::mat4 viewProj; glm::vec4 color; };
    struct DrawCall { uint32_t start, count; glm::vec4 color; };

    void BuildPipeline(vk::Format colorFormat, vk::Format depthFormat);

    static std::vector<uint32_t> LoadSpv(const std::string& path);
    static void AddCross  (std::vector<Vertex>& v, glm::vec3 pos, float size);
    static void AddCircle (std::vector<Vertex>& v, glm::vec3 center,
                            glm::vec3 right, glm::vec3 up, float radius, int N = 16);
    static void AddCone   (std::vector<Vertex>& v, glm::vec3 origin, glm::vec3 dir,
                            float range, float angleDeg, int N = 16);
    static void AddBox    (std::vector<Vertex>& v, const glm::mat4& world,
                            const glm::vec3& halfExtents, const glm::vec3& center);
    static void AddCapsule(std::vector<Vertex>& v, const glm::mat4& world,
                            float radius, float halfHeight,
                            const glm::vec3& center, int N = 24);

    vk::raii::PipelineLayout pipelineLayout = nullptr;
    vk::raii::Pipeline       pipeline       = nullptr;
    VulkanBuffer             vertexBuffer;

    static constexpr uint32_t MAX_VERTS = 65536;
};

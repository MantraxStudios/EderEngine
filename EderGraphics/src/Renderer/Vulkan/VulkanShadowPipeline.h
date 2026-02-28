#pragma once
#include "ImportCore.h"
#include "../../Core/Vertex.h"
#include <glm/glm.hpp>

class EDERGRAPHICS_API VulkanShadowPipeline
{
public:
    void Create (vk::Format depthFormat);
    void Bind   (vk::CommandBuffer cmd);
    void Destroy();

    vk::raii::PipelineLayout& GetLayout() { return pipelineLayout; }

private:
    std::vector<uint32_t> LoadSpv(const std::string& path);

    vk::raii::PipelineLayout pipelineLayout = nullptr;
    vk::raii::Pipeline       pipeline       = nullptr;
};

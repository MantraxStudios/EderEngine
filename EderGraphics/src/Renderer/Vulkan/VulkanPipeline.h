#pragma once
#include "ImportCore.h"
#include "../../Core/Vertex.h"
#include <glm/glm.hpp>

class VulkanPipeline
{
public:
    void Create(const std::string& vertPath, const std::string& fragPath, vk::Format swapchainFormat);
    void Bind   (vk::CommandBuffer cmd);
    void Destroy();

    vk::raii::PipelineLayout&      GetLayout()              { return pipelineLayout; }
    vk::raii::DescriptorSetLayout& GetDescriptorSetLayout() { return descriptorSetLayout; }

private:
    std::vector<uint32_t>  LoadSpv(const std::string& path);
    vk::raii::ShaderModule CreateShaderModule(const std::vector<uint32_t>& code);

    vk::raii::DescriptorSetLayout descriptorSetLayout = nullptr;
    vk::raii::PipelineLayout      pipelineLayout      = nullptr;
    vk::raii::Pipeline            pipeline            = nullptr;
};

#pragma once
#include "ImportCore.h"
#include "../../Core/Vertex.h"
#include <glm/glm.hpp>

class EDERGRAPHICS_API VulkanPipeline
{
public:
    void Create(const std::string& vertPath, const std::string& fragPath, vk::Format swapchainFormat, vk::Format depthFormat);
    void Bind            (vk::CommandBuffer cmd);  // opaque
    void BindTransparent (vk::CommandBuffer cmd);  // alpha-blend, depth-write off
    void Destroy();

    vk::raii::PipelineLayout&      GetLayout()                    { return pipelineLayout; }
    vk::raii::DescriptorSetLayout& GetDescriptorSetLayout()      { return descriptorSetLayout; }
    vk::raii::DescriptorSetLayout& GetLightDescriptorSetLayout() { return lightDescriptorSetLayout; }
    vk::raii::DescriptorSetLayout& GetBoneDescriptorSetLayout()  { return boneDescriptorSetLayout; }

private:
    std::vector<uint32_t>  LoadSpv(const std::string& path);
    vk::raii::ShaderModule CreateShaderModule(const std::vector<uint32_t>& code);

    vk::raii::DescriptorSetLayout descriptorSetLayout      = nullptr;
    vk::raii::DescriptorSetLayout lightDescriptorSetLayout = nullptr;
    vk::raii::DescriptorSetLayout boneDescriptorSetLayout  = nullptr;  // set 2 — bone SSBO
    vk::raii::PipelineLayout      pipelineLayout           = nullptr;
    vk::raii::Pipeline            pipeline                 = nullptr;  // opaque
    vk::raii::Pipeline            pipelineTransparent      = nullptr;  // alpha-blend
};


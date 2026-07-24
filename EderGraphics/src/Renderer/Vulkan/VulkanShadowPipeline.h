#pragma once
#include "ImportCore.h"
#include "../../Core/Vertex.h"
#include <glm/glm.hpp>

class EDERGRAPHICS_API VulkanShadowPipeline
{
public:
    // materialDSL: the main pipeline's material set layout — used by the
    // alpha-tested variant so foliage casts texture-shaped shadows.
    void Create (vk::Format depthFormat, vk::DescriptorSetLayout materialDSL = VK_NULL_HANDLE);
    void Bind   (vk::CommandBuffer cmd);
    void BindAlpha(vk::CommandBuffer cmd);
    bool HasAlpha() const { return *alphaPipeline != vk::Pipeline{}; }
    void Destroy();

    vk::raii::PipelineLayout&      GetLayout()               { return pipelineLayout; }
    vk::raii::PipelineLayout&      GetAlphaLayout()          { return alphaPipelineLayout; }
    vk::raii::DescriptorSetLayout& GetBoneDescriptorSetLayout() { return boneDescriptorSetLayout; }

private:
    std::vector<uint32_t> LoadSpv(const std::string& path);

    vk::raii::DescriptorSetLayout boneDescriptorSetLayout = nullptr;
    vk::raii::PipelineLayout      pipelineLayout          = nullptr;
    vk::raii::Pipeline            pipeline                = nullptr;

    // Alpha-tested variant (set 0 = material, set 1 = bones)
    vk::raii::PipelineLayout      alphaPipelineLayout     = nullptr;
    vk::raii::Pipeline            alphaPipeline           = nullptr;
};

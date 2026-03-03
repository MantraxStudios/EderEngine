#pragma once
#include <vulkan/vulkan_raii.hpp>
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include "Renderer/Vulkan/VulkanFramebuffer.h"
#include "Renderer/Vulkan/VulkanBuffer.h"

class VulkanVolumetricLight
{
public:
    void Create (vk::Format colorFormat, uint32_t w, uint32_t h,
                  vk::DescriptorSetLayout lightDSL);  // layout from VulkanPipeline::GetLightDescriptorSetLayout()
    void Destroy();
    void Resize  (uint32_t w, uint32_t h);

    // Draw the volumetric lighting pass and write the result into outputFb.
    void Draw(vk::CommandBuffer   cmd,
              vk::ImageView       sceneView,
              vk::Sampler         sceneSampler,
              vk::ImageView       depthView,
              vk::Sampler         depthSampler,
              vk::ImageView       shadowMapView,      // 4-layer cascade array
              vk::Sampler         shadowMapSampler,
              const glm::mat4&    invViewProj,        // inverse(proj * view)
              const glm::mat4     shadowMatrices[4],  // cascade VP matrices
              const glm::vec4&    cascadeSplits,      // xyz = view-space split distances
              const glm::vec3&    lightDir,           // normalised direction toward sun
              const glm::vec3&    lightColor,
              float               lightIntensity,
              const glm::vec3&    cameraPos,
              // LightBuffer descriptor set bound at set 1
              vk::DescriptorSet   lightDS,
              // VolumetricLightComponent fields
              int                 numSteps,
              float               density,
              float               absorption,
              float               g,
              float               intensity,
              float               maxDistance,
              float               jitter,
              const glm::vec3&    tint);

    VulkanFramebuffer& GetOutput() { return outputFb; }

private:
    // Mirror of std140 UBO in volumetric.frag
    struct alignas(16) VolumetricUBO
    {
        glm::mat4  invViewProj;
        glm::mat4  shadowMatrix[4];
        glm::vec4  cascadeSplits;
        glm::vec4  lightDir;       // w unused
        glm::vec4  lightColor;     // w = intensity
        glm::vec4  camPosMaxDist;  // w = maxDistance
        glm::vec4  params;         // x=density y=absorption z=g w=jitter
        glm::ivec4 iparams;        // x = numSteps
        glm::vec4  tint;           // xyz = tint  w = finalIntensity
    };

    void BuildPipeline(vk::Format colorFormat, vk::DescriptorSetLayout lightDSL);
    void UpdateDescriptor(vk::ImageView sceneView,     vk::Sampler sceneSampler,
                          vk::ImageView depthView,     vk::Sampler depthSampler,
                          vk::ImageView shadowMapView, vk::Sampler shadowMapSampler);
    static std::vector<uint32_t> LoadSpv(const std::string& path);

    VulkanFramebuffer             outputFb;
    vk::Format                    fmt       = vk::Format::eUndefined;

    VulkanBuffer                  uboBuffer;

    vk::raii::DescriptorSetLayout setLayout      = nullptr;
    vk::raii::DescriptorPool      descriptorPool = nullptr;
    vk::raii::DescriptorSet       descriptorSet  = nullptr;
    vk::raii::PipelineLayout      pipelineLayout = nullptr;
    vk::raii::Pipeline            pipeline       = nullptr;

    // Track current bindings to skip redundant descriptor writes
    vk::ImageView lastSceneView   = nullptr;
    vk::Sampler   lastSceneSmp    = nullptr;
    vk::ImageView lastDepthView   = nullptr;
    vk::Sampler   lastDepthSmp    = nullptr;
    vk::ImageView lastShadowView  = nullptr;
    vk::Sampler   lastShadowSmp   = nullptr;
};

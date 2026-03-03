#pragma once
#include <vulkan/vulkan_raii.hpp>
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include "Renderer/Vulkan/VulkanFramebuffer.h"
#include "Renderer/Vulkan/VulkanBuffer.h"

class VulkanVolumetricFog
{
public:
    void Create (vk::Format colorFormat, uint32_t w, uint32_t h,
                  vk::DescriptorSetLayout lightDSL);
    void Destroy();
    void Resize  (uint32_t w, uint32_t h);

    void Draw(vk::CommandBuffer   cmd,
              vk::ImageView       sceneView,
              vk::Sampler         sceneSampler,
              vk::ImageView       depthView,
              vk::Sampler         depthSampler,
              const glm::mat4&    invViewProj,
              const glm::vec3&    cameraPos,
              // VolumetricFogComponent fields
              const glm::vec3&    fogColor,
              float               density,
              const glm::vec3&    horizonColor,
              float               heightFalloff,
              const glm::vec3&    sunScatterColor,
              float               scatterStrength,
              const glm::vec3&    lightDir,
              float               sunIntensity,
              float               heightOffset,
              float               maxFogAmount,
              float               fogStart,
              float               fogEnd,
              vk::DescriptorSet   lightDS);   // LightBuffer bound at set 1

    VulkanFramebuffer& GetOutput() { return outputFb; }

private:
    struct alignas(16) FogUBO
    {
        glm::mat4 invViewProj;
        glm::vec4 camPos;           // xyz = camPos
        glm::vec4 fogColor;         // xyz = base fog colour,    w = density
        glm::vec4 horizonColor;     // xyz = horizon colour,     w = heightFalloff
        glm::vec4 sunScatterColor;  // xyz = sun scatter colour, w = scatterStrength
        glm::vec4 lightDir;         // xyz = toward sun,         w = sunIntensity
        glm::vec4 params;           // x=heightOffset, y=maxFogAmount, z=fogStart, w=fogEnd
    };

    void BuildPipeline(vk::Format colorFormat, vk::DescriptorSetLayout lightDSL);
    void UpdateDescriptor(vk::ImageView sceneView, vk::Sampler sceneSampler,
                          vk::ImageView depthView,  vk::Sampler depthSampler);
    static std::vector<uint32_t> LoadSpv(const std::string& path);

    VulkanFramebuffer             outputFb;
    vk::Format                    fmt = vk::Format::eUndefined;

    VulkanBuffer                  uboBuffer;

    vk::raii::DescriptorSetLayout setLayout      = nullptr;
    vk::raii::DescriptorPool      descriptorPool = nullptr;
    vk::raii::DescriptorSet       descriptorSet  = nullptr;
    vk::raii::PipelineLayout      pipelineLayout = nullptr;
    vk::raii::Pipeline            pipeline       = nullptr;

    // Track bindings to skip redundant descriptor writes
    vk::ImageView lastSceneView = nullptr;
    vk::Sampler   lastSceneSmp  = nullptr;
    vk::ImageView lastDepthView = nullptr;
    vk::Sampler   lastDepthSmp  = nullptr;
};

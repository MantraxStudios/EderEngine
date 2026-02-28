#pragma once
#include "ImportCore.h"
#include "VulkanBuffer.h"
#include "../../Core/Vertex.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

class VulkanMesh
{
public:
    void Load          (const std::string& path);
    void DrawInstanced (vk::CommandBuffer cmd, uint32_t firstInstance, uint32_t instanceCount);
    void Destroy       ();

    uint32_t GetIndexCount()  { return indexCount; }
    uint32_t GetVertexCount() { return vertexCount; }

private:
    void ProcessNode(aiNode* node, const aiScene* scene);
    void ProcessMesh(aiMesh* mesh, const aiScene* scene);

    VulkanBuffer vertexBuffer;
    VulkanBuffer indexBuffer;

    std::vector<Vertex>   vertices;
    std::vector<uint32_t> indices;

    uint32_t vertexCount = 0;
    uint32_t indexCount  = 0;
};
#include "VulkanMesh.h"

void VulkanMesh::Load(const std::string& path)
{
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(path,
        aiProcess_Triangulate        |
        aiProcess_GenNormals         |
        aiProcess_CalcTangentSpace   |
        aiProcess_FlipUVs            |
        aiProcess_JoinIdenticalVertices);

    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
        throw std::runtime_error("Assimp: " + std::string(importer.GetErrorString()));

    ProcessNode(scene->mRootNode, scene);

    vertexCount = static_cast<uint32_t>(vertices.size());
    indexCount  = static_cast<uint32_t>(indices.size());

    vertexBuffer.Create(
        sizeof(Vertex) * vertices.size(),
        vk::BufferUsageFlagBits::eVertexBuffer,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
    vertexBuffer.Upload(vertices.data(), sizeof(Vertex) * vertices.size());

    indexBuffer.Create(
        sizeof(uint32_t) * indices.size(),
        vk::BufferUsageFlagBits::eIndexBuffer,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
    indexBuffer.Upload(indices.data(), sizeof(uint32_t) * indices.size());

    std::cout << "[Mesh] Loaded: " << path << " | V:" << vertexCount << " I:" << indexCount << std::endl;
}

void VulkanMesh::ProcessNode(aiNode* node, const aiScene* scene)
{
    for (uint32_t i = 0; i < node->mNumMeshes; i++)
        ProcessMesh(scene->mMeshes[node->mMeshes[i]], scene);
    for (uint32_t i = 0; i < node->mNumChildren; i++)
        ProcessNode(node->mChildren[i], scene);
}

void VulkanMesh::ProcessMesh(aiMesh* mesh, const aiScene* scene)
{
    uint32_t baseIndex = static_cast<uint32_t>(vertices.size());

    for (uint32_t i = 0; i < mesh->mNumVertices; i++)
    {
        Vertex v{};
        v.position  = { mesh->mVertices[i].x,  mesh->mVertices[i].y,  mesh->mVertices[i].z };
        v.normal    = { mesh->mNormals[i].x,    mesh->mNormals[i].y,    mesh->mNormals[i].z };
        v.tangent   = mesh->mTangents   ? glm::vec3(mesh->mTangents[i].x,   mesh->mTangents[i].y,   mesh->mTangents[i].z)   : glm::vec3(0);
        v.bitangent = mesh->mBitangents ? glm::vec3(mesh->mBitangents[i].x, mesh->mBitangents[i].y, mesh->mBitangents[i].z) : glm::vec3(0);
        v.uv        = mesh->mTextureCoords[0] ? glm::vec2(mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y) : glm::vec2(0);
        v.color     = { 1.0f, 1.0f, 1.0f, 1.0f };
        vertices.push_back(v);
    }

    for (uint32_t i = 0; i < mesh->mNumFaces; i++)
        for (uint32_t j = 0; j < mesh->mFaces[i].mNumIndices; j++)
            indices.push_back(baseIndex + mesh->mFaces[i].mIndices[j]);
}

void VulkanMesh::DrawInstanced(vk::CommandBuffer cmd, uint32_t firstInstance, uint32_t instanceCount)
{
    vk::Buffer     vb     = vertexBuffer.GetBuffer();
    vk::DeviceSize offset = 0;
    cmd.bindVertexBuffers(0, vb, offset);
    cmd.bindIndexBuffer(indexBuffer.GetBuffer(), 0, vk::IndexType::eUint32);
    cmd.drawIndexed(indexCount, instanceCount, 0, 0, firstInstance);
}

void VulkanMesh::Destroy()
{
    vertexBuffer.Destroy();
    indexBuffer.Destroy();
}
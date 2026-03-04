#include "VulkanMesh.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <cmath>
#include <algorithm>
#include <IO/AssetManager.h>

// ── Helper: convert aiMatrix4x4 (row-major) to glm::mat4 (column-major) ──────
static glm::mat4 AiToGlm(const aiMatrix4x4& m)
{
    return glm::transpose(glm::mat4(
        m.a1, m.a2, m.a3, m.a4,
        m.b1, m.b2, m.b3, m.b4,
        m.c1, m.c2, m.c3, m.c4,
        m.d1, m.d2, m.d3, m.d4));
}

// ── Keyframe search helpers ───────────────────────────────────────────────────
static uint32_t FindKeyV(float t, const std::vector<AnimKeyVec3>& k)
{
    for (uint32_t i = 0; i + 1 < (uint32_t)k.size(); i++)
        if (t < k[i+1].time) return i;
    return (uint32_t)k.size() > 0 ? (uint32_t)k.size() - 1 : 0;
}
static uint32_t FindKeyQ(float t, const std::vector<AnimKeyQuat>& k)
{
    for (uint32_t i = 0; i + 1 < (uint32_t)k.size(); i++)
        if (t < k[i+1].time) return i;
    return (uint32_t)k.size() > 0 ? (uint32_t)k.size() - 1 : 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Load
// ─────────────────────────────────────────────────────────────────────────────
void VulkanMesh::Load(const std::string& path)
{
    // If AssetManager is initialised, route through it so both loose-file and
    // PAK modes are handled transparently.
    auto& am = Krayon::AssetManager::Get();
    if (!am.GetWorkDir().empty() || am.IsCompiled())
    {
        std::vector<uint8_t> bytes = am.GetBytes(path);
        if (!bytes.empty())
        {
            // Use the original path as the Assimp file hint (preserves extension).
            LoadFromMemory(bytes.data(), bytes.size(), path);
            return;
        }
        // Fall through to direct disk read if AssetManager returned nothing
        // (e.g. path is absolute and outside the workDir).
    }

    // Direct disk fallback (original behaviour)
    _LoadFromPath(path);
}

void VulkanMesh::LoadFromMemory(const uint8_t* data, size_t size, const std::string& hint)
{
    // Clear any previous load
    vertices.clear(); indices.clear();
    boneNameToIndex.clear(); boneInfos.clear();
    animations.clear(); nodes.clear(); nodeNameToIndex.clear();
    rootNodeIndex = 0;

    Assimp::Importer importer;

    const aiScene* scene = importer.ReadFileFromMemory(
        data, size,
        aiProcess_Triangulate           |
        aiProcess_GenSmoothNormals      |
        aiProcess_CalcTangentSpace      |
        aiProcess_FlipUVs               |
        aiProcess_JoinIdenticalVertices |
        aiProcess_LimitBoneWeights,
        hint.empty() ? nullptr : hint.c_str());

    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
        throw std::runtime_error("Assimp (memory): " + std::string(importer.GetErrorString()));

    aiMatrix4x4 gi = scene->mRootNode->mTransformation;
    gi.Inverse();
    globalInverseTransform = AiToGlm(gi);

    ProcessNode(scene->mRootNode, scene, aiMatrix4x4()); // identity — nodeTransform is accumulated top-down
    BuildNodeTree(scene->mRootNode, UINT32_MAX);
    LoadAnimations(scene);

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

    std::cout << "[Mesh] LoadedFromMemory: " << (hint.empty() ? "(no hint)" : hint)
              << " | V:" << vertexCount
              << " I:" << indexCount
              << " | Bones:" << boneInfos.size()
              << " Anims:" << animations.size()
              << std::endl;
}

// ─────────────────────────────────────────────────────────────────────────────
// _LoadFromPath — direct disk load (original behaviour, used as fallback)
// ─────────────────────────────────────────────────────────────────────────────
void VulkanMesh::_LoadFromPath(const std::string& path)
{
    // Clear any previous load
    vertices.clear(); indices.clear();
    boneNameToIndex.clear(); boneInfos.clear();
    animations.clear(); nodes.clear(); nodeNameToIndex.clear();
    rootNodeIndex = 0;

    Assimp::Importer importer;

    const aiScene* scene = importer.ReadFile(path,
        aiProcess_Triangulate           |
        aiProcess_GenSmoothNormals      |
        aiProcess_CalcTangentSpace      |
        aiProcess_FlipUVs               |
        aiProcess_JoinIdenticalVertices |
        aiProcess_LimitBoneWeights);

    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
        throw std::runtime_error("Assimp: " + std::string(importer.GetErrorString()));

    aiMatrix4x4 gi = scene->mRootNode->mTransformation;
    gi.Inverse();
    globalInverseTransform = AiToGlm(gi);

    ProcessNode(scene->mRootNode, scene, aiMatrix4x4()); // identity — nodeTransform is accumulated top-down
    BuildNodeTree(scene->mRootNode, UINT32_MAX);
    LoadAnimations(scene);

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

    std::cout << "[Mesh] Loaded: " << path
              << " | V:" << vertexCount
              << " I:" << indexCount
              << " | Bones:" << boneInfos.size()
              << " Anims:" << animations.size()
              << std::endl;
}

// ─────────────────────────────────────────────────────────────────────────────
// ProcessNode / ProcessMesh
// ─────────────────────────────────────────────────────────────────────────────
void VulkanMesh::ProcessNode(aiNode* node, const aiScene* scene, const aiMatrix4x4& parentTransform)
{
    aiMatrix4x4 nodeTransform = parentTransform * node->mTransformation;
    for (uint32_t i = 0; i < node->mNumMeshes; i++)
        ProcessMesh(scene->mMeshes[node->mMeshes[i]], scene, nodeTransform);
    for (uint32_t i = 0; i < node->mNumChildren; i++)
        ProcessNode(node->mChildren[i], scene, nodeTransform);
}

void VulkanMesh::ProcessMesh(aiMesh* mesh, const aiScene* /*scene*/, const aiMatrix4x4& nodeTransform)
{
    uint32_t baseIndex = static_cast<uint32_t>(vertices.size());

    // For skinned meshes the bone/animation pipeline already accumulates the
    // full node-transform chain via ProcessNodeHierarchy, so we must NOT bake
    // the transform into raw vertex positions here (it would fight the skinning).
    // For static meshes we DO apply it so that FBX unit-scale (stored in the
    // root node's mTransformation) is correctly converted to engine units.
    const bool isStaticMesh = (mesh->mNumBones == 0);

    // Pre-compute the 3×3 normal matrix (inverse-transpose of the upper-left 3×3)
    // only when we're actually going to use it.
    aiMatrix3x3 normalMatrix;
    if (isStaticMesh)
    {
        normalMatrix = aiMatrix3x3(nodeTransform);
        normalMatrix.Inverse();
        normalMatrix.Transpose();
    }

    for (uint32_t i = 0; i < mesh->mNumVertices; i++)
    {
        Vertex v{};

        if (isStaticMesh)
        {
            aiVector3D pos = nodeTransform * mesh->mVertices[i];
            v.position = { pos.x, pos.y, pos.z };

            if (mesh->mNormals)
            {
                aiVector3D nor = normalMatrix * mesh->mNormals[i];
                v.normal = glm::normalize(glm::vec3(nor.x, nor.y, nor.z));
            }
            if (mesh->mTangents)
            {
                aiVector3D t = normalMatrix * mesh->mTangents[i];
                v.tangent = glm::normalize(glm::vec3(t.x, t.y, t.z));
            }
            if (mesh->mBitangents)
            {
                aiVector3D b = normalMatrix * mesh->mBitangents[i];
                v.bitangent = glm::normalize(glm::vec3(b.x, b.y, b.z));
            }
        }
        else
        {
            v.position  = { mesh->mVertices[i].x,  mesh->mVertices[i].y,  mesh->mVertices[i].z };
            v.normal    = { mesh->mNormals[i].x,    mesh->mNormals[i].y,    mesh->mNormals[i].z };
            v.tangent   = mesh->mTangents   ? glm::vec3(mesh->mTangents[i].x,   mesh->mTangents[i].y,   mesh->mTangents[i].z)   : glm::vec3(0);
            v.bitangent = mesh->mBitangents ? glm::vec3(mesh->mBitangents[i].x, mesh->mBitangents[i].y, mesh->mBitangents[i].z) : glm::vec3(0);
        }
        v.uv        = mesh->mTextureCoords[0] ? glm::vec2(mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y) : glm::vec2(0);
        v.color     = { 1.0f, 1.0f, 1.0f, 1.0f };
        // boneIndices / boneWeights are zero-initialised by default — filled below
        vertices.push_back(v);
    }

    // Load bone weights
    for (uint32_t b = 0; b < mesh->mNumBones; b++)
    {
        aiBone*     bone     = mesh->mBones[b];
        std::string boneName = bone->mName.C_Str();

        uint32_t boneIdx;
        auto it = boneNameToIndex.find(boneName);
        if (it == boneNameToIndex.end())
        {
            boneIdx = static_cast<uint32_t>(boneInfos.size());
            boneNameToIndex[boneName] = boneIdx;
            BoneInfo bi;
            bi.offsetMatrix = AiToGlm(bone->mOffsetMatrix);
            boneInfos.push_back(bi);
        }
        else
        {
            boneIdx = it->second;
        }

        for (uint32_t w = 0; w < bone->mNumWeights; w++)
        {
            uint32_t vertIdx = baseIndex + bone->mWeights[w].mVertexId;
            float    weight  = bone->mWeights[w].mWeight;
            if (weight < 0.0001f) continue;

            Vertex& v = vertices[vertIdx];
            // Fill the first free slot (up to 4 influences per vertex)
            for (int slot = 0; slot < 4; slot++)
            {
                if (v.boneWeights[slot] == 0.0f)
                {
                    v.boneIndices[slot] = boneIdx;
                    v.boneWeights[slot] = weight;
                    break;
                }
            }
        }
    }

    for (uint32_t i = 0; i < mesh->mNumFaces; i++)
        for (uint32_t j = 0; j < mesh->mFaces[i].mNumIndices; j++)
            indices.push_back(baseIndex + mesh->mFaces[i].mIndices[j]);
}

// ─────────────────────────────────────────────────────────────────────────────
// BuildNodeTree  — must be called AFTER ProcessNode so boneNameToIndex is populated
// ─────────────────────────────────────────────────────────────────────────────
void VulkanMesh::BuildNodeTree(aiNode* node, uint32_t parentIdx)
{
    uint32_t myIdx = static_cast<uint32_t>(nodes.size());

    SceneNode sn;
    sn.name           = node->mName.C_Str();
    sn.localTransform = AiToGlm(node->mTransformation);
    auto it = boneNameToIndex.find(sn.name);
    sn.boneIndex = (it != boneNameToIndex.end()) ? static_cast<int>(it->second) : -1;

    nodeNameToIndex[sn.name] = myIdx;
    nodes.push_back(sn);

    if (parentIdx == UINT32_MAX)
        rootNodeIndex = myIdx;
    else
        nodes[parentIdx].children.push_back(myIdx);

    for (uint32_t i = 0; i < node->mNumChildren; i++)
        BuildNodeTree(node->mChildren[i], myIdx);
}

// ─────────────────────────────────────────────────────────────────────────────
// LoadAnimations
// ─────────────────────────────────────────────────────────────────────────────
void VulkanMesh::LoadAnimations(const aiScene* scene)
{
    for (uint32_t a = 0; a < scene->mNumAnimations; a++)
    {
        aiAnimation* anim = scene->mAnimations[a];
        AnimationClip clip;
        clip.name           = anim->mName.C_Str();
        clip.duration       = static_cast<float>(anim->mDuration);
        clip.ticksPerSecond = static_cast<float>(anim->mTicksPerSecond > 0.0 ? anim->mTicksPerSecond : 25.0);

        for (uint32_t c = 0; c < anim->mNumChannels; c++)
        {
            aiNodeAnim* ch = anim->mChannels[c];
            BoneChannel bc;
            bc.nodeName = ch->mNodeName.C_Str();

            for (uint32_t k = 0; k < ch->mNumPositionKeys; k++)
                bc.posKeys.push_back({ static_cast<float>(ch->mPositionKeys[k].mTime),
                    glm::vec3(ch->mPositionKeys[k].mValue.x,
                              ch->mPositionKeys[k].mValue.y,
                              ch->mPositionKeys[k].mValue.z) });

            for (uint32_t k = 0; k < ch->mNumRotationKeys; k++)
                bc.rotKeys.push_back({ static_cast<float>(ch->mRotationKeys[k].mTime),
                    glm::quat(ch->mRotationKeys[k].mValue.w,
                              ch->mRotationKeys[k].mValue.x,
                              ch->mRotationKeys[k].mValue.y,
                              ch->mRotationKeys[k].mValue.z) });

            for (uint32_t k = 0; k < ch->mNumScalingKeys; k++)
                bc.scaleKeys.push_back({ static_cast<float>(ch->mScalingKeys[k].mTime),
                    glm::vec3(ch->mScalingKeys[k].mValue.x,
                              ch->mScalingKeys[k].mValue.y,
                              ch->mScalingKeys[k].mValue.z) });

            clip.channels.push_back(std::move(bc));
        }

        animations.push_back(std::move(clip));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Keyframe interpolation
// ─────────────────────────────────────────────────────────────────────────────
glm::vec3 VulkanMesh::InterpolatePosition(float t, const BoneChannel& ch)
{
    if (ch.posKeys.empty()) return glm::vec3(0.0f);
    if (ch.posKeys.size() == 1) return ch.posKeys[0].value;
    uint32_t i = FindKeyV(t, ch.posKeys);
    uint32_t j = std::min(i + 1, static_cast<uint32_t>(ch.posKeys.size()) - 1);
    float dt   = ch.posKeys[j].time - ch.posKeys[i].time;
    float fac  = (dt > 0.0f) ? (t - ch.posKeys[i].time) / dt : 0.0f;
    return glm::mix(ch.posKeys[i].value, ch.posKeys[j].value, glm::clamp(fac, 0.0f, 1.0f));
}

glm::quat VulkanMesh::InterpolateRotation(float t, const BoneChannel& ch)
{
    if (ch.rotKeys.empty()) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    if (ch.rotKeys.size() == 1) return ch.rotKeys[0].value;
    uint32_t i = FindKeyQ(t, ch.rotKeys);
    uint32_t j = std::min(i + 1, static_cast<uint32_t>(ch.rotKeys.size()) - 1);
    float dt   = ch.rotKeys[j].time - ch.rotKeys[i].time;
    float fac  = (dt > 0.0f) ? (t - ch.rotKeys[i].time) / dt : 0.0f;
    return glm::normalize(glm::slerp(ch.rotKeys[i].value, ch.rotKeys[j].value,
                                     glm::clamp(fac, 0.0f, 1.0f)));
}

glm::vec3 VulkanMesh::InterpolateScale(float t, const BoneChannel& ch)
{
    if (ch.scaleKeys.empty()) return glm::vec3(1.0f);
    if (ch.scaleKeys.size() == 1) return ch.scaleKeys[0].value;
    uint32_t i = FindKeyV(t, ch.scaleKeys);
    uint32_t j = std::min(i + 1, static_cast<uint32_t>(ch.scaleKeys.size()) - 1);
    float dt   = ch.scaleKeys[j].time - ch.scaleKeys[i].time;
    float fac  = (dt > 0.0f) ? (t - ch.scaleKeys[i].time) / dt : 0.0f;
    return glm::mix(ch.scaleKeys[i].value, ch.scaleKeys[j].value, glm::clamp(fac, 0.0f, 1.0f));
}

// ─────────────────────────────────────────────────────────────────────────────
// Hierarchy traversal — fills out[] with world-space bone matrices
// ─────────────────────────────────────────────────────────────────────────────
void VulkanMesh::ProcessNodeHierarchy(uint32_t nodeIdx, const glm::mat4& parentTransform,
                                       const AnimationClip* clip, float timeTicks,
                                       std::vector<glm::mat4>& out)
{
    const SceneNode& node = nodes[nodeIdx];
    glm::mat4 localTransform = node.localTransform;

    if (clip)
    {
        for (const auto& ch : clip->channels)
        {
            if (ch.nodeName == node.name)
            {
                glm::vec3 pos   = InterpolatePosition(timeTicks, ch);
                glm::quat rot   = InterpolateRotation(timeTicks, ch);
                glm::vec3 scale = InterpolateScale   (timeTicks, ch);

                localTransform = glm::translate(glm::mat4(1.0f), pos)
                               * glm::mat4_cast(rot)
                               * glm::scale(glm::mat4(1.0f), scale);
                break;
            }
        }
    }

    glm::mat4 globalTransform = parentTransform * localTransform;

    if (node.boneIndex >= 0 && static_cast<uint32_t>(node.boneIndex) < out.size())
        out[node.boneIndex] = globalInverseTransform
                            * globalTransform
                            * boneInfos[node.boneIndex].offsetMatrix;

    for (uint32_t child : node.children)
        ProcessNodeHierarchy(child, globalTransform, clip, timeTicks, out);
}

// ─────────────────────────────────────────────────────────────────────────────
// ComputeBoneTransforms  — public entry point called each frame
// ─────────────────────────────────────────────────────────────────────────────
void VulkanMesh::ComputeBoneTransforms(uint32_t clipIndex, float timeSecs,
                                        std::vector<glm::mat4>& out)
{
    out.assign(boneInfos.size(), glm::mat4(1.0f));
    if (boneInfos.empty() || nodes.empty()) return;

    const AnimationClip* clip      = nullptr;
    float                timeTicks = 0.0f;

    if (clipIndex < static_cast<uint32_t>(animations.size()))
    {
        clip      = &animations[clipIndex];
        timeTicks = timeSecs * clip->ticksPerSecond;
        if (clip->duration > 0.0f)
            timeTicks = std::fmod(timeTicks, clip->duration);
    }

    ProcessNodeHierarchy(rootNodeIndex, glm::mat4(1.0f), clip, timeTicks, out);
}

// ─────────────────────────────────────────────────────────────────────────────
// Draw / Destroy
// ─────────────────────────────────────────────────────────────────────────────
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

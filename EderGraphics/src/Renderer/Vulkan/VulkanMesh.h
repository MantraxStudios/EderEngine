#pragma once
#include "ImportCore.h"
#include "VulkanBuffer.h"
#include "../../Core/Vertex.h"
#include <cfloat>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <map>
#include <string>

// ── Animation keyframe types ─────────────────────────────────────────────────
struct AnimKeyVec3 { float time; glm::vec3 value; };
struct AnimKeyQuat { float time; glm::quat value; };

struct BoneChannel
{
    std::string              nodeName;
    std::vector<AnimKeyVec3> posKeys;
    std::vector<AnimKeyQuat> rotKeys;
    std::vector<AnimKeyVec3> scaleKeys;
};

struct AnimationClip
{
    std::string              name;
    float                    duration       = 0.0f;   // in ticks
    float                    ticksPerSecond = 25.0f;
    std::vector<BoneChannel> channels;
};

// ── Skeleton types ────────────────────────────────────────────────────────────
struct BoneInfo
{
    glm::mat4 offsetMatrix = glm::mat4(1.0f);  // inverse bind-pose matrix
};

struct SceneNode
{
    std::string           name;
    glm::mat4             localTransform = glm::mat4(1.0f);
    std::vector<uint32_t> children;
    int                   boneIndex = -1;  // -1 if this node is not a bone
};

class EDERGRAPHICS_API VulkanMesh
{
public:
    // Load from a file path.  Internally routes through AssetManager when available.
    void Load           (const std::string& path);

    // Load from raw bytes already in memory (e.g. from PAK / AssetManager).
    // `hint` is a virtual filename with extension so Assimp can pick the right parser
    // (e.g. "model.fbx", "scene.glb"). Leave empty to let Assimp auto-detect.
    void LoadFromMemory (const uint8_t* data, size_t size, const std::string& hint = "");

    void DrawInstanced  (vk::CommandBuffer cmd, uint32_t firstInstance, uint32_t instanceCount);
    void Destroy        ();

    uint32_t GetIndexCount()  const { return indexCount; }
    uint32_t GetVertexCount() const { return vertexCount; }

    // ── Animation queries ──────────────────────────────────────────────────    // ── Bounds (local space, set during load) ─────────────────────────────
    /// Returns the axis-aligned bounding box in local/object space.
    /// For static meshes the mesh is normalised to [-0.5, +0.5] per axis.
    glm::vec3 GetBoundsMin() const { return m_boundsMin; }
    glm::vec3 GetBoundsMax() const { return m_boundsMax; }

    // Raw geometry access — used by PhysicsSystem to cook triangle mesh colliders.
    const std::vector<Vertex>&    GetVertices() const { return vertices; }
    const std::vector<uint32_t>&  GetIndices()  const { return indices;  }
    uint32_t    GetBoneCount      () const { return static_cast<uint32_t>(boneInfos.size()); }
    uint32_t    GetAnimationCount () const { return static_cast<uint32_t>(animations.size()); }
    std::string GetAnimationName  (uint32_t idx) const
        { return (idx < animations.size()) ? animations[idx].name : ""; }
    float       GetAnimationDuration(uint32_t idx) const
        { return (idx < animations.size())
            ? animations[idx].duration / std::max(animations[idx].ticksPerSecond, 0.001f)
            : 0.0f; }

    // Fill `out` with one mat4 per bone every frame to drive skinning.
    void ComputeBoneTransforms(uint32_t clipIndex, float timeSecs, std::vector<glm::mat4>& out);

private:
    void _LoadFromPath       (const std::string& path);
    void ProcessNode (aiNode* node, const aiScene* scene, const aiMatrix4x4& parentTransform);
    void ProcessMesh (aiMesh* mesh, const aiScene* scene, const aiMatrix4x4& nodeTransform);
    void BuildNodeTree       (aiNode* node, uint32_t parentIdx);
    void LoadAnimations      (const aiScene* scene);
    void ProcessNodeHierarchy(uint32_t nodeIdx, const glm::mat4& parentTransform,
                               const AnimationClip* clip, float timeTicks,
                               std::vector<glm::mat4>& out);

    glm::vec3 InterpolatePosition(float timeTicks, const BoneChannel& ch);
    glm::quat InterpolateRotation(float timeTicks, const BoneChannel& ch);
    glm::vec3 InterpolateScale   (float timeTicks, const BoneChannel& ch);

    VulkanBuffer vertexBuffer;
    VulkanBuffer indexBuffer;

    std::vector<Vertex>   vertices;
    std::vector<uint32_t> indices;

    uint32_t vertexCount = 0;
    uint32_t indexCount  = 0;

    // ── Skeleton / animation ───────────────────────────────────────────────
    std::map<std::string, uint32_t>  boneNameToIndex;
    std::vector<BoneInfo>            boneInfos;
    std::vector<AnimationClip>       animations;
    std::vector<SceneNode>           nodes;
    uint32_t                         rootNodeIndex = 0;
    std::map<std::string, uint32_t>  nodeNameToIndex;
    glm::mat4                        globalInverseTransform = glm::mat4(1.0f);

    // ── AABB ───────────────────────────────────────────────────────────────
    glm::vec3 m_boundsMin = glm::vec3( FLT_MAX);
    glm::vec3 m_boundsMax = glm::vec3(-FLT_MAX);
};

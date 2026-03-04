#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec3 inTangent;
layout(location = 4) in vec3 inBitangent;
layout(location = 5) in vec4 inColor;

// Skeletal animation: bone indices + blend weights (0 = no skinning, static mesh)
layout(location = 6) in uvec4 inBoneIndices;
layout(location = 7) in vec4  inBoneWeights;

// Instance data — model matrix cols (binding 1, instance rate)
layout(location = 8)  in vec4 instanceModelCol0;
layout(location = 9)  in vec4 instanceModelCol1;
layout(location = 10) in vec4 instanceModelCol2;
layout(location = 11) in vec4 instanceModelCol3;

layout(push_constant) uniform PushConstants {
    mat4 viewProj;
} push;

layout(set = 0, binding = 0) uniform MaterialUBO {
    vec4  albedo;
    float roughness;
    float metallic;
    float emissiveIntensity;
    float alphaThreshold;  // 0 = disabled; >0 = cutout (discard below threshold)
} material;

// Bone matrices SSBO — one mat4 per bone, up to 256 bones
layout(set = 2, binding = 0) readonly buffer BoneBuffer {
    mat4 boneMatrices[];
} bones;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec2 fragUV;
layout(location = 2) out vec4 fragColor;
layout(location = 3) out float fragRoughness;
layout(location = 4) out float fragMetallic;
layout(location = 5) out float fragEmissive;
layout(location = 6) out vec3 fragWorldPos;

void main()
{
    mat4 model = mat4(instanceModelCol0, instanceModelCol1, instanceModelCol2, instanceModelCol3);

    // Skeletal skinning — only applied when at least one weight > 0.
    // For static meshes all weights are 0, so they pass through unchanged.
    float totalWeight = inBoneWeights.x + inBoneWeights.y + inBoneWeights.z + inBoneWeights.w;

    vec4 localPos;
    vec3 localNormal;

    if (totalWeight > 0.0001)
    {
        mat4 skinMat =
            inBoneWeights.x * bones.boneMatrices[inBoneIndices.x] +
            inBoneWeights.y * bones.boneMatrices[inBoneIndices.y] +
            inBoneWeights.z * bones.boneMatrices[inBoneIndices.z] +
            inBoneWeights.w * bones.boneMatrices[inBoneIndices.w];

        localPos    = skinMat * vec4(inPosition, 1.0);
        localNormal = mat3(transpose(inverse(skinMat))) * inNormal;
    }
    else
    {
        localPos    = vec4(inPosition, 1.0);
        localNormal = inNormal;
    }

    vec4 worldPos = model * localPos;
    gl_Position   = push.viewProj * worldPos;
    fragNormal    = normalize(mat3(transpose(inverse(model))) * localNormal);
    fragUV        = inUV;
    fragColor     = inColor * material.albedo;
    fragRoughness = material.roughness;
    fragMetallic  = material.metallic;
    fragEmissive  = material.emissiveIntensity;
    fragWorldPos  = worldPos.xyz;
}

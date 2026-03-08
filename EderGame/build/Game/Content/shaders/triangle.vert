#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec3 inTangent;
layout(location = 4) in vec3 inBitangent;
layout(location = 5) in vec4 inColor;

layout(location = 6) in uvec4 inBoneIndices;
layout(location = 7) in vec4  inBoneWeights;

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
    float alphaThreshold;  
} material;

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

    // Precalcular la normal matrix del modelo una sola vez
    // evita recalcularla dos veces (una por skinning, otra al final)
    mat3 modelNormalMat = mat3(transpose(inverse(model)));

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

        localPos = skinMat * vec4(inPosition, 1.0);

        // Usar solo mat3(skinMat) para rotar la normal por los huesos
        // sin inverse(): skinMat ya es ortonormal si los bones estan bien normalizados
        localNormal = mat3(skinMat) * inNormal;
    }
    else
    {
        localPos    = vec4(inPosition, 1.0);
        localNormal = inNormal;
    }

    vec4 worldPos = model * localPos;
    gl_Position   = push.viewProj * worldPos;

    // Aplicar la normal matrix del modelo (calculada una sola vez arriba)
    fragNormal    = normalize(modelNormalMat * localNormal);
    fragUV        = inUV;
    fragColor     = inColor * material.albedo;
    fragRoughness = material.roughness;
    fragMetallic  = material.metallic;
    fragEmissive  = material.emissiveIntensity;
    fragWorldPos  = worldPos.xyz;
}
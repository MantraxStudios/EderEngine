#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec3 inTangent;
layout(location = 4) in vec3 inBitangent;
layout(location = 5) in vec4 inColor;

layout(location = 6) in vec4 instanceModelCol0;
layout(location = 7) in vec4 instanceModelCol1;
layout(location = 8) in vec4 instanceModelCol2;
layout(location = 9) in vec4 instanceModelCol3;

layout(push_constant) uniform PushConstants {
    mat4 viewProj;
} push;

layout(set = 0, binding = 0) uniform MaterialUBO {
    vec4  albedo;
    float roughness;
    float metallic;
    float emissiveIntensity;
    float _pad;
} material;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec2 fragUV;
layout(location = 2) out vec4 fragColor;
layout(location = 3) out float fragRoughness;
layout(location = 4) out float fragMetallic;
layout(location = 5) out float fragEmissive;
layout(location = 6) out vec3 fragWorldPos;

void main()
{
    mat4 model    = mat4(instanceModelCol0, instanceModelCol1, instanceModelCol2, instanceModelCol3);
    vec4 worldPos = model * vec4(inPosition, 1.0);
    gl_Position   = push.viewProj * worldPos;
    fragNormal    = normalize(mat3(transpose(inverse(model))) * inNormal);
    fragUV        = inUV;
    fragColor     = inColor * material.albedo;
    fragRoughness = material.roughness;
    fragMetallic  = material.metallic;
    fragEmissive  = material.emissiveIntensity;
    fragWorldPos  = worldPos.xyz;
}

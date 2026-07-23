#version 450

// Deferred geometry pass — writes the G-buffer. Shares triangle.vert and the
// material descriptor set (set 0) with the forward pipeline.

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragUV;
layout(location = 2) in vec4 fragColor;
layout(location = 3) in float fragRoughness;
layout(location = 4) in float fragMetallic;
layout(location = 5) in float fragEmissive;
layout(location = 6) in vec3 fragWorldPos;
layout(location = 7) in vec3 fragTangent;
layout(location = 8) in vec3 fragBitangent;

layout(set = 0, binding = 1) uniform sampler2D albedoTex;
layout(set = 0, binding = 2) uniform sampler2D normalTex;
layout(set = 0, binding = 3) uniform sampler2D roughMetalTex;
layout(set = 0, binding = 4) uniform sampler2D emissiveTex;

layout(set = 0, binding = 0) uniform MaterialUBO {
    vec4  albedo;
    float roughness;
    float metallic;
    float emissiveIntensity;
    float alphaThreshold;
    float hasNormalMap;
    float hasRoughMap;
    float hasEmissiveMap;
} material;

layout(location = 0) out vec4 outAlbedo;    // rgb = albedo, a = emissiveIntensity
layout(location = 1) out vec4 outNormal;    // rgb = world-space normal
layout(location = 2) out vec4 outMaterial;  // r = roughness, g = metallic

void main()
{
    vec4  albedoSample = texture(albedoTex, fragUV);
    float alpha        = fragColor.a * albedoSample.a;
    if (material.alphaThreshold > 0.0 && alpha < material.alphaThreshold)
        discard;

    vec3  baseColor = fragColor.rgb * albedoSample.rgb;

    float facing = gl_FrontFacing ? 1.0 : -1.0;
    vec3  Ng     = normalize(fragNormal) * facing;
    vec3  N      = Ng;
    if (material.hasNormalMap > 0.5)
    {
        vec3 T  = normalize(fragTangent);
        vec3 B  = normalize(fragBitangent) * facing;
        vec3 ts = texture(normalTex, fragUV).xyz * 2.0 - 1.0;
        N = normalize(mat3(T, B, Ng) * ts);
    }

    float roughness = fragRoughness;
    float metallic  = fragMetallic;
    if (material.hasRoughMap > 0.5)
    {
        vec3 rm    = texture(roughMetalTex, fragUV).rgb;
        roughness *= rm.g;
        metallic  *= rm.b;
    }

    outAlbedo   = vec4(baseColor, clamp(fragEmissive, 0.0, 1.0));
    outNormal   = vec4(N, 1.0);
    outMaterial = vec4(clamp(roughness, 0.0, 1.0), clamp(metallic, 0.0, 1.0), 0.0, 1.0);
}

#version 450

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragUV;
layout(location = 2) in vec4 fragColor;
layout(location = 3) in float fragRoughness;
layout(location = 4) in float fragMetallic;
layout(location = 5) in float fragEmissive;

layout(set = 0, binding = 0) uniform MaterialUBO {
    vec4  albedo;
    float roughness;
    float metallic;
    float emissiveIntensity;
    float _pad;
} material;

layout(set = 0, binding = 1) uniform sampler2D albedoTex;

layout(location = 0) out vec4 outColor;

void main()
{
    vec3  normal    = normalize(fragNormal);
    
    // Muestrear textura
    vec4  texColor  = texture(albedoTex, fragUV);
    
    // Usar material color * textura
    vec4  baseColor = material.albedo * texColor;
    
    // Si la textura es completamente negra, usar material color
    if (length(texColor.rgb) < 0.01)
        baseColor = material.albedo;
    
    // Lighting
    vec3 lightDir = normalize(vec3(1.0, 2.0, 1.0));
    float diffuse = max(dot(normal, lightDir), 0.0);
    float ambient = 0.5;
    float light   = ambient + diffuse * 0.6;
    
    vec3 lit = baseColor.rgb * light;
    outColor = vec4(lit, baseColor.a);
}

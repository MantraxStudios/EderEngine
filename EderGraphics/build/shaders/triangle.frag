#version 450

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragUV;
layout(location = 2) in vec4 fragColor;
layout(location = 3) in float fragRoughness;
layout(location = 4) in float fragMetallic;
layout(location = 5) in float fragEmissive;

layout(location = 0) out vec4 outColor;

void main()
{
    vec3  normal    = normalize(fragNormal);
    float diffuse   = max(dot(normal, normalize(vec3(0.4, 1.0, 0.6))), 0.0);
    float ambient   = 0.08 + fragMetallic * 0.04;
    float light     = ambient + diffuse * (1.0 - fragRoughness * 0.5);
    vec3  baseColor = fragColor.rgb * light;
    vec3  emissive  = fragColor.rgb * fragEmissive;
    outColor        = vec4(baseColor + emissive, fragColor.a);
}

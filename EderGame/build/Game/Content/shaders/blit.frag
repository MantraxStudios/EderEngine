#version 450

// Final present blit (player path). The scene arrives as LINEAR HDR, so this is
// where the single display transform happens: exposure -> ACES filmic -> gamma.

layout(set = 0, binding = 0) uniform sampler2D src;

layout(location = 0) in  vec2 fragUV;
layout(location = 0) out vec4 outColor;

vec3 ACESFilm(vec3 x)
{
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main()
{
    vec3 hdr    = texture(src, fragUV).rgb;
    vec3 mapped = ACESFilm(hdr * 1.15);
    outColor    = vec4(pow(mapped, vec3(1.0 / 2.2)), 1.0);
}

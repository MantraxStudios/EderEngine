#version 450

// ─────────────────────────────────────────────────────────────────────────────
//  grain.frag — film grain (single pass, static)
//
//  Adds hashed per-pixel noise. There is no time uniform in the post-process
//  contract, so the grain pattern is static (a fixed noise overlay) rather than
//  animated frame-to-frame.
//
//  paramCount = 2
//    p[0].x = intensity (0..0.5, default 0.08)
//    p[0].y = size      (0.5..4, default 1.0)   larger = coarser grain
// ─────────────────────────────────────────────────────────────────────────────

layout(set = 0, binding = 0) uniform sampler2D scene;
layout(set = 0, binding = 1) uniform sampler2D depthTex;

layout(std140, set = 0, binding = 2) uniform PPParams {
    vec4 p[4];
};

layout(location = 0) in  vec2 fragUV;
layout(location = 0) out vec4 outColor;

float hash(vec2 v)
{
    return fract(sin(dot(v, vec2(12.9898, 78.233))) * 43758.5453);
}

void main()
{
    vec3  col       = texture(scene, fragUV).rgb;
    float intensity = p[0].x;
    float size      = max(p[0].y, 0.001);

    vec2  cell  = floor(fragUV * vec2(textureSize(scene, 0)) / size);
    float n     = hash(cell) - 0.5;

    // Grain is more visible in the mid-tones, less in highlights/shadows.
    float luma  = dot(col, vec3(0.2126, 0.7152, 0.0722));
    float mask  = 1.0 - abs(luma - 0.5) * 2.0;

    outColor = vec4(clamp(col + n * intensity * mask, 0.0, 1.0), 1.0);
}

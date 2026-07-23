#version 450

// ─────────────────────────────────────────────────────────────────────────────
//  scanlines.frag — CRT-style scanlines (single pass)
//
//  paramCount = 2
//    p[0].x = count     (100..1080, default 400)  number of dark lines
//    p[0].y = intensity (0..0.6,    default 0.15) darkening strength
// ─────────────────────────────────────────────────────────────────────────────

layout(set = 0, binding = 0) uniform sampler2D scene;
layout(set = 0, binding = 1) uniform sampler2D depthTex;

layout(std140, set = 0, binding = 2) uniform PPParams {
    vec4 p[4];
};

layout(location = 0) in  vec2 fragUV;
layout(location = 0) out vec4 outColor;

const float PI = 3.14159265;

void main()
{
    vec3  col       = texture(scene, fragUV).rgb;
    float count     = max(p[0].x, 1.0);
    float intensity = p[0].y;

    float line = sin(fragUV.y * count * PI * 2.0) * 0.5 + 0.5;
    col *= 1.0 - line * intensity;

    outColor = vec4(col, 1.0);
}

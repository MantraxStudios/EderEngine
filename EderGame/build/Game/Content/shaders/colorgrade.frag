#version 450

// ─────────────────────────────────────────────────────────────────────────────
//  colorgrade.frag — Tone & Color grading (single pass)
//
//  paramCount = 8
//    p[0].x = exposure     (0..4,   default 1.0)   linear exposure multiplier
//    p[0].y = contrast     (0..2,   default 1.0)
//    p[0].z = saturation   (0..2,   default 1.0)
//    p[0].w = temperature  (-1..1,  default 0.0)   warm (+) / cool (-)
//    p[1].x = tint         (-1..1,  default 0.0)   magenta (+) / green (-)
//    p[1].y = tonemap mode (0..2,   default 2.0)   0 = off, 1 = Reinhard, 2 = ACES
//    p[1].z = gamma        (0.5..2.5, default 1.0)
//    p[1].w = brightness   (-0.5..0.5, default 0.0)
// ─────────────────────────────────────────────────────────────────────────────

layout(set = 0, binding = 0) uniform sampler2D scene;
layout(set = 0, binding = 1) uniform sampler2D depthTex;

layout(std140, set = 0, binding = 2) uniform PPParams {
    vec4 p[4];
};

layout(location = 0) in  vec2 fragUV;
layout(location = 0) out vec4 outColor;

// Narkowicz 2015, "ACES Filmic Tone Mapping Curve"
vec3 ACESFilm(vec3 x)
{
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main()
{
    vec3  col        = texture(scene, fragUV).rgb;

    float exposure   = p[0].x;
    float contrast   = p[0].y;
    float saturation = p[0].z;
    float temperature= p[0].w;
    float tint       = p[1].x;
    int   toneMode   = int(p[1].y + 0.5);
    float gamma      = max(p[1].z, 0.001);
    float brightness = p[1].w;

    // Exposure + brightness
    col *= exposure;
    col += brightness;

    // White-balance (temperature/tint) — cheap channel scaling
    col.r *= 1.0 + temperature * 0.20;
    col.b *= 1.0 - temperature * 0.20;
    col.g *= 1.0 + tint        * 0.20;

    // Tonemapping (HDR -> LDR)
    if (toneMode == 1)          // Reinhard
        col = col / (col + vec3(1.0));
    else if (toneMode == 2)     // ACES
        col = ACESFilm(col);

    // Contrast around mid-grey
    col = (col - 0.5) * contrast + 0.5;

    // Saturation
    float luma = dot(col, vec3(0.2126, 0.7152, 0.0722));
    col = mix(vec3(luma), col, saturation);

    // Gamma adjust
    col = pow(max(col, 0.0), vec3(1.0 / gamma));

    outColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}

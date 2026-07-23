#version 450

// ─────────────────────────────────────────────────────────────────────────────
//  bloom.frag — single-pass approximate bloom
//
//  A true bloom is multi-pass (bright-extract -> down/upsample blur -> combine).
//  This is a cheap single-pass approximation: a wide weighted tap kernel keeps
//  only the pixels above `threshold`, blurs them, and adds the glow back.
//  Good enough for a stylised glow; not a physically-wide bloom.
//
//  paramCount = 3
//    p[0].x = threshold  (0..2,  default 0.8)   luminance cutoff for glow
//    p[0].y = intensity  (0..3,  default 0.6)   glow strength added back
//    p[0].z = radius     (1..8,  default 3.0)   tap spread in texels
// ─────────────────────────────────────────────────────────────────────────────

layout(set = 0, binding = 0) uniform sampler2D scene;
layout(set = 0, binding = 1) uniform sampler2D depthTex;

layout(std140, set = 0, binding = 2) uniform PPParams {
    vec4 p[4];
};

layout(location = 0) in  vec2 fragUV;
layout(location = 0) out vec4 outColor;

float luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

vec3 bright(vec2 uv, float threshold)
{
    vec3 c = texture(scene, uv).rgb;
    float l = luma(c);
    return c * max(l - threshold, 0.0) / max(l, 1e-4);
}

void main()
{
    vec3  base      = texture(scene, fragUV).rgb;
    float threshold = p[0].x;
    float intensity = p[0].y;
    float radius    = max(p[0].z, 0.5);

    vec2 texel = radius / vec2(textureSize(scene, 0));

    // 13-tap weighted glow gather (separable-ish approximation).
    vec3  glow  = vec3(0.0);
    float wsum  = 0.0;
    for (int x = -2; x <= 2; ++x)
    for (int y = -2; y <= 2; ++y)
    {
        vec2  off = vec2(float(x), float(y));
        float w   = exp(-dot(off, off) * 0.5);
        glow += bright(fragUV + off * texel, threshold) * w;
        wsum += w;
    }
    glow /= max(wsum, 1e-4);

    outColor = vec4(base + glow * intensity, 1.0);
}

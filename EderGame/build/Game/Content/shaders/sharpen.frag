#version 450

// ─────────────────────────────────────────────────────────────────────────────
//  sharpen.frag — unsharp-mask sharpening (single pass)
//
//  paramCount = 1
//    p[0].x = amount (0..2, default 0.4)
// ─────────────────────────────────────────────────────────────────────────────

layout(set = 0, binding = 0) uniform sampler2D scene;
layout(set = 0, binding = 1) uniform sampler2D depthTex;

layout(std140, set = 0, binding = 2) uniform PPParams {
    vec4 p[4];
};

layout(location = 0) in  vec2 fragUV;
layout(location = 0) out vec4 outColor;

void main()
{
    float amount = p[0].x;
    vec2  texel  = 1.0 / vec2(textureSize(scene, 0));

    vec3 c  = texture(scene, fragUV).rgb;
    vec3 n  = texture(scene, fragUV + vec2( texel.x, 0)).rgb
            + texture(scene, fragUV + vec2(-texel.x, 0)).rgb
            + texture(scene, fragUV + vec2(0,  texel.y)).rgb
            + texture(scene, fragUV + vec2(0, -texel.y)).rgb;

    // Unsharp mask: c + amount * (c - blur)
    vec3 blur = n * 0.25;
    vec3 res  = c + (c - blur) * amount;

    outColor = vec4(clamp(res, 0.0, 1.0), 1.0);
}

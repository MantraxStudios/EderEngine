#version 450

// ─────────────────────────────────────────────────────────────────────────────
//  dof.frag — single-pass approximate depth-of-field
//
//  Blurs the scene by an amount proportional to the distance of each pixel's
//  depth from a focus plane. Uses the raw (non-linear) depth buffer, so the
//  focus value is tuned in depth-buffer space [0..1], not world units — a true
//  DoF would linearise depth and do a multi-pass circle-of-confusion gather.
//
//  paramCount = 3
//    p[0].x = focusDepth (0..1,   default 0.5)   depth kept sharp
//    p[0].y = focusRange (0.001..0.3, default 0.02) sharp band half-width
//    p[0].z = blurAmount (0..8,   default 3.0)   max tap radius in texels
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
    float focus = p[0].x;
    float range = max(p[0].y, 0.0001);
    float maxR  = p[0].z;

    float depth = texture(depthTex, fragUV).r;
    float coc   = clamp((abs(depth - focus) - range) / range, 0.0, 1.0);
    float rad   = coc * maxR;

    if (rad < 0.5)
    {
        outColor = vec4(texture(scene, fragUV).rgb, 1.0);
        return;
    }

    vec2 texel = rad / vec2(textureSize(scene, 0));

    // Weighted disc blur (5x5 gaussian-ish).
    vec3  acc  = vec3(0.0);
    float wsum = 0.0;
    for (int x = -2; x <= 2; ++x)
    for (int y = -2; y <= 2; ++y)
    {
        vec2  off = vec2(float(x), float(y));
        float w   = exp(-dot(off, off) * 0.4);
        acc  += texture(scene, fragUV + off * texel).rgb * w;
        wsum += w;
    }

    outColor = vec4(acc / max(wsum, 1e-4), 1.0);
}

#version 450

// ─────────────────────────────────────────────────────────────────────────────
//  chromatic.frag — chromatic aberration (single pass)
//
//  Offsets the R and B channels radially from the screen centre, growing toward
//  the edges — the classic lens-fringe look.
//
//  paramCount = 2
//    p[0].x = amount      (0..0.02, default 0.004)  max UV offset at the edge
//    p[0].y = edgeFalloff (0..3,    default 1.5)    how quickly it ramps outward
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
    float falloff= max(p[0].y, 0.0);

    vec2  dir  = fragUV - 0.5;
    float edge = pow(clamp(length(dir) * 2.0, 0.0, 1.0), falloff);
    vec2  off  = dir * amount * edge;

    float r = texture(scene, fragUV + off).r;
    float g = texture(scene, fragUV      ).g;
    float b = texture(scene, fragUV - off).b;

    outColor = vec4(r, g, b, 1.0);
}

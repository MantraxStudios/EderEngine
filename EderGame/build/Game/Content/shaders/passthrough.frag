#version 450

// ─────────────────────────────────────────────────────────────────────────────
//  passthrough.frag
//  Default post-process effect: passes the scene through unchanged.
//  Use this as a starting point for custom effects.
//
//  Bindings (same for ALL custom post-process effects):
//    binding 0  : scene color  (sampler2D)
//    binding 1  : scene depth  (sampler2D)
//    binding 2  : params UBO   (16 floats as vec4[4])
//
//  params layout (for this shader — paramCount = 0, nothing used):
//    p[0].x = unused
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
    outColor = texture(scene, fragUV);
}

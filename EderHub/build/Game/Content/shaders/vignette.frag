#version 450

// ─────────────────────────────────────────────────────────────────────────────
//  vignette.frag
//  Example custom post-process: vignette + optional desaturation.
//
//  params (paramCount = 3):
//    p[0].x = vignetteStrength  (0 = none, 1 = full)
//    p[0].y = vignetteSoftness  (0.1 to 0.8 recommended)
//    p[0].z = desaturation      (0 = color, 1 = grayscale)
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
    vec4  color      = texture(scene, fragUV);
    float vStrength  = p[0].x;
    float vSoftness  = max(p[0].y, 0.001);
    float desat      = clamp(p[0].z, 0.0, 1.0);

    // Vignette
    vec2  uv         = fragUV - 0.5;
    float vignette   = smoothstep(vStrength, vStrength - vSoftness, length(uv));
    color.rgb       *= mix(1.0, vignette, vStrength);

    // Desaturation
    float luma       = dot(color.rgb, vec3(0.299, 0.587, 0.114));
    color.rgb        = mix(color.rgb, vec3(luma), desat);

    outColor = color;
}

#version 450
#define PI 3.14159265358979323846

layout(location = 0) in  vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D sceneTex;
layout(set = 0, binding = 1) uniform sampler2D depthTex;

layout(push_constant) uniform Push
{
    vec2  sunUV;        // sun screen UV [0,1]
    float decay;        // per-step decay      (~0.96–0.98)
    float weight;       // per-sample weight   (~0.01–0.04)
    float exposure;     // final brightness
    float intensity;    // overall multiplier
    vec3  tint;         // shaft / glare tint
    float sunHeight;    // sun world-Y: >0 above horizon, <0 below
};

#define NUM_SAMPLES 80

// ---- Low-quality hash for jitter ----
float hash21(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

void main()
{
    vec3 scene = texture(sceneTex, fragUV).rgb;

    if (intensity <= 0.0) { outColor = vec4(scene, 1.0); return; }

    // Aspect-corrected pixel and sun positions (so glare is circular, not oval)
    vec2  res    = vec2(textureSize(sceneTex, 0));
    float aspect = res.x / res.y;

    vec2 pxCorr  = vec2((fragUV.x - 0.5) * aspect, fragUV.y - 0.5);
    vec2 sunCorr = vec2((sunUV.x  - 0.5) * aspect, sunUV.y  - 0.5);
    float distToSun = length(pxCorr - sunCorr);

    // sunVisible: full inside screen, fades only in outer 15% near edges, and at horizon
    float sunAbove   = clamp(sunHeight * 8.0 + 1.0, 0.0, 1.0);
    float mx         = max(abs(sunUV.x - 0.5), abs(sunUV.y - 0.5));   // 0=center, 0.5=edge
    float onScreen   = clamp(1.0 - (mx - 0.35) / 0.15, 0.0, 1.0);    // full until 0.35, 0 at 0.5
    float sunVisible = sunAbove * onScreen;

    // ----------------------------------------------------------------
    //  God rays (crepuscular rays)
    // ----------------------------------------------------------------
    vec2  uv     = fragUV;
    vec2  delta  = (uv - sunUV) / float(NUM_SAMPLES);
    // Per-pixel jitter to break hard banding
    float jitter = hash21(fragUV * 937.3 + 0.5) * 0.9 + 0.05;
    uv -= delta * jitter;

    float illum  = 1.0;
    float shafts = 0.0;

    for (int i = 0; i < NUM_SAMPLES; i++)
    {
        uv -= delta;
        vec2  suv   = clamp(uv, vec2(0.001), vec2(0.999));
        float depth = texture(depthTex, suv).r;
        float isSky = step(0.9999, depth);
        shafts += isSky * illum * weight;
        illum  *= decay;
    }

    // Color: sample actual sky color at the sun's screen position for natural shaft tint.
    // GPU Gems 3 spirit — the shafts inherit the sky's real color (warm at sunset, white at noon).
    // clamp() is safe: sunVisible will be 0 when sun is off-screen, zeroing the result.
    vec3  sunSkyColor = texture(sceneTex, clamp(sunUV, vec2(0.001), vec2(0.999))).rgb;
    float distFrac   = clamp(distToSun * 1.8, 0.0, 1.0);
    vec3  shaftTint  = mix(sunSkyColor * 1.6, tint, distFrac);
    // sunVisible (sunAbove * onScreen): zeroes shafts when sun is below horizon OR off-screen
    vec3 shaftColor = shafts * exposure * intensity * sunVisible * shaftTint;

    // ----------------------------------------------------------------
    //  Sun glare (only when sun is visible)
    // ----------------------------------------------------------------
    vec3  glare       = vec3(0.0);
    float glareStrength = sunVisible * exposure * intensity;

    if (glareStrength > 0.001)
    {
        float d  = distToSun;
        float d2 = d * d;

        // 1. Soft outer corona (wide smooth halo)
        float corona = exp(-d2 * 8.0) * 0.55;
        glare += mix(vec3(1.2, 0.9, 0.5), tint * 1.0, 0.4) * corona;

        // 2. Tight inner bloom
        float bloom = exp(-d2 * 60.0) * 1.1;
        glare += vec3(1.6, 1.4, 1.1) * bloom;

        // 3. Aperture starburst — 6 blades (camera hexagon aperture)
        vec2  toSun = pxCorr - sunCorr;
        float angle = atan(toSun.y, toSun.x);
        float star  = 0.0;
        for (int k = 0; k < 6; k++)
        {
            float ba = float(k) * (PI / 3.0);
            float a  = abs(cos(angle - ba));
            // Narrow spike that falls off radially
            star += pow(a, 220.0) * exp(-d * 5.5);
        }
        glare += vec3(1.4, 1.1, 0.7) * star * 0.30;

        // 4. Chromatic lens rings (3 halos at different radii)
        float r1 = smoothstep(0.04, 0.05, d) * smoothstep(0.08, 0.065, d);
        float r2 = smoothstep(0.10, 0.115, d) * smoothstep(0.145, 0.13, d);
        float r3 = smoothstep(0.17, 0.19, d)  * smoothstep(0.23,  0.21, d);
        glare += vec3(0.3, 0.7, 1.4) * r1 * 0.25;
        glare += vec3(1.1, 0.35, 0.15) * r2 * 0.18;
        glare += vec3(0.5, 1.1, 0.35) * r3 * 0.12;

        glare *= glareStrength;
    }

    // Combine: scene + god rays + glare
    outColor = vec4(scene + shaftColor + glare, 1.0);
}

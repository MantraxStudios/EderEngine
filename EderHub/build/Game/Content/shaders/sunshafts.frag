#version 450

layout(location = 0) in  vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D sceneTex;
layout(set = 0, binding = 1) uniform sampler2D occlusionTex;
layout(set = 0, binding = 2) uniform sampler2D depthTex;  // reserved, unused when bloom off

layout(push_constant) uniform Push
{
    layout(offset =  0) vec2  sunUV;
    layout(offset =  8) float decay;
    layout(offset = 12) float weight;
    layout(offset = 16) float exposure;
    layout(offset = 20) float density;
    layout(offset = 24) float bloomScale;
    layout(offset = 28) float sunHeight;
    layout(offset = 32) vec3  tint;
    layout(offset = 44) float aspect;  // viewport width / height
};

#define NUM_SAMPLES 100

void main()
{
    vec3  scene    = texture(sceneTex, fragUV).rgb;

    // Ramp down bloom as sun descends: full at sunHeight>=0.3, zero at horizon.
    float sunAbove = smoothstep(0.0, 0.30, sunHeight);

    if (sunAbove < 0.001)
    {
        outColor = vec4(scene, 1.0);
        return;
    }

    vec2  deltaUV = (sunUV - fragUV) * (density / float(NUM_SAMPLES));
    vec2  uv      = fragUV;

    float illuminationDecay = 1.0;
    float shafts            = 0.0;
    float validDecaySum     = 0.0;

    for (int i = 0; i < NUM_SAMPLES; i++)
    {
        uv += deltaUV;
        // Skip out-of-bounds samples (no edge accumulation) but keep marching
        // so high density values don't kill the effect by exiting early
        if (uv.x >= 0.0 && uv.x <= 1.0 && uv.y >= 0.0 && uv.y <= 1.0)
        {
            shafts        += texture(occlusionTex, uv).r * illuminationDecay;
            validDecaySum += illuminationDecay;
        }
        illuminationDecay *= decay;
    }

    // Normalize only against samples that were actually in-bounds
    float shaftsNorm = (validDecaySum > 0.001) ? shafts / validDecaySum : 0.0;

    // Radial proximity to the sun — aspect-corrected so it's circular on screen.
    vec2  dToSun    = fragUV - sunUV;
    dToSun.x       *= aspect;
    float distFromSun = length(dToSun);
    float proxFactor  = clamp(1.0 - distFromSun * 0.7, 0.0, 1.0);
    proxFactor *= proxFactor;

    // Depth factor: sky is already bright so only a tiny atmospheric tint;
    // geometry gets a stronger warm glow where shafts reach through.
    float currentDepth = texture(depthTex, fragUV).r;
    float isSkyPixel   = step(0.9999, currentDepth);
    float depthFactor  = mix(0.35, 0.06, isSkyPixel);

    float shaftStrength = clamp(pow(shaftsNorm, 0.45) * weight * exposure * sunAbove * proxFactor, 0.0, 3.0);

    vec3 sunColor = mix(vec3(1.1, 0.9, 0.55), tint, 0.35);
    outColor = vec4(scene + sunColor * shaftStrength * depthFactor, 1.0);
}
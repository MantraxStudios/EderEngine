#version 450

layout(location = 0) in  vec2 fragUV;
layout(location = 0) out float outOcclusion;

layout(set = 0, binding = 0) uniform sampler2D depthTex;

layout(push_constant) uniform Push
{
    layout(offset = 0) vec2  sunUV;
    layout(offset = 8) float sunRadius;
    layout(offset = 12) float aspect;  // viewport width / height
};

void main()
{
    float depth = texture(depthTex, fragUV).r;

    // Si hay geometria → negro (bloquea rayos). Si es cielo → fuente de luz.
    float isSky = step(0.9999, depth);

    // Aspect-correct UV distance so the sun disk appears circular on screen.
    // UV.x spans 'width' pixels; UV.y spans 'height' pixels.
    // Multiplying delta.x by aspect converts both axes to height-normalised units.
    vec2 delta = fragUV - sunUV;
    delta.x   *= aspect;
    float dist  = length(delta);

    // Disco visual del sol (controlado por sunRadius).
    // Sin exp falloff por distancia: cuando el sol esta fuera de pantalla
    // el cielo visible sigue aportando energia para los rayos de interior.
    // edge0 must be < edge1 per GLSL spec; use (1 - smoothstep) for the inverse.
    float sunDisk = 1.0 - smoothstep(sunRadius * 0.5, sunRadius, dist);

    // Gaussian glow centrado en el sol: solo el area cercana al sol aporta
    // energia a los rayos. Evita que todo el cielo blanco sea fuente uniforme.
    // Tighter falloff (40 instead of 20) + gated when sun is near/below horizon.
    float sunGlow = exp(-dist * dist * 40.0);

    // When sunUV is near the bottom edge (sun near horizon) reduce the glow
    // so it doesn't amplify the bright horizon band into runaway bloom.
    float horizonFade = clamp((0.85 - sunUV.y) * 10.0, 0.0, 1.0);

    outOcclusion = isSky * max(sunDisk, sunGlow * horizonFade);
}

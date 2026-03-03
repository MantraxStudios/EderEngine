#version 450

layout(location = 0) in  vec2 fragUV;
layout(location = 0) out float outOcclusion;

layout(set = 0, binding = 0) uniform sampler2D depthTex;

layout(push_constant) uniform Push
{
    layout(offset = 0) vec2  sunUV;
    layout(offset = 8) float sunRadius;
};

void main()
{
    float depth = texture(depthTex, fragUV).r;

    // Si hay geometria → negro (bloquea rayos). Si es cielo → fuente de luz.
    float isSky = step(0.9999, depth);

    float dist = length(fragUV - sunUV);

    // Disco visual del sol (controlado por sunRadius).
    // Sin exp falloff por distancia: cuando el sol esta fuera de pantalla
    // el cielo visible sigue aportando energia para los rayos de interior.
    float sunDisk = smoothstep(sunRadius, sunRadius * 0.5, dist);

    // Gaussian glow centrado en el sol: solo el area cercana al sol aporta
    // energia a los rayos. Evita que todo el cielo blanco sea fuente uniforme.
    float sunGlow = exp(-dist * dist * 20.0);

    outOcclusion = isSky * max(sunDisk, sunGlow);
}

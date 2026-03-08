#version 450

layout(location = 0) in  vec2 fragUV;
layout(location = 0) out float outOcclusion;

layout(set = 0, binding = 0) uniform sampler2D depthTex;

layout(push_constant) uniform Push
{
    layout(offset = 0) vec2  sunUV;
    layout(offset = 8) float sunRadius;
    layout(offset = 12) float aspect;  
};

void main()
{
    float depth = texture(depthTex, fragUV).r;

    
    float isSky = step(0.9999, depth);

    
    
    
    vec2 delta = fragUV - sunUV;
    delta.x   *= aspect;
    float dist  = length(delta);

    
    
    
    
    float sunDisk = 1.0 - smoothstep(sunRadius * 0.5, sunRadius, dist);

    
    
    
    float sunGlow = exp(-dist * dist * 40.0);

    
    
    float horizonFade = clamp((0.85 - sunUV.y) * 10.0, 0.0, 1.0);

    outOcclusion = isSky * max(sunDisk, sunGlow * horizonFade);
}

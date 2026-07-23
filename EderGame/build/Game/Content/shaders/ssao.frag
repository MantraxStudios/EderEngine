#version 450

// Screen-space ambient occlusion from the deferred G-buffer.
// View-space hemisphere sampling with a procedural (hash-rotated) kernel — no
// noise texture required. Output is a single-channel occlusion factor (1 = lit).
// NOTE: v1 has no separate blur pass, so a little grain is expected.

layout(location = 0) in  vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform SSAOUBO {
    mat4 proj;
    mat4 invProj;
    mat4 view;
    vec4 params;   // x = radius, y = bias, z = intensity, w = power
} ubo;

layout(set = 0, binding = 1) uniform sampler2D gNormal;  // world-space normal
layout(set = 0, binding = 2) uniform sampler2D gDepth;

#define SAMPLES 24

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

vec3 ViewPos(vec2 uv, float depth)
{
    vec4 ndc = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 v   = ubo.invProj * ndc;
    return v.xyz / v.w;
}

void main()
{
    float depth = texture(gDepth, fragUV).r;
    if (depth >= 0.9999) { outColor = vec4(1.0); return; }   // sky = unoccluded

    float radius = ubo.params.x;
    float bias   = ubo.params.y;
    float power  = max(ubo.params.w, 0.01);

    vec3 P = ViewPos(fragUV, depth);
    vec3 N = normalize(mat3(ubo.view) * normalize(texture(gNormal, fragUV).xyz));

    // Per-pixel random rotation angle to decorrelate the kernel.
    float rnd = hash(fragUV * vec2(1920.0, 1080.0)) * 6.2831853;
    float cs = cos(rnd), sn = sin(rnd);

    float occlusion = 0.0;
    for (int i = 0; i < SAMPLES; i++)
    {
        // Golden-spiral hemisphere direction in tangent space.
        float t   = (float(i) + 0.5) / float(SAMPLES);
        float ang = float(i) * 2.399963;
        vec3  dir = vec3(cos(ang) * sqrt(t), sin(ang) * sqrt(t), sqrt(1.0 - t));
        dir.xy = vec2(dir.x * cs - dir.y * sn, dir.x * sn + dir.y * cs);

        // Orient the hemisphere around the view-space normal.
        vec3 up  = abs(N.z) < 0.9 ? vec3(0,0,1) : vec3(1,0,0);
        vec3 T   = normalize(up - N * dot(up, N));
        vec3 B   = cross(N, T);
        vec3 smp = P + (T * dir.x + B * dir.y + N * dir.z) * radius * (0.2 + 0.8 * t);

        vec4 off = ubo.proj * vec4(smp, 1.0);
        off.xyz /= off.w;
        vec2 suv = off.xy * 0.5 + 0.5;
        if (suv.x < 0.0 || suv.x > 1.0 || suv.y < 0.0 || suv.y > 1.0) continue;

        float sd  = texture(gDepth, suv).r;
        vec3  sp  = ViewPos(suv, sd);

        // Occluded if the stored surface is closer to the camera than the sample
        // (view space looks down -Z, so a larger z = nearer).
        float rangeCheck = smoothstep(0.0, 1.0, radius / max(abs(P.z - sp.z), 1e-4));
        occlusion += (sp.z >= smp.z + bias ? 1.0 : 0.0) * rangeCheck;
    }

    float ao = 1.0 - (occlusion / float(SAMPLES)) * ubo.params.z;
    ao = pow(clamp(ao, 0.0, 1.0), power);
    outColor = vec4(ao, ao, ao, 1.0);
}

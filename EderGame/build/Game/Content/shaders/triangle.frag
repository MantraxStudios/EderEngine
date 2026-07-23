#version 450

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragUV;
layout(location = 2) in vec4 fragColor;
layout(location = 3) in float fragRoughness;
layout(location = 4) in float fragMetallic;
layout(location = 5) in float fragEmissive;
layout(location = 6) in vec3 fragWorldPos;
layout(location = 7) in vec3 fragTangent;
layout(location = 8) in vec3 fragBitangent;

layout(set = 0, binding = 1) uniform sampler2D albedoTex;
layout(set = 0, binding = 2) uniform sampler2D normalTex;       // tangent-space normal map
layout(set = 0, binding = 3) uniform sampler2D roughMetalTex;   // glTF: G = roughness, B = metallic
layout(set = 0, binding = 4) uniform sampler2D emissiveTex;

layout(set = 0, binding = 0) uniform MaterialUBO {
    vec4  albedo;
    float roughness;
    float metallic;
    float emissiveIntensity;
    float alphaThreshold;
    float hasNormalMap;
    float hasRoughMap;
    float hasEmissiveMap;
} material;

#define MAX_DIR_LIGHTS   4
#define MAX_POINT_LIGHTS 16
#define MAX_SPOT_LIGHTS  8

struct DirectionalLight { vec3 direction; float intensity; vec3 color; float _pad; };
struct PointLight       { vec3 position; float radius; vec3 color; float intensity;
                          int shadowIdx; float _p0; float _p1; float _p2; };
struct SpotLight        { vec3 position; float innerCos; vec3 direction; float outerCos;
                          vec3 color; float intensity; float radius; int shadowIdx; float _p0; float _p1; };

layout(set = 1, binding = 0) uniform LightUBO {
    DirectionalLight dirLights[MAX_DIR_LIGHTS];
    PointLight       pointLights[MAX_POINT_LIGHTS];
    SpotLight        spotLights[MAX_SPOT_LIGHTS];
    int              numDirLights;
    int              numPointLights;
    int              numSpotLights;
    float            nearPlane;
    vec3             cameraPos;    float _pad2;
    vec3             cameraForward; float _pad3;
    vec4             cascadeSplits;
    mat4             cascadeMatrices[4];
    mat4             spotMatrices[4];
    vec4             pointFarPlanes;
    vec4             skyAmbient;
    vec4             groundAmbient;
} lights;

// Shadow comparison samplers: each texture() tap performs a hardware-filtered
// (bilinear 2x2) depth comparison — smooth PCF without per-tap binary noise.
layout(set = 1, binding = 1) uniform sampler2DArrayShadow   shadowMap;
layout(set = 1, binding = 2) uniform sampler2DArrayShadow   spotShadowMap;
layout(set = 1, binding = 3) uniform samplerCubeArrayShadow pointShadowMap;

layout(location = 0) out vec4 outColor;

// ─────────────────────────────────────────────────────────────────────────────
//  Cook-Torrance GGX PBR
//  Diffuse is kept WITHOUT the 1/PI factor (and specular compensated by PI)
//  so existing scene light intensities keep roughly the same diffuse level —
//  the upgrade adds physically-based specular shape + energy conservation.
// ─────────────────────────────────────────────────────────────────────────────
#define PI 3.14159265359

float DistributionGGX(float NdotH, float a)
{
    float a2 = a * a;
    float d  = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

float GeometrySmith(float NdotV, float NdotL, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) * 0.125;               // UE4 direct-light remap
    float gv = NdotV / (NdotV * (1.0 - k) + k);
    float gl = NdotL / (NdotL * (1.0 - k) + k);
    return gv * gl;
}

vec3 FresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 CalcLight(vec3 N, vec3 L, vec3 V, vec3 lightColor, float intensity,
               vec3 baseColor, float roughness, float metallic)
{
    float NdotL = max(dot(N, L), 0.0);
    if (NdotL <= 0.0) return vec3(0.0);

    vec3  H     = normalize(L + V);
    float NdotV = max(dot(N, V), 1e-4);
    float NdotH = max(dot(N, H), 0.0);
    float HdotV = max(dot(H, V), 0.0);

    float a  = max(roughness * roughness, 0.002);
    vec3  F0 = mix(vec3(0.04), baseColor, metallic);

    float D = DistributionGGX(NdotH, a);
    float G = GeometrySmith(NdotV, NdotL, roughness);
    vec3  F = FresnelSchlick(HdotV, F0);

    vec3 spec = (D * G) * F / max(4.0 * NdotV * NdotL, 1e-4);
    vec3 kd   = (vec3(1.0) - F) * (1.0 - metallic);

    vec3 radiance = lightColor * intensity;
    return (kd * baseColor + spec * PI) * radiance * NdotL;
}

vec2 VogelDisk(int i, int n, float phi)
{
    float r     = sqrt((float(i) + 0.5) / float(n));
    float theta = float(i) * 2.399963 + phi;
    return vec2(r * cos(theta), r * sin(theta));
}

// World-space stable hash — same phi for the same world point regardless of camera orientation.
// Prevents shadow shimmer when the camera moves (screen-space phi changes, world-space doesn't).
float WorldPhi(vec3 p)
{
    return fract(sin(dot(p, vec3(127.1, 311.7, 74.7))) * 43758.5453) * 6.28318530;
}

// With hardware-PCF taps (each one is a filtered 2x2 comparison) 16 Vogel taps
// give an effective ~64-sample kernel — smoother than the old 32 binary taps
// at half the cost.
#define DIR_FILTER_TEXELS  3.0
#define FILTER_TEXELS      1.0
#define PCF_N              16

const float cascadeBiasScale[4] = float[4](1.0, 1.5, 2.0, 3.0);

float SampleCascade(vec3 worldPos, vec3 N, vec3 L, int cascade)
{
    // Normal-offset bias scaled to cascade coverage so it stays at ~2-4 shadow texels
    // regardless of cascade level. A fixed world-space bias causes massive peter-panning
    // in near cascades and under-biasing in far ones, creating a visible "sweep" artifact
    // as objects cross cascade boundaries while the camera moves.
    float NdotL      = clamp(dot(N, L), 0.0, 1.0);
    // Normal-offset bias: scale by cascade texel size (cascadeSplits ≈ world units covered).
    // sqrt(1-NdotL²) is tan(angle) — bias grows on grazing surfaces, shrinks on face-on ones.
    // Clamp avoids huge offsets at 90° (back-faces already handled via gl_FrontFacing).
    float sinAngle   = sqrt(max(0.0, 1.0 - NdotL * NdotL));
    float normalBias = clamp(sinAngle * 0.002, 0.0001, 0.004) * lights.cascadeSplits[cascade];
    vec4 lsPos = lights.cascadeMatrices[cascade] * vec4(worldPos + N * normalBias, 1.0);
    vec3 proj  = lsPos.xyz / lsPos.w;
    vec2 uv    = proj.xy * 0.5 + 0.5;

    if (proj.z < 0.0 || proj.z > 1.0 ||
        uv.x  < 0.0 || uv.x  > 1.0 ||
        uv.y  < 0.0 || uv.y  > 1.0)
        return 1.0;

    // Slope-scaled depth bias. 0.0005 gives ~0.01–0.06 world-unit offset across all
    // cascades (enough to prevent self-shadowing / acne on flat and angled surfaces).
    float slopeBias  = 0.0005 * cascadeBiasScale[cascade];
    float slopeScale = clamp(sinAngle, 0.0, 1.0);
    float recvZ      = proj.z - slopeBias * (1.0 + slopeScale * 3.0);

    vec2  ts     = 1.0 / vec2(textureSize(shadowMap, 0).xy);
    float fR     = DIR_FILTER_TEXELS * ts.x;
    float phi    = WorldPhi(worldPos);   // world-space stable — no shimmer on camera move
    float shadow = 0.0;
    for (int i = 0; i < PCF_N; i++)
    {
        vec2 o = VogelDisk(i, PCF_N, phi) * fR;
        // Hardware PCF: 4th component is the compare reference, result in [0,1].
        shadow += texture(shadowMap, vec4(uv + o, float(cascade), recvZ));
    }
    shadow /= float(PCF_N);
    return shadow;
}

float ShadowFactor(vec3 worldPos, vec3 N, vec3 L)
{
    // Use world-space distance from camera for cascade selection.
    // The cascade light matrices are sphere-based (centered at camera, radius = cascadeSplit),
    // so selection must use world distance — not view-depth — to avoid phantom shadows
    // when the camera rotates (objects at grazing angles have near-zero view-depth but
    // full world-distance, causing them to jump cascades as the camera moves).
    float dist = length(worldPos - lights.cameraPos);
    if (dist < lights.nearPlane) return 1.0;

    int   cascade = 3;
    for (int i = 0; i < 4; i++)
        if (dist < lights.cascadeSplits[i]) { cascade = i; break; }

    float shadow = SampleCascade(worldPos, N, L, cascade);

    if (cascade < 3)
    {
        // Blend over the last 70% of each cascade to hide the circular seam.
        // Starting at 30% (instead of 60%) reduces the pure-cascade-N inner area,
        // which minimises the lit circle when the nearest cascade has no shadow casters
        // at a given UV (e.g. scene objects outside the cascade sphere).
        float far   = lights.cascadeSplits[cascade];
        float blend = clamp((dist - far * 0.30) / (far * 0.70), 0.0, 1.0);
        if (blend > 0.0)
            shadow = mix(shadow, SampleCascade(worldPos, N, L, cascade + 1), blend);
    }
    else
    {
        // For cascade 3 only: fade out PCF samples that land near the UV edge
        // (Vogel disk can leak outside [0,1] → border color = white = 1.0 = lit).
        // Safe here because there is no cascade+1 to blend with, so the fade
        // only affects the terminal cascade and does not create a lit sphere.
        vec4 lsPos3 = lights.cascadeMatrices[3] * vec4(worldPos, 1.0);
        vec2 uv3    = lsPos3.xy / lsPos3.w * 0.5 + 0.5;
        float edgeDist = min(min(uv3.x, 1.0 - uv3.x), min(uv3.y, 1.0 - uv3.y));
        shadow = mix(1.0, shadow, clamp(edgeDist * 10.0, 0.0, 1.0));

        // Fade toward FULLY LIT (1.0) over the last 25% of cascade-3's range.
        // Fading toward 0 would zero out the directional light beyond the shadow
        // range, creating a dark band at the horizon that looks like a far shadow.
        float maxDist   = lights.cascadeSplits[3];
        float fadeStart = maxDist * 0.75;
        float fade = 1.0 - clamp((dist - fadeStart) / (maxDist - fadeStart), 0.0, 1.0);
        shadow = mix(1.0, shadow, fade);
    }
    return shadow;
}

float ShadowSpot(int slot, vec3 worldPos, vec3 N, vec3 L)
{
    float NdotL      = clamp(dot(N, L), 0.0, 1.0);
    float sinAngle   = sqrt(max(0.0, 1.0 - NdotL * NdotL));
    float normalBias = clamp(sinAngle * 0.015, 0.001, 0.02);
    vec4 lsPos = lights.spotMatrices[slot] * vec4(worldPos + N * normalBias, 1.0);
    if (lsPos.w <= 0.0) return 1.0;
    vec3 proj  = lsPos.xyz / lsPos.w;
    vec2 uv    = proj.xy * 0.5 + 0.5;
    if (proj.z < 0.0 || proj.z > 1.0 ||
        uv.x  < 0.0 || uv.x  > 1.0 ||
        uv.y  < 0.0 || uv.y  > 1.0)
        return 1.0;

    // Slope-scaled depth bias using sinAngle (more geometrically correct than 1-NdotL)
    float spotSlope = clamp(sinAngle, 0.0, 1.0);
    float recvZ  = proj.z - 0.0002 * (1.0 + spotSlope * 4.0);
    vec2  ts     = 1.0 / vec2(textureSize(spotShadowMap, 0).xy);
    float fR     = 3.0 * ts.x;
    float phi    = WorldPhi(worldPos);
    float shadow = 0.0;
    for (int i = 0; i < PCF_N; i++)
    {
        vec2 o = VogelDisk(i, PCF_N, phi) * fR;
        shadow += texture(spotShadowMap, vec4(uv + o, float(slot), recvZ));
    }
    return shadow / float(PCF_N);
}

float ShadowPoint(int slot, vec3 worldPos, vec3 lightPos, vec3 N)
{
    vec3  dir     = worldPos - lightPos;
    float dist    = length(dir);
    float farP    = lights.pointFarPlanes[slot];
    float curDist = dist / farP;
    if (curDist >= 1.0) return 1.0;

    vec3  dirN   = dir / dist;
    float NdotL  = clamp(dot(N, -dirN), 0.0, 1.0);
    float sinAngle = sqrt(max(0.0, 1.0 - NdotL * NdotL));
    // Normal-offset scales with surface angle — correct direction guaranteed by
    // gl_FrontFacing N-flip in main(), so bias always pushes away from geometry.
    float normalBias = clamp(sinAngle * 0.04, 0.005, 0.05);
    vec3  biasedDir  = (worldPos + N * normalBias) - lightPos;
    // Filter radius normalized by far plane — avoids over-blur on close lights
    // and under-blur on distant ones when all share the same 512px cubemap.
    float fR    = FILTER_TEXELS * (dist / farP) * 2.0 / 512.0;

    // Gram-Schmidt tangent frame — continuous everywhere, no 0.99 threshold flip
    vec3 tang = abs(dirN.y) < 0.9 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    tang = normalize(tang - dot(tang, dirN) * dirN);
    vec3 btan = cross(dirN, tang);

    float phi    = WorldPhi(worldPos);
    float shadow = 0.0;
    for (int i = 0; i < PCF_N; i++)
    {
        vec2 o    = VogelDisk(i, PCF_N, phi) * fR;
        vec3 sdir = biasedDir + o.x * tang + o.y * btan;
        // samplerCubeArrayShadow: compare reference is the extra argument.
        shadow   += texture(pointShadowMap, vec4(sdir, float(slot)), curDist);
    }
    return shadow / float(PCF_N);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Output transform: exposure → ACES (Narkowicz) → gamma 2.2.
//  The swapchain / scene framebuffer is UNORM (not sRGB), so without this the
//  linear-space lighting is displayed raw — dark, flat and clipped.
// ─────────────────────────────────────────────────────────────────────────────
#define EXPOSURE 1.15

vec3 ACESFilm(vec3 x)
{
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main()
{
    vec4  albedoSample = texture(albedoTex, fragUV);
    float alpha        = fragColor.a * albedoSample.a;
    if (material.alphaThreshold > 0.0 && alpha < material.alphaThreshold)
        discard;

    vec3 baseColor = fragColor.rgb * albedoSample.rgb;

    // Geometric normal, flipped for back-faces (inverted-winding meshes).
    // Without this, N points inward → normal-offset bias pushes the sample point
    // INTO the geometry → false self-shadowing / phantom shadows on every back-face.
    float facing = gl_FrontFacing ? 1.0 : -1.0;
    vec3  Ng     = normalize(fragNormal) * facing;

    // Tangent-space normal mapping (only when the material supplies a normal map).
    vec3 N = Ng;
    if (material.hasNormalMap > 0.5)
    {
        vec3 T  = normalize(fragTangent);
        vec3 B  = normalize(fragBitangent) * facing;
        vec3 ts = texture(normalTex, fragUV).xyz * 2.0 - 1.0;
        N = normalize(mat3(T, B, Ng) * ts);
    }

    // Roughness / metallic — from the material scalars, modulated by an optional
    // packed map (glTF convention: G = roughness, B = metallic).
    float roughness = fragRoughness;
    float metallic  = fragMetallic;
    if (material.hasRoughMap > 0.5)
    {
        vec3 rm    = texture(roughMetalTex, fragUV).rgb;
        roughness *= rm.g;
        metallic  *= rm.b;
    }

    vec3 V = normalize(lights.cameraPos - fragWorldPos);

    // Hemisphere ambient: diffuse part + a Fresnel-weighted specular ambient so
    // metals and grazing angles pick up environment colour instead of going black.
    float hemi    = N.y * 0.5 + 0.5;
    vec3  ambient = mix(lights.groundAmbient.rgb, lights.skyAmbient.rgb, hemi);
    float NdotV   = max(dot(N, V), 0.0);
    vec3  F0      = mix(vec3(0.04), baseColor, metallic);
    vec3  Fa      = F0 + (max(vec3(1.0 - roughness), F0) - F0)
                       * pow(1.0 - NdotV, 5.0);
    vec3  result  = ambient * (baseColor * (1.0 - metallic) + Fa);

    for (int i = 0; i < lights.numDirLights; i++)
    {
        vec3  L      = normalize(-lights.dirLights[i].direction);
        float shadow = (i == 0) ? ShadowFactor(fragWorldPos, N, L) : 1.0;
        result += shadow * CalcLight(N, L, V,
            lights.dirLights[i].color, lights.dirLights[i].intensity,
            baseColor, roughness, metallic);
    }

    for (int i = 0; i < lights.numPointLights; i++)
    {
        vec3  toLight  = lights.pointLights[i].position - fragWorldPos;
        float distSqr  = dot(toLight, toLight);
        float dist     = sqrt(distSqr);
        float r        = lights.pointLights[i].radius;
        if (dist >= r) continue;
        float normDist = dist / r;
        float window   = max(0.0, 1.0 - normDist * normDist * normDist * normDist);
        window        *= window;
        // Bounded inverse-square falloff: physical 1/d² rolloff shape without the
        // singularity at the light centre (peaks at 1), so existing point-light
        // intensities stay usable while the near-field falls off more naturally.
        float atten    = window / (1.0 + 8.0 * normDist * normDist);
        float shadow   = (lights.pointLights[i].shadowIdx >= 0)
                       ? ShadowPoint(lights.pointLights[i].shadowIdx, fragWorldPos,
                                     lights.pointLights[i].position, N) : 1.0;
        result += shadow * CalcLight(N, toLight / dist, V,
            lights.pointLights[i].color, lights.pointLights[i].intensity * atten,
            baseColor, roughness, metallic);
    }

    for (int i = 0; i < lights.numSpotLights; i++)
    {
        vec3  toLight  = lights.spotLights[i].position - fragWorldPos;
        float distSqr  = dot(toLight, toLight);
        float dist     = sqrt(distSqr);
        vec3  L        = toLight / dist;
        float r        = lights.spotLights[i].radius;
        if (dist >= r) continue;
        float normDist = dist / r;
        float window   = max(0.0, 1.0 - normDist * normDist * normDist * normDist);
        window        *= window;
        float atten    = window / (1.0 + 8.0 * normDist * normDist);
        float theta    = dot(L, normalize(-lights.spotLights[i].direction));
        float eps      = lights.spotLights[i].innerCos - lights.spotLights[i].outerCos;
        float cone     = clamp((theta - lights.spotLights[i].outerCos) / max(eps, 0.0001), 0.0, 1.0);
        if (cone <= 0.0) continue;
        float shadow   = (lights.spotLights[i].shadowIdx >= 0)
                       ? ShadowSpot(lights.spotLights[i].shadowIdx, fragWorldPos, N, L) : 1.0;
        result += shadow * CalcLight(N, L, V,
            lights.spotLights[i].color, lights.spotLights[i].intensity * atten * cone,
            baseColor, roughness, metallic);
    }

    // Emission: tinted by the albedo by default, or by a dedicated emissive map.
    vec3 emission = baseColor * fragEmissive;
    if (material.hasEmissiveMap > 0.5)
        emission = texture(emissiveTex, fragUV).rgb * fragEmissive;
    result += emission;

    // HDR → display: exposure, filmic tonemap, gamma encode (UNORM target).
    vec3 mapped = ACESFilm(result * EXPOSURE);
    mapped      = pow(mapped, vec3(1.0 / 2.2));

    outColor = vec4(mapped, alpha);
}
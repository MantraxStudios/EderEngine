#version 450

// Deferred lighting pass. Reads the G-buffer + SSAO (set 0) and the shared
// LightBuffer descriptor set (set 1, identical to the forward pipeline), then
// runs the same Cook-Torrance PBR + CSM / spot / point shadows as triangle.frag.

layout(location = 0) in  vec2 fragUV;
layout(location = 0) out vec4 outColor;

// ── Set 0: G-buffer + reconstruction UBO ─────────────────────────────────────
layout(set = 0, binding = 0) uniform DeferredUBO {
    mat4 invViewProj;
} ubo;
layout(set = 0, binding = 1) uniform sampler2D gAlbedo;    // rgb albedo, a emissive
layout(set = 0, binding = 2) uniform sampler2D gNormal;    // rgb world normal
layout(set = 0, binding = 3) uniform sampler2D gMaterial;  // r rough, g metal
layout(set = 0, binding = 4) uniform sampler2D gDepth;
layout(set = 0, binding = 5) uniform sampler2D ssaoTex;    // r = ambient occlusion

// ── Set 1: LightBuffer (same as forward) ─────────────────────────────────────
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
    int   numDirLights;
    int   numPointLights;
    int   numSpotLights;
    float nearPlane;
    vec3  cameraPos;    float _pad2;
    vec3  cameraForward; float _pad3;
    vec4  cascadeSplits;
    mat4  cascadeMatrices[4];
    mat4  spotMatrices[4];
    vec4  pointFarPlanes;
    vec4  skyAmbient;
    vec4  groundAmbient;
} lights;

layout(set = 1, binding = 1) uniform sampler2DArrayShadow   shadowMap;
layout(set = 1, binding = 2) uniform sampler2DArrayShadow   spotShadowMap;
layout(set = 1, binding = 3) uniform samplerCubeArrayShadow pointShadowMap;

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
    float k = (r * r) * 0.125;
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
// PCF rotation angle. In the deferred path the world position is RECONSTRUCTED
// from the depth buffer, so it carries tiny camera-dependent error every frame.
// Hashing that (like the forward path does with exact positions) re-randomizes
// the PCF disk each frame -> shadows boil whenever the camera changes at all.
// Interleaved Gradient Noise over the screen pixel is fully deterministic:
// zero frame-to-frame variance, so shadows stay rock stable.
float PixelPhi(vec2 pixel)
{
    return fract(52.9829189 * fract(0.06711056 * pixel.x + 0.00583715 * pixel.y))
           * 6.28318530;
}

#define DIR_FILTER_TEXELS  3.0
#define FILTER_TEXELS      1.0
#define PCF_N              16
const float cascadeBiasScale[4] = float[4](1.0, 1.5, 2.0, 3.0);

float SampleCascade(vec3 worldPos, vec3 N, vec3 L, int cascade)
{
    float NdotL      = clamp(dot(N, L), 0.0, 1.0);
    float sinAngle   = sqrt(max(0.0, 1.0 - NdotL * NdotL));
    float normalBias = clamp(sinAngle * 0.002, 0.0001, 0.004) * lights.cascadeSplits[cascade];
    vec4 lsPos = lights.cascadeMatrices[cascade] * vec4(worldPos + N * normalBias, 1.0);
    vec3 proj  = lsPos.xyz / lsPos.w;
    vec2 uv    = proj.xy * 0.5 + 0.5;
    if (proj.z < 0.0 || proj.z > 1.0 || uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)
        return 1.0;
    // Slightly larger than the forward path: the reconstructed world position
    // carries depth-buffer error that the bias must absorb to avoid acne flicker.
    float slopeBias  = 0.0007 * cascadeBiasScale[cascade];
    float slopeScale = clamp(sinAngle, 0.0, 1.0);
    float recvZ      = proj.z - slopeBias * (1.0 + slopeScale * 3.0);
    vec2  ts     = 1.0 / vec2(textureSize(shadowMap, 0).xy);
    float fR     = DIR_FILTER_TEXELS * ts.x;
    float phi    = PixelPhi(gl_FragCoord.xy);
    float shadow = 0.0;
    for (int i = 0; i < PCF_N; i++)
    {
        vec2 o = VogelDisk(i, PCF_N, phi) * fR;
        shadow += texture(shadowMap, vec4(uv + o, float(cascade), recvZ));
    }
    return shadow / float(PCF_N);
}
float ShadowFactor(vec3 worldPos, vec3 N, vec3 L)
{
    float dist = length(worldPos - lights.cameraPos);
    if (dist < lights.nearPlane) return 1.0;
    int cascade = 3;
    for (int i = 0; i < 4; i++)
        if (dist < lights.cascadeSplits[i]) { cascade = i; break; }
    float shadow = SampleCascade(worldPos, N, L, cascade);
    if (cascade < 3)
    {
        float far   = lights.cascadeSplits[cascade];
        float blend = clamp((dist - far * 0.30) / (far * 0.70), 0.0, 1.0);
        if (blend > 0.0)
            shadow = mix(shadow, SampleCascade(worldPos, N, L, cascade + 1), blend);
    }
    else
    {
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
    if (proj.z < 0.0 || proj.z > 1.0 || uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)
        return 1.0;
    float spotSlope = clamp(sinAngle, 0.0, 1.0);
    float recvZ  = proj.z - 0.0002 * (1.0 + spotSlope * 4.0);
    float fR     = 3.0 / vec2(textureSize(spotShadowMap, 0).xy).x;
    float phi    = PixelPhi(gl_FragCoord.xy);
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
    float normalBias = clamp(sinAngle * 0.04, 0.005, 0.05);
    vec3  biasedDir  = (worldPos + N * normalBias) - lightPos;
    float fR    = FILTER_TEXELS * (dist / farP) * 2.0 / 512.0;
    vec3 tang = abs(dirN.y) < 0.9 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    tang = normalize(tang - dot(tang, dirN) * dirN);
    vec3 btan = cross(dirN, tang);
    float phi    = PixelPhi(gl_FragCoord.xy);
    float shadow = 0.0;
    for (int i = 0; i < PCF_N; i++)
    {
        vec2 o    = VogelDisk(i, PCF_N, phi) * fR;
        vec3 sdir = biasedDir + o.x * tang + o.y * btan;
        shadow   += texture(pointShadowMap, vec4(sdir, float(slot)), curDist);
    }
    return shadow / float(PCF_N);
}

#define EXPOSURE 1.15
vec3 ACESFilm(vec3 x)
{
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

vec3 ReconstructWorldPos(vec2 uv, float depth)
{
    vec4 ndc   = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 world = ubo.invViewProj * ndc;
    return world.xyz / world.w;
}

void main()
{
    float depth = texture(gDepth, fragUV).r;

    // Sky / background pixel (no geometry): discard so the procedural skybox that
    // was drawn into this framebuffer just before the composite shows through.
    if (depth >= 0.9999)
        discard;

    // Re-emit the G-buffer depth so downstream depth-based passes keep working.
    gl_FragDepth = depth;

    vec4  gA        = texture(gAlbedo,   fragUV);
    vec3  baseColor = gA.rgb;
    float emissive  = gA.a;
    vec3  N         = normalize(texture(gNormal, fragUV).xyz);
    vec2  rm        = texture(gMaterial, fragUV).rg;
    float roughness = rm.r;
    float metallic  = rm.g;

    // Blur the SSAO (5x5 box) — the raw AO is noisy and screen-space, which
    // otherwise looks like shadows swimming across surfaces as the camera moves.
    vec2  aoTexel = 1.0 / vec2(textureSize(ssaoTex, 0));
    float ao = 0.0;
    for (int ax = -2; ax <= 2; ++ax)
    for (int ay = -2; ay <= 2; ++ay)
        ao += texture(ssaoTex, fragUV + vec2(ax, ay) * aoTexel).r;
    ao /= 25.0;

    vec3 worldPos = ReconstructWorldPos(fragUV, depth);
    vec3 V        = normalize(lights.cameraPos - worldPos);

    // Hemisphere ambient, modulated by SSAO.
    float hemi    = N.y * 0.5 + 0.5;
    vec3  ambient = mix(lights.groundAmbient.rgb, lights.skyAmbient.rgb, hemi) * ao;
    float NdotV   = max(dot(N, V), 1e-3);
    vec3  F0      = mix(vec3(0.04), baseColor, metallic);
    // Split-sum environment BRDF approximation (Karis) — roughness-aware ambient
    // specular (no mirror-like grazing sheen from tilted normals).
    vec4  eb0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    vec4  eb1 = vec4( 1.0,  0.0425,  1.04,  -0.04);
    vec4  ebr = roughness * eb0 + eb1;
    float ebA = min(ebr.x * ebr.x, exp2(-9.28 * NdotV)) * ebr.x + ebr.y;
    vec2  ebAB = vec2(-1.04, 1.04) * ebA + ebr.zw;
    vec3  specAmbient = ambient * (F0 * ebAB.x + ebAB.y);
    vec3  result  = ambient * baseColor * (1.0 - metallic) + specAmbient;

    for (int i = 0; i < lights.numDirLights; i++)
    {
        vec3  L      = normalize(-lights.dirLights[i].direction);
        float shadow = (i == 0) ? ShadowFactor(worldPos, N, L) : 1.0;
        result += shadow * CalcLight(N, L, V,
            lights.dirLights[i].color, lights.dirLights[i].intensity,
            baseColor, roughness, metallic);
    }
    for (int i = 0; i < lights.numPointLights; i++)
    {
        vec3  toLight  = lights.pointLights[i].position - worldPos;
        float dist     = length(toLight);
        float r        = lights.pointLights[i].radius;
        if (dist >= r) continue;
        float normDist = dist / r;
        float window   = max(0.0, 1.0 - normDist*normDist*normDist*normDist);
        window        *= window;
        float atten    = window / (1.0 + 8.0 * normDist * normDist);
        float shadow   = (lights.pointLights[i].shadowIdx >= 0)
                       ? ShadowPoint(lights.pointLights[i].shadowIdx, worldPos,
                                     lights.pointLights[i].position, N) : 1.0;
        result += shadow * CalcLight(N, toLight / dist, V,
            lights.pointLights[i].color, lights.pointLights[i].intensity * atten,
            baseColor, roughness, metallic);
    }
    for (int i = 0; i < lights.numSpotLights; i++)
    {
        vec3  toLight  = lights.spotLights[i].position - worldPos;
        float dist     = length(toLight);
        vec3  L        = toLight / dist;
        float r        = lights.spotLights[i].radius;
        if (dist >= r) continue;
        float normDist = dist / r;
        float window   = max(0.0, 1.0 - normDist*normDist*normDist*normDist);
        window        *= window;
        float atten    = window / (1.0 + 8.0 * normDist * normDist);
        float theta    = dot(L, normalize(-lights.spotLights[i].direction));
        float eps      = lights.spotLights[i].innerCos - lights.spotLights[i].outerCos;
        float cone     = clamp((theta - lights.spotLights[i].outerCos) / max(eps, 0.0001), 0.0, 1.0);
        if (cone <= 0.0) continue;
        float shadow   = (lights.spotLights[i].shadowIdx >= 0)
                       ? ShadowSpot(lights.spotLights[i].shadowIdx, worldPos, N, L) : 1.0;
        result += shadow * CalcLight(N, L, V,
            lights.spotLights[i].color, lights.spotLights[i].intensity * atten * cone,
            baseColor, roughness, metallic);
    }

    result += baseColor * emissive;

    // Linear HDR out — single tonemap at the end of the frame chain.
    outColor = vec4(result, 1.0);
}

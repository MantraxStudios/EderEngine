#version 450

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragUV;
layout(location = 2) in vec4 fragColor;
layout(location = 3) in float fragRoughness;
layout(location = 4) in float fragMetallic;
layout(location = 5) in float fragEmissive;
layout(location = 6) in vec3 fragWorldPos;

layout(set = 0, binding = 1) uniform sampler2D albedoTex;

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
    float            _pad;
    vec3             cameraPos;
    float            _pad2;
    vec3             cameraForward;
    float            _pad3;
    vec4             cascadeSplits;
    mat4             cascadeMatrices[4];
    mat4             spotMatrices[4];
    vec4             pointFarPlanes;
} lights;

layout(set = 1, binding = 1) uniform sampler2DArray    shadowMap;
layout(set = 1, binding = 2) uniform sampler2DArray    spotShadowMap;
layout(set = 1, binding = 3) uniform samplerCubeArray  pointShadowMap;

layout(location = 0) out vec4 outColor;

// ---------------------------------------------------------------------------
// Lighting
// ---------------------------------------------------------------------------
vec3 CalcLight(vec3 N, vec3 L, vec3 V, vec3 lightColor, float intensity,
               vec3 baseColor, float roughness, float metallic)
{
    float NdotL   = max(dot(N, L), 0.0);
    vec3  diffuse = baseColor * (1.0 - metallic) * lightColor * intensity * NdotL;

    vec3  H         = normalize(L + V);
    float shininess = pow(1.0 - roughness, 4.0) * 256.0 + 1.0;
    float spec      = pow(max(dot(N, H), 0.0), shininess) * NdotL;
    vec3  specColor = mix(vec3(0.04), baseColor, metallic);
    vec3  specular  = specColor * lightColor * intensity * spec;

    return diffuse + specular;
}

// ---------------------------------------------------------------------------
// PCSS helpers
// ---------------------------------------------------------------------------
vec2 VogelDisk(int i, int n, float phi)
{
    float r     = sqrt((float(i) + 0.5) / float(n));
    float theta = float(i) * 2.399963 + phi;
    return vec2(r * cos(theta), r * sin(theta));
}

float IGN(vec2 fragCoord)
{
    return fract(52.9829189 * fract(dot(fragCoord, vec2(0.06711056, 0.00583715))));
}

// ---------------------------------------------------------------------------
// PCSS for a single cascade — bounds-checked blocker search
// ---------------------------------------------------------------------------
float SampleCascade(vec3 worldPos, vec3 N, vec3 L, int cascade)
{
    vec4 lsPos = lights.cascadeMatrices[cascade] * vec4(worldPos, 1.0);
    vec3 proj   = lsPos.xyz / lsPos.w;
    vec2 uv     = proj.xy * 0.5 + 0.5;

    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || proj.z > 1.0)
        return 1.0;

    float NdotL = max(dot(N, L), 0.0);
    float bias  = max(0.0015 * (1.0 - NdotL), 0.0003);
    float recvZ = proj.z - bias;

    vec2  texelSize = 1.0 / vec2(textureSize(shadowMap, 0).xy);
    float phi       = IGN(gl_FragCoord.xy) * 6.28318530;

    // Blocker search — skip samples outside shadow map to avoid false "no blocker"
    const int   BLOCKER_N = 8;
    const float SEARCH_R  = 3.5;
    float blockerSum = 0.0;
    int   blockerCnt = 0;
    for (int i = 0; i < BLOCKER_N; i++)
    {
        vec2 o   = VogelDisk(i, BLOCKER_N, phi) * (SEARCH_R * texelSize.x);
        vec2 suv = uv + o;
        if (suv.x < 0.0 || suv.x > 1.0 || suv.y < 0.0 || suv.y > 1.0) continue;
        float d = texture(shadowMap, vec3(suv, float(cascade))).r;
        if (d < recvZ) { blockerSum += d; blockerCnt++; }
    }
    if (blockerCnt == 0) return 1.0;

    // Penumbra — contact-hardening radius
    float avgBlocker = blockerSum / float(blockerCnt);
    float penumbra   = (recvZ - avgBlocker) / avgBlocker;
    float filterR    = clamp(penumbra * 0.06, 1.0 * texelSize.x, 7.0 * texelSize.x);

    // PCF with Vogel disk
    const int PCF_N = 24;
    float shadow = 0.0;
    for (int i = 0; i < PCF_N; i++)
    {
        vec2  o       = VogelDisk(i, PCF_N, phi + 1.5707963) * filterR;
        float closest = texture(shadowMap, vec3(uv + o, float(cascade))).r;
        shadow += (recvZ < closest) ? 1.0 : 0.0;
    }
    return shadow / float(PCF_N);
}

// ---------------------------------------------------------------------------
// Shadow factor with cascade selection + blend zone to hide seams
// ---------------------------------------------------------------------------
float ShadowFactor(vec3 worldPos, vec3 N, vec3 L)
{
    float depth = dot(worldPos - lights.cameraPos, lights.cameraForward);

    int cascade = 3;
    for (int i = 0; i < 4; i++)
        if (depth < lights.cascadeSplits[i]) { cascade = i; break; }

    float shadow = SampleCascade(worldPos, N, L, cascade);

    // Blend with next cascade in the last 15% before the split boundary
    if (cascade < 3)
    {
        float splitFar    = lights.cascadeSplits[cascade];
        float blendStart  = splitFar * 0.85;
        float blend       = clamp((depth - blendStart) / (splitFar - blendStart), 0.0, 1.0);
        if (blend > 0.0)
            shadow = mix(shadow, SampleCascade(worldPos, N, L, cascade + 1), blend);
    }

    return shadow;
}

// ---------------------------------------------------------------------------
// Spot light shadow — PCSS (igual que directional)
// ---------------------------------------------------------------------------
float ShadowSpot(int slot, vec3 worldPos, vec3 N, vec3 L)
{
    vec4 lsPos = lights.spotMatrices[slot] * vec4(worldPos, 1.0);
    if (lsPos.w <= 0.0) return 1.0;

    vec3 proj = lsPos.xyz / lsPos.w;
    vec2 uv   = proj.xy * 0.5 + 0.5;
    if (proj.z < 0.0 || proj.z > 1.0 ||
        uv.x < 0.0 || uv.x > 1.0 ||
        uv.y < 0.0 || uv.y > 1.0)
        return 1.0;

    float NdotL     = max(dot(N, L), 0.0);
    float bias      = max(0.002 * (1.0 - NdotL), 0.0005);
    float recvZ     = proj.z - bias;
    vec2  texelSize = 1.0 / vec2(textureSize(spotShadowMap, 0).xy);
    float phi       = IGN(gl_FragCoord.xy) * 6.28318530;

    // Blocker search
    const int   BLOCKER_N = 8;
    const float SEARCH_R  = 3.5;
    float blockerSum = 0.0; int blockerCnt = 0;
    for (int i = 0; i < BLOCKER_N; i++)
    {
        vec2  o   = VogelDisk(i, BLOCKER_N, phi) * (SEARCH_R * texelSize.x);
        vec2  suv = uv + o;
        if (suv.x < 0.0 || suv.x > 1.0 || suv.y < 0.0 || suv.y > 1.0) continue;
        float d = texture(spotShadowMap, vec3(suv, float(slot))).r;
        if (d < recvZ) { blockerSum += d; blockerCnt++; }
    }
    if (blockerCnt == 0) return 1.0;

    float avgBlocker = blockerSum / float(blockerCnt);
    float penumbra   = (recvZ - avgBlocker) / avgBlocker;
    float filterR    = clamp(penumbra * 0.06, 1.0 * texelSize.x, 7.0 * texelSize.x);

    // PCF
    const int PCF_N = 24;
    float shadow = 0.0;
    for (int i = 0; i < PCF_N; i++)
    {
        vec2  o = VogelDisk(i, PCF_N, phi + 1.5707963) * filterR;
        float d = texture(spotShadowMap, vec3(uv + o, float(slot))).r;
        shadow += (recvZ < d) ? 1.0 : 0.0;
    }
    return shadow / float(PCF_N);
}

// ---------------------------------------------------------------------------
// Point light shadow — PCSS en world-space (igual suavizado que directional)
// ---------------------------------------------------------------------------
float ShadowPoint(int slot, vec3 worldPos, vec3 lightPos, vec3 N)
{
    vec3  dir      = worldPos - lightPos;
    float dist     = length(dir);
    float farPlane = lights.pointFarPlanes[slot];
    float curDist  = dist / farPlane;
    if (curDist >= 1.0) return 1.0;

    vec3  dirN  = dir / dist;
    float NdotL = max(dot(N, -dirN), 0.0);
    float bias  = max(0.0008 * (1.0 - NdotL), 0.0001);

    // Tamaño de texel en world-space: cara cubemap 90° a distancia dist
    float worldTexel = dist * 2.0 / 512.0;   // 512 = tamaño del cubemap

    // Tangente/bitangente perpendiculares a dirN (sin cruce de cara)
    vec3 up      = abs(dirN.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, dirN));
    vec3 bitan   = cross(dirN, tangent);

    float phi = IGN(gl_FragCoord.xy) * 6.28318530;

    // Blocker search (en world-space)
    const int   BLOCKER_N  = 8;
    const float SEARCH_R   = 3.5;
    float searchR = SEARCH_R * worldTexel;
    float blockerSum = 0.0; int blockerCnt = 0;
    for (int i = 0; i < BLOCKER_N; i++)
    {
        vec2  o    = VogelDisk(i, BLOCKER_N, phi) * searchR;
        vec3  sdir = dir + o.x * tangent + o.y * bitan;
        float d    = texture(pointShadowMap, vec4(sdir, float(slot))).r;
        // d está en [0,1] normalizado por farPlane
        float sampleDist = d * farPlane;
        if (sampleDist < dist - bias * farPlane) { blockerSum += sampleDist; blockerCnt++; }
    }
    if (blockerCnt == 0) return 1.0;

    float avgBlocker = blockerSum / float(blockerCnt);
    float penumbra   = (dist - avgBlocker) / avgBlocker;
    float filterR    = clamp(penumbra * 0.06 * dist, 1.0 * worldTexel, 7.0 * worldTexel);

    // PCF
    const int PCF_N = 24;
    float shadow = 0.0;
    for (int i = 0; i < PCF_N; i++)
    {
        vec2  o    = VogelDisk(i, PCF_N, phi + 1.5707963) * filterR;
        vec3  sdir = dir + o.x * tangent + o.y * bitan;
        float d    = texture(pointShadowMap, vec4(sdir, float(slot))).r;
        shadow    += (curDist - bias > d) ? 0.0 : 1.0;
    }
    return shadow / float(PCF_N);
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
void main()
{
    vec3 baseColor = fragColor.rgb * texture(albedoTex, fragUV).rgb;
    vec3 N         = normalize(fragNormal);
    vec3 V         = normalize(lights.cameraPos - fragWorldPos);

    float hemi         = N.y * 0.5 + 0.5;
    vec3  ambientSky   = vec3(0.05, 0.06, 0.09);
    vec3  ambientGround= vec3(0.03, 0.025, 0.015);
    vec3 result = baseColor * mix(ambientGround, ambientSky, hemi);

    for (int i = 0; i < lights.numDirLights; i++)
    {
        vec3  L      = normalize(-lights.dirLights[i].direction);
        float shadow = (i == 0) ? ShadowFactor(fragWorldPos, N, L) : 1.0;
        result += shadow * CalcLight(N, L, V,
            lights.dirLights[i].color, lights.dirLights[i].intensity,
            baseColor, fragRoughness, fragMetallic);
    }

    for (int i = 0; i < lights.numPointLights; i++)
    {
        vec3  toLight = lights.pointLights[i].position - fragWorldPos;
        float dist    = length(toLight);
        float r       = lights.pointLights[i].radius;
        float atten   = clamp(1.0 - (dist * dist) / (r * r), 0.0, 1.0);
        atten        *= atten;
        float shadow  = (lights.pointLights[i].shadowIdx >= 0)
                        ? ShadowPoint(lights.pointLights[i].shadowIdx, fragWorldPos,
                                      lights.pointLights[i].position, N)
                        : 1.0;
        result += shadow * CalcLight(N, toLight / dist, V,
            lights.pointLights[i].color, lights.pointLights[i].intensity * atten,
            baseColor, fragRoughness, fragMetallic);
    }

    for (int i = 0; i < lights.numSpotLights; i++)
    {
        vec3  toLight = lights.spotLights[i].position - fragWorldPos;
        float dist    = length(toLight);
        vec3  L       = toLight / dist;
        float r       = lights.spotLights[i].radius;
        float atten   = clamp(1.0 - (dist * dist) / (r * r), 0.0, 1.0);
        atten        *= atten;
        float theta   = dot(L, normalize(-lights.spotLights[i].direction));
        float eps     = lights.spotLights[i].innerCos - lights.spotLights[i].outerCos;
        float cone    = clamp((theta - lights.spotLights[i].outerCos) / eps, 0.0, 1.0);
        float shadow  = (lights.spotLights[i].shadowIdx >= 0)
                        ? ShadowSpot(lights.spotLights[i].shadowIdx, fragWorldPos, N, L)
                        : 1.0;
        result += shadow * CalcLight(N, L, V,
            lights.spotLights[i].color, lights.spotLights[i].intensity * atten * cone,
            baseColor, fragRoughness, fragMetallic);
    }

    result += baseColor * fragEmissive;

    float alpha = fragColor.a * texture(albedoTex, fragUV).a;
    outColor = vec4(result, alpha);
}

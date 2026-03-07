#version 450

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragUV;
layout(location = 2) in vec4 fragColor;
layout(location = 3) in float fragRoughness;
layout(location = 4) in float fragMetallic;
layout(location = 5) in float fragEmissive;
layout(location = 6) in vec3 fragWorldPos;

layout(set = 0, binding = 1) uniform sampler2D albedoTex;

layout(set = 0, binding = 0) uniform MaterialUBO {
    vec4  albedo;
    float roughness;
    float metallic;
    float emissiveIntensity;
    float alphaThreshold;
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
    float            _pad;
    vec3             cameraPos;
    float            _pad2;
    vec3             cameraForward;
    float            _pad3;
    vec4             cascadeSplits;
    mat4             cascadeMatrices[4];
    mat4             spotMatrices[4];
    vec4             pointFarPlanes;
    vec4             skyAmbient;
    vec4             groundAmbient;
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
// Vogel disk
// ---------------------------------------------------------------------------
vec2 VogelDisk(int i, int n, float phi)
{
    float r     = sqrt((float(i) + 0.5) / float(n));
    float theta = float(i) * 2.399963 + phi;
    return vec2(r * cos(theta), r * sin(theta));
}

// Hash cuantizado en world-space → ángulo fijo por celda, sin parpadeo al moverse
float WorldHash(vec3 p)
{
    return fract(dot(floor(p * 8.0 + 0.5), vec3(127.1, 311.7, 74.7)));
}

// ---------------------------------------------------------------------------
// PCF de radio fijo — estilo Unreal Engine
//
// Unreal usa un kernel fijo de ~2–3 texels de radio con disco Poisson/Vogel
// rotado por un hash en world-space. El resultado: bordes suaves y uniformes,
// bien pegados al objeto, sin la zona pre-sombra difusa del PCSS.
//
// FILTER_TEXELS controla el suavizado:
//   1.5  → sombra casi dura con bordes apenas suavizados
//   2.5  → suave tipo Unreal (valor recomendado)
//   4.0  → más suave, ligeramente más "dreamy"
// ---------------------------------------------------------------------------
#define FILTER_TEXELS 2.5
#define PCF_N         16

float SampleCascade(vec3 worldPos, vec3 N, vec3 L, int cascade)
{
    vec4 lsPos = lights.cascadeMatrices[cascade] * vec4(worldPos, 1.0);
    vec3 proj   = lsPos.xyz / lsPos.w;
    vec2 uv     = proj.xy * 0.5 + 0.5;

    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || proj.z > 1.0)
        return 1.0;

    float NdotL = max(dot(N, L), 0.0);
    float bias  = max(0.0006 * (1.0 - NdotL), 0.0001);
    float recvZ = proj.z - bias;

    vec2  texelSize = 1.0 / vec2(textureSize(shadowMap, 0).xy);
    float filterR   = FILTER_TEXELS * texelSize.x;   // radio fijo, no depende de penumbra

    // Rotación aleatoria por hash world-space → sin banding, sin parpadeo
    float phi   = WorldHash(worldPos) * 6.28318530;
    float shadow = 0.0;
    for (int i = 0; i < PCF_N; i++)
    {
        vec2  o       = VogelDisk(i, PCF_N, phi) * filterR;
        float closest = texture(shadowMap, vec3(uv + o, float(cascade))).r;
        shadow += (recvZ < closest) ? 1.0 : 0.0;
    }
    return shadow / float(PCF_N);
}

// ---------------------------------------------------------------------------
// Shadow factor con selección de cascade + zona de blend
// ---------------------------------------------------------------------------
float ShadowFactor(vec3 worldPos, vec3 N, vec3 L)
{
    float depth = dot(worldPos - lights.cameraPos, lights.cameraForward);

    int cascade = 3;
    for (int i = 0; i < 4; i++)
        if (depth < lights.cascadeSplits[i]) { cascade = i; break; }

    float shadow = SampleCascade(worldPos, N, L, cascade);

    // Blend suave con el siguiente cascade en el último 10% antes del split
    if (cascade < 3)
    {
        float splitFar   = lights.cascadeSplits[cascade];
        float blendStart = splitFar * 0.90;
        float blend      = clamp((depth - blendStart) / (splitFar - blendStart), 0.0, 1.0);
        if (blend > 0.0)
            shadow = mix(shadow, SampleCascade(worldPos, N, L, cascade + 1), blend);
    }

    return shadow;
}

// ---------------------------------------------------------------------------
// Spot light shadow — PCF radio fijo
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
    float bias      = max(0.0006 * (1.0 - NdotL), 0.0001);
    float recvZ     = proj.z - bias;
    vec2  texelSize = 1.0 / vec2(textureSize(spotShadowMap, 0).xy);
    float filterR   = FILTER_TEXELS * texelSize.x;
    float phi       = WorldHash(worldPos) * 6.28318530;

    float shadow = 0.0;
    for (int i = 0; i < PCF_N; i++)
    {
        vec2  o = VogelDisk(i, PCF_N, phi) * filterR;
        float d = texture(spotShadowMap, vec3(uv + o, float(slot))).r;
        shadow += (recvZ < d) ? 1.0 : 0.0;
    }
    return shadow / float(PCF_N);
}

// ---------------------------------------------------------------------------
// Point light shadow — PCF radio fijo en world-space
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
    float bias  = max(0.0005 * (1.0 - NdotL), 0.0001);

    // Radio fijo en world-space equivalente a FILTER_TEXELS texels del cubemap
    float worldTexel = dist * 2.0 / 512.0;
    float filterR    = FILTER_TEXELS * worldTexel;

    vec3 up      = abs(dirN.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, dirN));
    vec3 bitan   = cross(dirN, tangent);

    float phi    = WorldHash(worldPos) * 6.28318530;
    float shadow = 0.0;
    for (int i = 0; i < PCF_N; i++)
    {
        vec2  o    = VogelDisk(i, PCF_N, phi) * filterR;
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

    float hemi  = N.y * 0.5 + 0.5;
    vec3 ambient = mix(lights.groundAmbient.rgb, lights.skyAmbient.rgb, hemi);
    vec3 result  = baseColor * ambient;

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
        float distSqr = dot(toLight, toLight);
        float dist    = sqrt(distSqr);
        float r       = lights.pointLights[i].radius;
        if (dist >= r) continue;
        float minDSq   = r * r * 0.0001;
        float normDist = dist / r;
        float window   = max(0.0, 1.0 - normDist * normDist * normDist * normDist);
        float atten    = (r * r / max(distSqr, minDSq)) * (window * window);
        float shadow   = (lights.pointLights[i].shadowIdx >= 0)
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
        float distSqr = dot(toLight, toLight);
        float dist    = sqrt(distSqr);
        vec3  L       = toLight / dist;
        float r       = lights.spotLights[i].radius;
        if (dist >= r) continue;
        float minDSq   = r * r * 0.0001;
        float normDist = dist / r;
        float window   = max(0.0, 1.0 - normDist * normDist * normDist * normDist);
        float atten    = (r * r / max(distSqr, minDSq)) * (window * window);
        float theta   = dot(L, normalize(-lights.spotLights[i].direction));
        float eps     = lights.spotLights[i].innerCos - lights.spotLights[i].outerCos;
        float cone    = clamp((theta - lights.spotLights[i].outerCos) / max(eps, 0.0001), 0.0, 1.0);
        if (cone <= 0.0) continue;
        float shadow  = (lights.spotLights[i].shadowIdx >= 0)
                        ? ShadowSpot(lights.spotLights[i].shadowIdx, fragWorldPos, N, L)
                        : 1.0;
        result += shadow * CalcLight(N, L, V,
            lights.spotLights[i].color, lights.spotLights[i].intensity * atten * cone,
            baseColor, fragRoughness, fragMetallic);
    }

    result += baseColor * fragEmissive;

    result = pow(max(result, vec3(0.0)), vec3(1.0 / 2.2));

    float alpha = fragColor.a * texture(albedoTex, fragUV).a;
    if (material.alphaThreshold > 0.0 && alpha < material.alphaThreshold)
        discard;
    outColor = vec4(result, alpha);
}

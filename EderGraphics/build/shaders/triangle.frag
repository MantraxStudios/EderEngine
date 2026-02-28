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
struct PointLight       { vec3 position;  float radius;    vec3 color; float intensity; };
struct SpotLight        { vec3 position; float innerCos; vec3 direction; float outerCos;
                          vec3 color; float intensity; float radius; float _p0; float _p1; float _p2; };

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
} lights;

layout(set = 1, binding = 1) uniform sampler2DArray shadowMap;

layout(location = 0) out vec4 outColor;

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

float ShadowFactor(vec3 worldPos, vec3 N, vec3 L)
{
    // Select cascade by view-space depth
    float depth = dot(worldPos - lights.cameraPos, lights.cameraForward);
    int cascade = 3;
    for (int i = 0; i < 4; i++)
    {
        if (depth < lights.cascadeSplits[i])
        {
            cascade = i;
            break;
        }
    }

    vec4 lightSpacePos = lights.cascadeMatrices[cascade] * vec4(worldPos, 1.0);
    vec3 projCoords    = lightSpacePos.xyz / lightSpacePos.w;
    vec2 uv            = projCoords.xy * 0.5 + 0.5;

    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || projCoords.z > 1.0)
        return 1.0;

    // Bias proportional to surface angle — eliminates acne without peter-panning
    float NdotL     = max(dot(N, L), 0.0);
    float bias      = max(0.0008 * (1.0 - NdotL), 0.0001);
    vec2  texelSize = 1.0 / vec2(textureSize(shadowMap, 0).xy);

    // 5×5 PCF with 1.5-texel spread for soft penumbra
    float shadow = 0.0;
    for (int x = -2; x <= 2; ++x)
    for (int y = -2; y <= 2; ++y)
    {
        vec2  offset  = vec2(x, y) * texelSize * 1.5;
        float closest = texture(shadowMap, vec3(uv + offset, float(cascade))).r;
        shadow += (projCoords.z - bias < closest) ? 1.0 : 0.0;
    }
    return shadow / 25.0;
}

void main()
{
    vec3 baseColor = fragColor.rgb * texture(albedoTex, fragUV).rgb;
    vec3 N         = normalize(fragNormal);
    vec3 V         = normalize(lights.cameraPos - fragWorldPos);

    vec3 result = baseColor * 0.04;

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
        result += CalcLight(N, toLight / dist, V,
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
        result += CalcLight(N, L, V,
            lights.spotLights[i].color, lights.spotLights[i].intensity * atten * cone,
            baseColor, fragRoughness, fragMetallic);
    }

    result += baseColor * fragEmissive;

    outColor = vec4(result, 1.0);
}

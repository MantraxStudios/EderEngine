#version 450

layout(location = 0) in  vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D sceneTex;
layout(set = 0, binding = 1) uniform sampler2D depthTex;

layout(set = 0, binding = 2) uniform FogUBO
{
    mat4  invViewProj;
    vec4  camPos;           // xyz = camera world position
    vec4  fogColor;         // xyz = base fog colour,    w = density
    vec4  horizonColor;     // xyz = horizon fog colour, w = heightFalloff
    vec4  sunScatterColor;  // xyz = sun glow colour,    w = scatter strength
    vec4  lightDir;         // xyz = direction toward sun (normalised), w = sun intensity
    vec4  params;           // x = heightOffset, y = maxFogAmount, z = fogStart, w = fogEnd
} ubo;

// ── Set 1 : LightBuffer ───────────────────────────────────────────────────────
#define MAX_DIR_LIGHTS   4
#define MAX_POINT_LIGHTS 16
#define MAX_SPOT_LIGHTS   8
#define MAX_SPOT_SHADOWS  4

struct DirectionalLight { vec3 direction; float intensity; vec3 color; float _pad; };
struct PointLight { vec3 position; float radius; vec3 color; float intensity;
                    int shadowIdx; float _p0; float _p1; float _p2; };
struct SpotLight  { vec3 position; float innerCos; vec3 direction; float outerCos;
                    vec3 color; float intensity; float radius; int shadowIdx;
                    float _p0; float _p1; };

// Correct std140 layout: dir → point → spot → counts (matches Lights.h exactly)
layout(set = 1, binding = 0) uniform LightUBO
{
    DirectionalLight dirLights[MAX_DIR_LIGHTS];      // 4 × 32  = 128 bytes
    PointLight       pointLights[MAX_POINT_LIGHTS];  // 16 × 48 = 768 bytes
    SpotLight        spotLights[MAX_SPOT_LIGHTS];    // 8 × 64  = 512 bytes
    int   numDirLights;
    int   numPointLights;
    int   numSpotLights;
    float _pad;
    vec3  cameraPos;    float _pad2;
    vec3  cameraFwd;    float _pad3;
    vec4  cascadeSplits;
    mat4  cascadeMatrices[4];
    mat4  spotMatrices[MAX_SPOT_SHADOWS];
    vec4  pointFarPlanes;
    vec4  skyAmbient;
    vec4  groundAmbient;
} lights;

// ── Helpers ───────────────────────────────────────────────────────────────────
vec3 ReconstructWorldPos(vec2 uv, float depth)
{
    vec4 ndc   = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 world = ubo.invViewProj * ndc;
    return world.xyz / world.w;
}

float MiePhase(float cosTheta, float g)
{
    float g2  = g * g;
    float num = (1.0 - g2) * (1.0 + cosTheta * cosTheta);
    float den = (2.0 + g2) * pow(max(1.0 + g2 - 2.0 * g * cosTheta, 0.0001), 1.5);
    return (3.0 / (8.0 * 3.14159265359)) * num / den;
}

float DistAtten(float dist, float radius)
{
    float x = clamp(dist / max(radius, 0.0001), 0.0, 1.0);
    float f = 1.0 - x * x;
    return f * f;
}

// Returns chord length through the sphere along the view ray (0 if it misses).
// Uses closest-approach distance; clamps t to [0, marchDist].
float RaySphereChord(vec3 origin, vec3 dir, float marchDist, vec3 center, float radius)
{
    vec3  oc = center - origin;
    float tc = clamp(dot(oc, dir), 0.0, marchDist);
    vec3  cl = origin + dir * tc;
    float d2 = dot(center - cl, center - cl);
    float r2 = radius * radius;
    if (d2 >= r2) return 0.0;
    return 2.0 * sqrt(r2 - d2);
}

// ── Main ─────────────────────────────────────────────────────────────────────
void main()
{
    vec3  sceneColor = texture(sceneTex, fragUV).rgb;
    float depth      = texture(depthTex,  fragUV).r;

    bool  isSky       = (depth >= 0.9999);
    float sampleDepth = isSky ? 0.9998 : depth;
    vec3  worldPos    = ReconstructWorldPos(fragUV, sampleDepth);

    vec3  camPos = ubo.camPos.xyz;
    vec3  toFrag = worldPos - camPos;
    float dist   = isSky ? ubo.params.w : length(toFrag);
    vec3  rayDir = normalize(toFrag);

    float density       = ubo.fogColor.w;
    float heightFalloff = ubo.horizonColor.w;
    float heightOffset  = ubo.params.x;
    float maxFogAmount  = ubo.params.y;
    float fogStart      = ubo.params.z;
    float fogEnd        = ubo.params.w;
    float marchDist     = min(dist, fogEnd);

    // ── Height-based exponential fog ─────────────────────────────────────────
    float avgH      = ((camPos.y) + (isSky ? camPos.y : worldPos.y)) * 0.5;
    float heightFog = density * exp(-heightFalloff * max(avgH - heightOffset, 0.0));

    float fogFactor = 1.0 - exp(-heightFog * max(marchDist - fogStart, 0.0));
    fogFactor = clamp(fogFactor, 0.0, maxFogAmount);
    fogFactor *= clamp((dist - fogStart) / max(fogStart * 0.15 + 0.5, 0.5), 0.0, 1.0);

    // ── Atmospheric colour (sun scatter) ─────────────────────────────────────
    float horizonBlend    = pow(1.0 - abs(rayDir.y), 3.0);
    vec3  baseFogColor    = mix(ubo.fogColor.xyz, ubo.horizonColor.xyz, horizonBlend);
    float cosTheta        = dot(rayDir, ubo.lightDir.xyz);
    float mie             = MiePhase(cosTheta, 0.76);
    float scatterStrength = ubo.sunScatterColor.w * ubo.lightDir.w;
    vec3  sunContrib      = ubo.sunScatterColor.xyz * mie * scatterStrength
                            * (isSky ? 0.3 : 1.0) * fogFactor;

    // ── Point light scatter (analytical ray-sphere) ───────────────────────────
    // Each point light casts a glowing halo through the fog volume.
    // Chord length = how "wide" the sphere looks along this ray.
    // Weighted by local attenuation and overall fog density so it fades
    // naturally where there is no fog.
    vec3 lightScatter = vec3(0.0);
    for (int p = 0; p < lights.numPointLights; p++)
    {
        vec3  lpos  = lights.pointLights[p].position;
        float lrad  = lights.pointLights[p].radius;

        float chord = RaySphereChord(camPos, rayDir, marchDist, lpos, lrad);
        if (chord <= 0.0) continue;

        // Distance from ray to light centre at closest point
        vec3  oc    = lpos - camPos;
        float tc    = clamp(dot(oc, rayDir), 0.0, marchDist);
        float dCl   = length(lpos - (camPos + rayDir * tc));

        float atten  = DistAtten(dCl, lrad);
        float weight = density * chord * atten * fogFactor;
        lightScatter += lights.pointLights[p].color
                      * lights.pointLights[p].intensity * weight;
    }

    // ── Spot light scatter (ray-sphere + cone check at closest point) ─────────
    for (int s = 0; s < lights.numSpotLights; s++)
    {
        vec3  lpos  = lights.spotLights[s].position;
        float lrad  = lights.spotLights[s].radius;

        float chord = RaySphereChord(camPos, rayDir, marchDist, lpos, lrad);
        if (chord <= 0.0) continue;

        vec3  oc  = lpos - camPos;
        float tc  = clamp(dot(oc, rayDir), 0.0, marchDist);
        vec3  cp  = camPos + rayDir * tc;

        // Cone factor at the closest point on the ray
        vec3  toCP  = normalize(cp - lpos);
        float cosA  = dot(toCP, lights.spotLights[s].direction);
        float inner = lights.spotLights[s].innerCos;
        float outer = lights.spotLights[s].outerCos;
        float cone  = clamp((cosA - outer) / max(inner - outer, 0.0001), 0.0, 1.0);
        if (cone < 0.001) continue;

        float dCl    = length(lpos - cp);
        float atten  = DistAtten(dCl, lrad);
        float weight = density * chord * atten * cone * fogFactor;
        lightScatter += lights.spotLights[s].color
                      * lights.spotLights[s].intensity * weight;
    }

    // ── Composite ─────────────────────────────────────────────────────────────
    vec3 finalFogColor = baseFogColor + sunContrib + lightScatter;
    outColor = vec4(mix(sceneColor, finalFogColor, fogFactor), 1.0);
}

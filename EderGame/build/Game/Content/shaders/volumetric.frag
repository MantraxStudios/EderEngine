#version 450

layout(location = 0) in  vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D      sceneTex;
layout(set = 0, binding = 1) uniform sampler2D      depthTex;
layout(set = 0, binding = 2) uniform sampler2DArray shadowMapTex;

layout(set = 0, binding = 3) uniform VolumetricUBO
{
    mat4  invViewProj;
    mat4  shadowMatrix[4];
    vec4  cascadeSplits;
    vec4  lightDir;
    vec4  lightColor;
    vec4  camPosMaxDist;
    vec4  params;
    ivec4 iparams;
    vec4  tint;
} ubo;

#define MAX_DIR_LIGHTS   4
#define MAX_POINT_LIGHTS 16
#define MAX_SPOT_LIGHTS  8
#define MAX_SPOT_SHADOWS 4

struct DirectionalLight { vec3 direction; float intensity; vec3 color; float _pad; };
struct PointLight       { vec3 position; float radius; vec3 color; float intensity;
                          int shadowIdx; float _p0; float _p1; float _p2; };
struct SpotLight        { vec3 position; float innerCos; vec3 direction; float outerCos;
                          vec3 color; float intensity; float radius; int shadowIdx;
                          float _p0; float _p1; };

layout(set = 1, binding = 0) uniform LightUBO
{
    DirectionalLight dirLights[MAX_DIR_LIGHTS];
    PointLight       pointLights[MAX_POINT_LIGHTS];
    SpotLight        spotLights[MAX_SPOT_LIGHTS];
    int              numDirLights;
    int              numPointLights;
    int              numSpotLights;
    float            _pad;
    vec3             cameraPos;    float _pad2;
    vec3             cameraForward; float _pad3;
    vec4             cascadeSplits;
    mat4             cascadeMatrices[4];
    mat4             spotMatrices[MAX_SPOT_SHADOWS];
    vec4             pointFarPlanes;
    vec4             skyAmbient;
    vec4             groundAmbient;
} lights;

layout(set = 1, binding = 2) uniform sampler2DArray   spotShadowMap;
layout(set = 1, binding = 3) uniform samplerCubeArray pointShadowMap;

float IGN(vec2 pixelPos)
{
    return fract(52.9829189 * fract(0.06711056 * pixelPos.x + 0.00583715 * pixelPos.y));
}

bool SphereIntersectsRay(vec3 origin, vec3 dir, float tMax, vec3 center, float radius)
{
    vec3  oc = center - origin;
    float tc = clamp(dot(oc, dir), 0.0, tMax);
    vec3  cl = origin + dir * tc;
    return dot(center - cl, center - cl) <= radius * radius;
}

float HenyeyGreenstein(float cosTheta, float g)
{
    float g2  = g * g;
    float den = max(1.0 + g2 - 2.0 * g * cosTheta, 0.0001);
    return (1.0 - g2) / (4.0 * 3.14159265359 * pow(den, 1.5));
}

float SampleCascade(vec3 worldPos, int cascade)
{
    vec4  sc  = ubo.shadowMatrix[cascade] * vec4(worldPos, 1.0);
    sc.xyz   /= sc.w;
    vec2 uv   = sc.xy * 0.5 + 0.5;
    float ref = sc.z;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 ||
        ref  < 0.0 || ref  > 1.0)
        return 1.0;
    return (texture(shadowMapTex, vec3(uv, float(cascade))).r >= ref - 0.0005) ? 1.0 : 0.0;
}

float DirVisibility(vec3 worldPos, float viewDist)
{
    int cascade = 3;
    if      (viewDist < ubo.cascadeSplits.x) cascade = 0;
    else if (viewDist < ubo.cascadeSplits.y) cascade = 1;
    else if (viewDist < ubo.cascadeSplits.z) cascade = 2;
    return SampleCascade(worldPos, cascade);
}

float SpotVisibility(vec3 worldPos, int slot)
{
    vec4  sc  = lights.spotMatrices[slot] * vec4(worldPos, 1.0);
    sc.xyz   /= sc.w;
    vec2 uv   = sc.xy * 0.5 + 0.5;
    float ref = sc.z;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 ||
        ref  < 0.0 || ref  > 1.0)
        return 1.0;
    return (texture(spotShadowMap, vec3(uv, float(slot))).r >= ref - 0.002) ? 1.0 : 0.0;
}

float PointVisibility(vec3 worldPos, vec3 lightPos, int slot, float farPlane)
{
    vec3  dir    = worldPos - lightPos;
    float dist   = length(dir);
    float stored = texture(pointShadowMap, vec4(dir, float(slot))).r * farPlane;
    return (stored >= dist - 0.15) ? 1.0 : 0.0;
}

float DistAtten(float dist, float radius)
{
    float x      = clamp(dist / max(radius, 0.0001), 0.0, 1.0);
    float window = max(0.0, 1.0 - x * x * x * x);
    return window * window;
}

vec3 ReconstructWorldPos(vec2 uv, float depth)
{
    vec4 ndc   = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 world = ubo.invViewProj * ndc;
    return world.xyz / world.w;
}

void main()
{
    vec3  sceneColor = texture(sceneTex, fragUV).rgb;
    float depth      = texture(depthTex,  fragUV).r;

    bool  isSky       = (depth >= 0.9999);
    float sampleDepth = isSky ? 0.9998 : depth;

    vec3  fragWorldPos = ReconstructWorldPos(fragUV, sampleDepth);
    vec3  camPos       = ubo.camPosMaxDist.xyz;
    float maxDist      = ubo.camPosMaxDist.w;
    vec3  toFrag       = fragWorldPos - camPos;
    float fragDist     = length(toFrag);
    vec3  rayDir       = toFrag / max(fragDist, 0.0001);

    float marchDist  = maxDist;
    float sceneDist  = isSky ? maxDist : min(fragDist, maxDist);
    int   steps      = ubo.iparams.x;

    float density    = ubo.params.x;
    float absorption = ubo.params.y;
    float g          = ubo.params.z;
    float jitter     = ubo.params.w;
    float finalMult  = ubo.tint.w;
    vec3  tint       = ubo.tint.xyz;
    float extinction = density + absorption;
    float stepSize   = marchDist / float(max(steps, 1));
    float stepT      = exp(-extinction * stepSize);

    float sceneTransmittance = exp(-extinction * sceneDist);

    float noise  = IGN(gl_FragCoord.xy);
    float startT = stepSize * noise * jitter;

    float dirCos   = dot(rayDir, ubo.lightDir.xyz);
    float dirPhase = HenyeyGreenstein(dirCos, g);
    vec3  dirColor = ubo.lightColor.xyz * ubo.lightColor.w * tint * dirPhase;

    vec3  ambientLight = (lights.skyAmbient.rgb + lights.groundAmbient.rgb) * 0.5;

    bool pointActive[MAX_POINT_LIGHTS];
    for (int p = 0; p < lights.numPointLights; p++)
        pointActive[p] = SphereIntersectsRay(camPos, rayDir, marchDist,
                                              lights.pointLights[p].position,
                                              lights.pointLights[p].radius);
    bool spotActive[MAX_SPOT_LIGHTS];
    for (int s = 0; s < lights.numSpotLights; s++)
        spotActive[s]  = SphereIntersectsRay(camPos, rayDir, marchDist,
                                              lights.spotLights[s].position,
                                              lights.spotLights[s].radius);

    vec3  scatter       = vec3(0.0);
    float transmittance = 1.0;

    for (int i = 0; i < steps; i++)
    {
        float t          = startT + float(i) * stepSize;
        float stepWeight = transmittance * (1.0 - stepT) / max(extinction, 1e-7);
        vec3  pos        = camPos + rayDir * t;
        float inScene    = (t <= sceneDist) ? 1.0 : 0.0;

        scatter += ambientLight * tint * density * stepWeight * inScene;

        float dirVis = DirVisibility(pos, t);
        scatter += dirColor * density * dirVis * stepWeight * inScene;

        for (int p = 0; p < lights.numPointLights; p++)
        {
            if (!pointActive[p]) continue;

            vec3  lpos  = lights.pointLights[p].position;
            float lrad  = lights.pointLights[p].radius;
            vec3  ltoP  = lpos - pos;
            float dist  = length(ltoP);
            if (dist > lrad) continue;

            vec3  ldir  = ltoP / dist;
            float atten = DistAtten(dist, lrad);
            float cosA  = dot(rayDir, ldir);
            float phase = HenyeyGreenstein(cosA, g);

            int   sidx  = lights.pointLights[p].shadowIdx;
            float farP  = (sidx >= 0) ? lights.pointFarPlanes[sidx] : lrad;
            float vis   = (sidx >= 0) ? PointVisibility(pos, lpos, sidx, farP) : 1.0;

            vec3 lcolor = lights.pointLights[p].color * lights.pointLights[p].intensity;
            scatter += lcolor * tint * density * phase * atten * vis * stepWeight * inScene;
        }

        for (int s = 0; s < lights.numSpotLights; s++)
        {
            if (!spotActive[s]) continue;

            vec3  lpos  = lights.spotLights[s].position;
            float lrad  = lights.spotLights[s].radius;
            vec3  ltoP  = lpos - pos;
            float dist  = length(ltoP);
            if (dist > lrad) continue;

            vec3  ldir     = ltoP / dist;
            float atten    = DistAtten(dist, lrad);
            float cosA     = dot(-ldir, lights.spotLights[s].direction);
            float inner    = lights.spotLights[s].innerCos;
            float outer    = lights.spotLights[s].outerCos;
            float coneFact = clamp((cosA - outer) / max(inner - outer, 0.0001), 0.0, 1.0);
            if (coneFact < 0.001) continue;

            float phaseA = dot(rayDir, ldir);
            float phase  = HenyeyGreenstein(phaseA, g);

            int   sidx = lights.spotLights[s].shadowIdx;
            float vis  = (sidx >= 0) ? SpotVisibility(pos, sidx) : 1.0;

            vec3 lcolor = lights.spotLights[s].color * lights.spotLights[s].intensity;
            scatter += lcolor * tint * density * phase * atten * coneFact * vis * stepWeight * inScene;
        }

        transmittance *= stepT;
        if (transmittance < 0.001) break;
    }

    float transAlpha = isSky ? 1.0 : texture(sceneTex, fragUV).a;

    vec3 finalScatter = scatter * finalMult * transAlpha;
    outColor = vec4(sceneColor * sceneTransmittance + finalScatter, 1.0);
}

#version 450
#define PI 3.14159265358979323846

layout(location = 0) in  vec2 fragNDC;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform Push
{
    mat4 invViewProj;
    vec4 sunDir;        // xyz = normalized direction toward sun, w = intensity
};

// ---------------------------------------------------------------------------
// Atmospheric constants — tuned for a warm, Unreal/Unity-like look.
// Less extreme blue dominance, stronger Mie glow.
// ---------------------------------------------------------------------------
const float EARTH_R = 6371e3;
const float ATMOS_R = 6471e3;
const float H_R     = 8500.0;
const float H_M     = 1200.0;
const vec3  BETA_R  = vec3(5.5e-6, 13.0e-6, 22.4e-6); // reduced blue for warmer sky
const float BETA_M  = 21.0e-6;
const float G       = 0.82;            // stronger forward Mie scattering (glowing corona)

// ---------------------------------------------------------------------------
vec2 sphereIntersect(vec3 ro, vec3 rd, float r)
{
    float b = dot(ro, rd);
    float c = dot(ro, ro) - r * r;
    float d = b * b - c;
    if (d < 0.0) return vec2(1.0, -1.0);
    float s = sqrt(d);
    return vec2(-b - s, -b + s);
}

float phaseR(float cosTheta)
{
    return (3.0 / (16.0 * PI)) * (1.0 + cosTheta * cosTheta);
}

float phaseM(float cosTheta)
{
    float g2 = G * G;
    return (3.0 / (8.0 * PI))
         * ((1.0 - g2) * (1.0 + cosTheta * cosTheta))
         / ((2.0 + g2) * pow(max(1.0 + g2 - 2.0 * G * cosTheta, 0.0001), 1.5));
}

// ---------------------------------------------------------------------------
// Nishita-style single scattering — returns HDR linear color (no tonemap here).
// rd.y must be >= 0. Sun disk NOT included (handled in main to avoid stretching).
// ---------------------------------------------------------------------------
vec3 ComputeSky(vec3 rd, vec3 sun, float intensity)
{
    vec3 ro = vec3(0.0, EARTH_R, 0.0);

    vec2 pa = sphereIntersect(ro, rd, ATMOS_R);
    if (pa.x > pa.y) return vec3(0.0);

    float tMax    = pa.y;
    float tMin    = max(pa.x, 0.0);
    float cosTheta = dot(rd, sun);
    float rPhase   = phaseR(cosTheta);
    float mPhase   = phaseM(cosTheta);

    vec3  optR = vec3(0.0);
    float optM = 0.0;
    vec3  sumR = vec3(0.0);
    vec3  sumM = vec3(0.0);

    const int   N_PRIMARY = 16;
    const int   N_SHADOW  = 6;
    float stepLen = (tMax - tMin) / float(N_PRIMARY);

    for (int i = 0; i < N_PRIMARY; i++)
    {
        vec3  pos    = ro + rd * (tMin + (float(i) + 0.5) * stepLen);
        float height = max(0.0, length(pos) - EARTH_R);

        float hr = exp(-height / H_R) * stepLen;
        float hm = exp(-height / H_M) * stepLen;
        optR += BETA_R * hr;
        optM += BETA_M * hm;

        vec2  ls      = sphereIntersect(pos, sun, ATMOS_R);
        float lMax    = ls.y;
        float lStep   = lMax / float(N_SHADOW);
        vec3  lOptR   = vec3(0.0);
        float lOptM   = 0.0;
        bool  blocked = false;

        for (int j = 0; j < N_SHADOW; j++)
        {
            vec3  lp = pos + sun * ((float(j) + 0.5) * lStep);
            float lh = length(lp) - EARTH_R;
            if (lh < 0.0) { blocked = true; break; }
            lOptR += BETA_R * exp(-lh / H_R) * lStep;
            lOptM += BETA_M * exp(-lh / H_M) * lStep;
        }
        if (blocked) continue;

        vec3 attn = exp(-(optR + lOptR) - (optM + lOptM));
        sumR += hr * attn;
        sumM += hm * attn;
    }

    return intensity * (sumR * rPhase * BETA_R + sumM * mPhase * BETA_M);
}

// Simple hash for star field
float hash2(vec2 p)
{
    p = fract(p * vec2(0.1031, 0.1030));
    p += dot(p, p.yx + 33.33);
    return fract((p.x + p.y) * p.x);
}

// Returns [0,1] star brightness for this ray using cube-face projection.
// No polar stretching because each cube face is a flat uniform 2D grid.
float StarField(vec3 rd, float density)
{
    vec3 a = abs(rd);
    vec2 uv;
    float faceId;
    if (a.x >= a.y && a.x >= a.z) {
        uv     = rd.yz / a.x;
        faceId = rd.x > 0.0 ? 0.0 : 1.0;
    } else if (a.y >= a.x && a.y >= a.z) {
        uv     = rd.xz / a.y;
        faceId = rd.y > 0.0 ? 2.0 : 3.0;
    } else {
        uv     = rd.xy / a.z;
        faceId = rd.z > 0.0 ? 4.0 : 5.0;
    }
    const float GRID = 60.0;          // cells per face side — tweak for star density
    vec2  cell    = floor(uv * GRID);
    vec2  cellUV  = fract(uv * GRID); // position within cell [0,1]

    float s      = hash2(cell + vec2(faceId * 13.7, faceId * 7.3));   // existence
    float bright = hash2(cell + vec2(faceId * 3.1,  faceId * 19.5));  // brightness

    // Random position of the star within its cell (kept away from edges)
    vec2 jitter = vec2(
        0.15 + 0.7 * hash2(cell + vec2(faceId * 5.23, 17.71)),
        0.15 + 0.7 * hash2(cell + vec2(faceId * 2.71, 31.41))
    );

    // Circular distance to the star centre inside the cell
    float dist   = length(cellUV - jitter);
    // Smooth circular star — radius ~8% of cell size (pinpoint but round)
    float circle = smoothstep(0.08, 0.0, dist);

    return step(1.0 - density, s) * (0.5 + 0.5 * bright) * circle;
}

// ---------------------------------------------------------------------------
void main()
{
    vec4 near = invViewProj * vec4(fragNDC, 0.0, 1.0);
    vec4 far  = invViewProj * vec4(fragNDC, 1.0, 1.0);
    near /= near.w;
    far  /= far.w;
    vec3 rayDir = normalize(far.xyz - near.xyz);

    vec3  sun       = normalize(sunDir.xyz);
    float intensity = sunDir.w;

    float sunY         = sun.y;                                    // altitude of sun
    float sunHorizon   = clamp(sunY * 5.0 + 1.0, 0.0, 1.0);      // 0 when sun 0.2 below
    float effIntensity = intensity * sunHorizon;

    // ---- Sky ray: all below-horizon rays map to the horizon ring ----
    // belowAmt: 0 at/above horizon, 1 looking straight down
    float belowAmt = clamp(-rayDir.y * 12.0, 0.0, 1.0);

    vec3 skyRay = rayDir;
    if (skyRay.y < 0.0)
    {
        skyRay.y = 0.0002;
        skyRay   = normalize(skyRay);
    }

    // ---- Atmosphere scatter (HDR) ----
    vec3 hdrSky = ComputeSky(skyRay, sun, effIntensity);

    // ---- Sun disk — gated on ORIGINAL rayDir.y to prevent below-horizon stretch ----
    float rayAbove = clamp(rayDir.y * 50.0, 0.0, 1.0);  // sharp gate: 0 below horizon
    float cosRaw   = dot(rayDir, sun);
    float sunFade  = clamp(sunY * 20.0 + 1.0, 0.0, 1.0);
    float sunDisk  = smoothstep(0.9997, 1.0, cosRaw) * sunFade * rayAbove;
    hdrSky += vec3(1.5, 1.25, 0.85) * sunDisk * intensity;

    // ---- Warm golden-hour horizon band ----
    // Only within ~5° above the actual horizon, only when sun is near horizon
    float sunNearHorizon = clamp(1.0 - abs(sunY) * 10.0, 0.0, 1.0) * sunHorizon;
    vec2  sunAz   = normalize(vec2(sun.x, sun.z) + vec2(1e-5));
    vec2  rayAz   = normalize(vec2(skyRay.x, skyRay.z) + vec2(1e-5));
    float azAlign = clamp(dot(rayAz, sunAz) * 0.5 + 0.5, 0.0, 1.0);
    // Tight band: only rays within 5° of the horizon (rayDir.y < 0.09)
    float rayNearHorizon = clamp(1.0 - rayDir.y * 22.0, 0.0, 1.0);
    float hBand   = sunNearHorizon * azAlign * rayNearHorizon;
    hdrSky += vec3(2.5, 0.85, 0.10) * hBand * 0.12 * effIntensity;

    // ---- Tone-map (filmic Reinhard per-channel) + gamma ----
    vec3 skyColor = hdrSky / (hdrSky + vec3(1.0));
    skyColor      = pow(max(skyColor, vec3(0.0)), vec3(1.0 / 2.2));

    // ---- Night sky ----
    float nightAmt = clamp(-sunY * 3.5, 0.0, 1.0);
    // Dark navy base
    vec3 nightBase = vec3(0.008, 0.014, 0.030);
    skyColor = mix(skyColor, max(skyColor, nightBase), nightAmt * clamp(rayDir.y * 2.0, 0.0, 1.0));

    // Stars: only above horizon, only at night
    if (nightAmt > 0.05 && rayDir.y > 0.0)
    {
        float starNight = nightAmt * clamp(rayDir.y * 3.0, 0.0, 1.0);
        // Cube-face grid — no polar stretching
        float vis = StarField(rayDir, 0.022);  // 0.022 ≈ density, ~2% of cells have a star
        skyColor += vec3(0.75, 0.85, 1.0) * vis * starNight;
    }

    // ---- Ground / below-horizon ----
    vec3 groundHDR   = vec3(0.10, 0.08, 0.06) * max(effIntensity * 0.28 + 0.015, 0.015);
    vec3 groundColor = groundHDR / (groundHDR + vec3(1.0));
    groundColor      = pow(max(groundColor, vec3(0.0)), vec3(1.0 / 2.2));

    // Smooth seam: carry horizon sky into the top of the ground band
    vec3 seam   = skyColor * clamp(1.0 - belowAmt * 5.0, 0.0, 1.0);
    groundColor = mix(seam, groundColor, smoothstep(0.0, 1.0, belowAmt));

    vec3 result = mix(skyColor, groundColor, belowAmt);

    outColor = vec4(result, 1.0);
}

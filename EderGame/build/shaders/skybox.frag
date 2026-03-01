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
// Atmospheric constants (Earth-scale, SI)
// ---------------------------------------------------------------------------
const float EARTH_R = 6371e3;          // Earth radius (m)
const float ATMOS_R = 6471e3;          // Atmosphere top radius (m)
const float H_R     = 8500.0;          // Rayleigh scale height (m)
const float H_M     = 1200.0;          // Mie scale height (m)
const vec3  BETA_R  = vec3(5.8e-6, 13.5e-6, 33.1e-6); // Rayleigh coefficients
const float BETA_M  = 21.0e-6;         // Mie coefficient
const float G       = 0.76;            // Mie anisotropy factor

// ---------------------------------------------------------------------------
// Ray-sphere intersection. Returns (tNear, tFar); tFar < tNear = no hit.
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

// ---------------------------------------------------------------------------
// Rayleigh phase function
// ---------------------------------------------------------------------------
float phaseR(float cosTheta)
{
    return (3.0 / (16.0 * PI)) * (1.0 + cosTheta * cosTheta);
}

// ---------------------------------------------------------------------------
// Henyey-Greenstein Mie phase function
// ---------------------------------------------------------------------------
float phaseM(float cosTheta)
{
    float g2 = G * G;
    return (3.0 / (8.0 * PI))
         * ((1.0 - g2) * (1.0 + cosTheta * cosTheta))
         / ((2.0 + g2) * pow(max(1.0 + g2 - 2.0 * G * cosTheta, 0.0001), 1.5));
}

// ---------------------------------------------------------------------------
// Main atmosphere integration (Nishita-style single-scattering)
// Primary ray: 8 samples. Secondary (sun) ray: 4 samples.
// ---------------------------------------------------------------------------
vec3 ComputeSky(vec3 rd, vec3 sun, float intensity)
{
    // Rays below the horizon map to the horizon color (camera is on Earth surface)
    if (rd.y < 0.0) { rd.y = 0.001; rd = normalize(rd); }

    // Observer at ground level, planet center at origin
    vec3 ro = vec3(0.0, EARTH_R, 0.0);

    // Primary ray: find atmosphere entry/exit
    vec2 pa = sphereIntersect(ro, rd, ATMOS_R);
    if (pa.x > pa.y) return vec3(0.0);

    float tMax = pa.y;
    float tMin = max(pa.x, 0.0);

    float cosTheta = dot(rd, sun);
    float rPhase   = phaseR(cosTheta);
    float mPhase   = phaseM(cosTheta);

    // Accumulated optical depth from camera along primary ray
    vec3  optR = vec3(0.0);
    float optM = 0.0;

    // Accumulated in-scatter
    vec3 sumR = vec3(0.0);
    vec3 sumM = vec3(0.0);

    const int N_PRIMARY = 8;
    float stepLen = (tMax - tMin) / float(N_PRIMARY);

    for (int i = 0; i < N_PRIMARY; i++)
    {
        vec3  pos    = ro + rd * (tMin + (float(i) + 0.5) * stepLen);
        float height = max(0.0, length(pos) - EARTH_R);

        float hr = exp(-height / H_R) * stepLen;
        float hm = exp(-height / H_M) * stepLen;
        optR += BETA_R * hr;
        optM += BETA_M * hm;

        // Secondary ray toward the sun
        vec2  ls     = sphereIntersect(pos, sun, ATMOS_R);
        float lMax   = ls.y;
        float lStep  = lMax / 4.0;
        vec3  lOptR  = vec3(0.0);
        float lOptM  = 0.0;
        bool  blocked = false;

        for (int j = 0; j < 4; j++)
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

    vec3 color = intensity * (sumR * rPhase * BETA_R + sumM * mPhase * BETA_M);

    // Sun disk
    float sunDisk = smoothstep(0.9998, 1.0, cosTheta);
    color += vec3(1.0, 0.97, 0.85) * sunDisk * intensity * 0.8;

    // Reinhard tone map + gamma correction
    color = color / (color + vec3(1.0));
    color = pow(max(color, vec3(0.0)), vec3(1.0 / 2.2));

    return color;
}

// ---------------------------------------------------------------------------
void main()
{
    // Reconstruct world-space ray from NDC position using inverse view-proj
    vec4 near = invViewProj * vec4(fragNDC, 0.0, 1.0);
    vec4 far  = invViewProj * vec4(fragNDC, 1.0, 1.0);
    near /= near.w;
    far  /= far.w;
    vec3 rayDir = normalize(far.xyz - near.xyz);

    vec3 sun       = normalize(sunDir.xyz);
    float intensity = sunDir.w;

    outColor = vec4(ComputeSky(rayDir, sun, intensity), 1.0);
}

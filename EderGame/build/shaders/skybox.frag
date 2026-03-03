#version 450
#define PI 3.14159265358979323846

layout(location = 0) in  vec2 inUVs;
layout(location = 0) out vec4 finalImage;

layout(set = 0, binding = 0) uniform SkyboxCamera {
    mat4 inverseView;
    mat4 inverseProj;
    vec3 cameraPosition;
} camData;

layout(push_constant) uniform Push
{
    vec4 sunDir;
};

const float EARTH_R    = 6371e3;
const float ATMOS_R    = 6471e3;
const float H_R        = 8000.0;
const float H_M        = 1200.0;
const vec3  BETA_R     = vec3(5.5e-6, 13.0e-6, 22.4e-6);
const float BETA_M     = 2.0e-6;
const float BETA_M_EXT = 2.22e-6;
const float G          = 0.76;

vec2 sphereIntersect(vec3 ro, vec3 rd, float r)
{
    float b = dot(ro, rd);
    float c = dot(ro, ro) - r * r;
    float d = b * b - c;
    if (d < 0.0) return vec2(1.0, -1.0);
    float s = sqrt(d);
    return vec2(-b - s, -b + s);
}

float phaseR(float cosA)
{
    return (3.0 / (16.0 * PI)) * (1.0 + cosA * cosA);
}

float phaseM(float cosA)
{
    float g2    = G * G;
    float denom = max(1.0 + g2 - 2.0 * G * cosA, 1e-4);
    return (3.0 / (8.0 * PI)) * ((1.0 - g2) * (1.0 + cosA * cosA))
         / ((2.0 + g2) * pow(denom, 1.5));
}

vec3 ComputeSky(vec3 rd, vec3 sun, float intensity)
{
    vec3 ro = vec3(0.0, EARTH_R + 1.0, 0.0);

    vec2 pa = sphereIntersect(ro, rd, ATMOS_R);
    if (pa.x > pa.y) return vec3(0.0);

    float tMin   = max(pa.x, 0.0);
    float tMax   = pa.y;
    float cosA   = dot(rd, sun);
    float rP     = phaseR(cosA);
    float mP     = phaseM(cosA);

    const int NP = 32;
    const int NS = 8;
    float segLen = (tMax - tMin) / float(NP);

    vec3  accumOptR = vec3(0.0);
    float accumOptM = 0.0;
    vec3  sumR = vec3(0.0);
    vec3  sumM = vec3(0.0);

    for (int i = 0; i < NP; ++i)
    {
        float t = tMin + (float(i) + 0.5) * segLen;
        vec3  p = ro + rd * t;
        float h = max(0.0, length(p) - EARTH_R);

        float densR = exp(-h / H_R);
        float densM = exp(-h / H_M);

        accumOptR += BETA_R     * densR * segLen;
        accumOptM += BETA_M_EXT * densM * segLen;

        vec2  ls = sphereIntersect(p, sun, ATMOS_R);
        if (ls.y < 0.0) continue;

        float lSeg    = ls.y / float(NS);
        vec3  lOptR   = vec3(0.0);
        float lOptM   = 0.0;
        bool  blocked = false;

        for (int j = 0; j < NS; ++j)
        {
            vec3  lp = p + sun * ((float(j) + 0.5) * lSeg);
            float lh = length(lp) - EARTH_R;
            if (lh < 0.0) { blocked = true; break; }
            lOptR += BETA_R     * exp(-lh / H_R) * lSeg;
            lOptM += BETA_M_EXT * exp(-lh / H_M) * lSeg;
        }
        if (blocked) continue;

        vec3 tau = (accumOptR + lOptR) + vec3(accumOptM + lOptM);
        vec3 att = exp(-tau);

        sumR += densR * segLen * att;
        sumM += densM * segLen * att;
    }

    vec3 scatter = intensity * (sumR * rP * BETA_R + sumM * mP * BETA_M);

    float msBlend = clamp(-sun.y * 4.0 + 0.5, 0.0, 1.0)
                  * clamp(rd.y * 2.0, 0.0, 1.0);
    scatter += vec3(0.008, 0.016, 0.040) * intensity * msBlend;

    return scatter;
}

vec3 ACESFilm(vec3 x)
{
    float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

float hash2(vec2 p)
{
    p  = fract(p * vec2(0.1031, 0.1030));
    p += dot(p, p.yx + 33.33);
    return fract((p.x + p.y) * p.x);
}

float StarField(vec3 rd, float density)
{
    vec3  a = abs(rd);
    vec2  uv;
    float fid;

    if      (a.x >= a.y && a.x >= a.z) { uv = rd.yz / a.x; fid = rd.x > 0.0 ? 0.0 : 1.0; }
    else if (a.y >= a.x && a.y >= a.z) { uv = rd.xz / a.y; fid = rd.y > 0.0 ? 2.0 : 3.0; }
    else                                { uv = rd.xy / a.z; fid = rd.z > 0.0 ? 4.0 : 5.0; }

    const float GRID = 60.0;
    vec2  cell = floor(uv * GRID);
    vec2  cuv  = fract(uv * GRID);
    float s    = hash2(cell + vec2(fid * 13.7,  fid * 7.3));
    float b    = hash2(cell + vec2(fid * 3.1,   fid * 19.5));
    vec2  jit  = vec2(0.15 + 0.7 * hash2(cell + vec2(fid * 5.23, 17.71)),
                      0.15 + 0.7 * hash2(cell + vec2(fid * 2.71, 31.41)));
    float circ = smoothstep(0.08, 0.0, length(cuv - jit));
    return step(1.0 - density, s) * (0.5 + 0.5 * b) * circ;
}

void main()
{
    vec4 clipPos  = vec4(inUVs.x * 2.0 - 1.0, inUVs.y * 2.0 - 1.0, 1.0, 1.0);
    vec4 viewPos  = camData.inverseProj * clipPos;
    viewPos      /= viewPos.w;
    vec4 worldPos = camData.inverseView * viewPos;
    vec3 rd       = normalize(worldPos.xyz - camData.cameraPosition);
    rd.x = -rd.x;

    vec3  sun  = normalize(sunDir.xyz);
    float itn  = sunDir.w;
    float sunY = sun.y;

    float sunH = smoothstep(-0.04, 0.15, sunY);
    float effI = itn * sunH;

    vec3 skyRd = rd;
    if (skyRd.y < 0.0001) skyRd.y = 0.0001;
    skyRd = normalize(skyRd);

    vec3 hdr = ComputeSky(skyRd, sun, effI);

    float cosR  = dot(rd, sun);
    float above = smoothstep(-0.005, 0.02, rd.y);
    float disk  = smoothstep(0.99975, 1.00000, cosR) * sunH * above;

    float hTint = clamp(1.0 - sunY * 3.0, 0.0, 1.0);
    vec3  dcol  = mix(vec3(1.5, 1.3, 1.1), vec3(1.8, 0.95, 0.45), hTint);
    hdr += dcol * disk * effI;

    float snh = clamp(1.0 - abs(sunY) * 6.0, 0.0, 1.0) * sunH * sunH;
    vec2  saz = normalize(sun.xz   + vec2(1e-5));
    vec2  raz = normalize(skyRd.xz + vec2(1e-5));
    float az  = clamp(dot(raz, saz) * 0.5 + 0.5, 0.0, 1.0);
    az        = az * az * az;
    float vb  = clamp(1.0 - skyRd.y * 8.0, 0.0, 1.0);
    hdr += vec3(2.2, 0.65, 0.06) * snh * az * vb * 0.04 * effI;

    float twi  = clamp(1.0 - abs(sunY) * 6.0, 0.0, 1.0) * sunH * sunH;
    float uzon = clamp(skyRd.y * 1.5, 0.0, 1.0);
    hdr += vec3(0.08, 0.03, 0.20) * twi * uzon * itn * 0.08;

    vec3 sky = ACESFilm(hdr * 0.50);
    sky = pow(max(sky, 0.0), vec3(1.0 / 2.2));

    float night = clamp(-sunY * 4.0, 0.0, 1.0);
    float upY   = clamp(rd.y * 2.0, 0.0, 1.0);
    sky = mix(sky, max(sky, vec3(0.004, 0.008, 0.020)), night * upY);

    if (night > 0.05 && rd.y > 0.0)
    {
        float sn = night * clamp(rd.y * 3.0, 0.0, 1.0);
        sky += vec3(0.80, 0.90, 1.00) * StarField(rd, 0.020) * sn;
    }

    float belowT = clamp(-rd.y * 12.0, 0.0, 1.0);
    vec3  gHDR   = vec3(0.08, 0.065, 0.050) * clamp(effI * 0.20 + 0.010, 0.010, 1.0);
    vec3  gCol   = pow(max(ACESFilm(gHDR * 0.50), 0.0), vec3(1.0 / 2.2));
    gCol = mix(gCol, gCol * vec3(1.1, 0.75, 0.50), snh * 0.5);

    float blend = smoothstep(0.0, 1.0, belowT);
    finalImage = vec4(mix(sky, gCol, blend), 1.0);
}

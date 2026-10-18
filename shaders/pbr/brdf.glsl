// Shared PBR BRDF. All quantities are scene-linear radiometric values; the caller owns the units.
//
//   N, V, L  unit vectors in one space; N faces the viewer side, V points to the viewer, L to the light.
//   evalBsdf returns f(V, L) in sr^-1 (no cosine); multiply by max(dot(N, L), 0) for reflected radiance.
//
// Define PBR_DFG_LOOKUP(nov, roughness) -> vec4 before including to supply the table built by
// GPP::Pbr::BuildDfgTable (x = A, y = B, z = A average, w = B average, bilinear in nov and perceptual
// roughness, texel i centred at i / (size - 1)). Without it the built-in 32x32 table of dfg_table.glsl is used.
#ifndef GPP_PBR_BRDF_GLSL
#define GPP_PBR_BRDF_GLSL

const float PBR_PI = 3.14159265358979323846;
const float PBR_MIN_ROUGHNESS = 0.045;
const float PBR_EPSILON = 1e-4;

struct PbrSurface
{
    vec3 baseColor;
    float metallic;
    float roughness;
    float ior;
    vec3 specularColor;
    float specularFactor;
};

struct PbrLobes
{
    vec3 diffuse;
    vec3 f0;
    vec3 f90;
    vec3 fms;
    vec3 albedoView;
    vec3 albedoAverage;
    float alpha;
    float roughness;
    float nov;
    float eView;
    float eAverage;
    float specularProbability;
};

PbrSurface pbrDefaultSurface()
{
    return PbrSurface(vec3(1.0), 0.0, 0.5, 1.5, vec3(1.0), 1.0);
}

float pbrLuminance(vec3 c)
{
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

float pbrF0FromIor(float ior)
{
    float r = (ior - 1.0) / (ior + 1.0);
    return r * r;
}

float pbrSq(float x)
{
    return x * x;
}

#ifndef PBR_DFG_LOOKUP
#include "dfg_table.glsl"
vec4 pbrDfgBuiltin(float nov, float roughness)
{
    float x = clamp(nov, 0.0, 1.0) * float(PBR_DFG_TABLE_WIDTH - 1);
    float y = clamp(roughness, 0.0, 1.0) * float(PBR_DFG_TABLE_HEIGHT - 1);
    int x0 = min(int(x), PBR_DFG_TABLE_WIDTH - 2);
    int y0 = min(int(y), PBR_DFG_TABLE_HEIGHT - 2);
    float fx = x - float(x0);
    float fy = y - float(y0);
    int i = y0 * PBR_DFG_TABLE_WIDTH + x0;
    return mix(mix(PBR_DFG_TABLE[i], PBR_DFG_TABLE[i + 1], fx),
               mix(PBR_DFG_TABLE[i + PBR_DFG_TABLE_WIDTH], PBR_DFG_TABLE[i + PBR_DFG_TABLE_WIDTH + 1], fx), fy);
}
#define PBR_DFG_LOOKUP(nov, roughness) pbrDfgBuiltin(nov, roughness)
#endif

float pbrFresnelWeight(float voh)
{
    float m = clamp(1.0 - voh, 0.0, 1.0);
    float m2 = m * m;
    return m2 * m2 * m;
}

float pbrGgx(float noh, float alpha)
{
    float a2 = alpha * alpha;
    float d = noh * noh * (a2 - 1.0) + 1.0;
    return a2 / (PBR_PI * d * d);
}

float pbrSmithVisibility(float nov, float nol, float alpha)
{
    float a2 = alpha * alpha;
    float v = nol * sqrt(nov * nov * (1.0 - a2) + a2);
    float l = nov * sqrt(nol * nol * (1.0 - a2) + a2);
    return 0.5 / (v + l);
}

vec3 pbrSpecularAlbedo(PbrLobes b, vec4 dfg, vec3 fms)
{
    return b.f0 * dfg.x + b.f90 * dfg.y + fms * (1.0 - dfg.x - dfg.y);
}

PbrLobes pbrLobes(PbrSurface s, vec3 N, vec3 V)
{
    PbrLobes b;
    b.roughness = clamp(s.roughness, PBR_MIN_ROUGHNESS, 1.0);
    b.alpha = b.roughness * b.roughness;
    b.nov = max(dot(N, V), PBR_EPSILON);
    float metallic = clamp(s.metallic, 0.0, 1.0);
    vec3 dielectricF0 = min(vec3(pbrF0FromIor(s.ior)) * s.specularColor, vec3(1.0)) * s.specularFactor;
    b.f0 = mix(dielectricF0, s.baseColor, metallic);
    b.f90 = mix(vec3(s.specularFactor), vec3(1.0), metallic);
    b.diffuse = s.baseColor * (1.0 - metallic);

    vec4 t = PBR_DFG_LOOKUP(min(b.nov, 1.0), b.roughness);
    b.eView = t.x + t.y;
    b.eAverage = t.z + t.w;
    vec3 favg = b.f0 + (b.f90 - b.f0) / 21.0;
    b.fms = favg * favg * b.eAverage / (vec3(1.0) - favg * (1.0 - b.eAverage));
    b.albedoView = pbrSpecularAlbedo(b, t, b.fms);
    b.albedoAverage = b.f0 * t.z + b.f90 * t.w + b.fms * (1.0 - b.eAverage);

    float single = pbrLuminance(b.f0 * t.x + b.f90 * t.y);
    float rest = pbrLuminance(b.fms * (1.0 - b.eView) + b.diffuse * (vec3(1.0) - b.albedoView));
    b.specularProbability = single <= 0.0 ? 0.0 : (rest <= 0.0 ? 1.0 : clamp(single / (single + rest), 0.02, 0.98));
    return b;
}

// Single scatter GGX + Kulla-Conty multiple-scatter lobe + diffuse coupled to the specular albedo
// (1 - Es(V))(1 - Es(L)) / (1 - Es_avg): reciprocal and energy conserving; (1 - F_avg) is its constant approximation.
vec3 evalBsdf(PbrLobes b, vec3 N, vec3 V, vec3 L)
{
    float nol = dot(N, L);
    if (nol <= 0.0) return vec3(0.0);
    vec3 H = normalize(V + L);
    float noh = clamp(dot(N, H), 0.0, 1.0);
    float voh = clamp(dot(V, H), 0.0, 1.0);

    vec3 f = b.f0 + (b.f90 - b.f0) * pbrFresnelWeight(voh);
    vec3 single = pbrGgx(noh, b.alpha) * pbrSmithVisibility(b.nov, nol, b.alpha) * f;

    vec4 ti = PBR_DFG_LOOKUP(min(nol, 1.0), b.roughness);
    float ei = ti.x + ti.y;
    float guardWhite = max(1.0 - b.eAverage, PBR_EPSILON);
    vec3 multi = b.fms * ((1.0 - b.eView) * (1.0 - ei) / (PBR_PI * guardWhite));

    vec3 esI = pbrSpecularAlbedo(b, ti, b.fms);
    vec3 guard = max(vec3(1.0) - b.albedoAverage, vec3(PBR_EPSILON));
    vec3 diffuse = b.diffuse * (vec3(1.0) - b.albedoView) * (vec3(1.0) - esI) / (PBR_PI * guard);
    return single + multi + diffuse;
}

float pdfBsdf(PbrLobes b, vec3 N, vec3 V, vec3 L)
{
    float nol = dot(N, L);
    if (nol <= 0.0) return 0.0;
    vec3 H = normalize(V + L);
    float noh = clamp(dot(N, H), 0.0, 1.0);
    float a2 = b.alpha * b.alpha;
    float pdfSpec = pbrGgx(noh, b.alpha) / (2.0 * (b.nov + sqrt(a2 + (1.0 - a2) * b.nov * b.nov)));
    return b.specularProbability * pdfSpec + (1.0 - b.specularProbability) * nol / PBR_PI;
}

void pbrBasis(vec3 n, out vec3 t, out vec3 bt)
{
    float s = n.z >= 0.0 ? 1.0 : -1.0;
    float a = -1.0 / (s + n.z);
    float b = n.x * n.y * a;
    t = vec3(1.0 + s * n.x * n.x * a, s * b, -s * n.x);
    bt = vec3(b, s + n.y * n.y * a, -n.y);
}

// Heitz 2018 visible-normal sampling, local frame (N = +Z).
vec3 pbrSampleVisibleNormal(vec3 v, float alpha, float u1, float u2)
{
    vec3 vh = normalize(vec3(alpha * v.x, alpha * v.y, v.z));
    float lensq = vh.x * vh.x + vh.y * vh.y;
    vec3 t1 = lensq > 0.0 ? vec3(-vh.y, vh.x, 0.0) / sqrt(lensq) : vec3(1.0, 0.0, 0.0);
    vec3 t2 = cross(vh, t1);
    float r = sqrt(u1);
    float phi = 2.0 * PBR_PI * u2;
    float a = r * cos(phi);
    float b = r * sin(phi);
    float s = 0.5 * (1.0 + vh.z);
    b = (1.0 - s) * sqrt(max(0.0, 1.0 - a * a)) + s * b;
    vec3 nh = a * t1 + b * t2 + sqrt(max(0.0, 1.0 - a * a - b * b)) * vh;
    return normalize(vec3(alpha * nh.x, alpha * nh.y, max(0.0, nh.z)));
}

// u: three uniform randoms. Returns false when the sampled direction is below the horizon.
// weight = f * cos / pdf.
bool sampleBsdf(PbrLobes b, vec3 N, vec3 V, vec3 u, out vec3 L, out float pdf, out vec3 weight)
{
    vec3 t, bt;
    pbrBasis(N, t, bt);
    vec3 vLocal = vec3(dot(V, t), dot(V, bt), dot(V, N));
    vec3 lLocal;
    if (u.x < b.specularProbability) {
        float u1 = u.x / b.specularProbability;
        vec3 h = pbrSampleVisibleNormal(vLocal, b.alpha, u1, u.y);
        lLocal = 2.0 * dot(vLocal, h) * h - vLocal;
    } else {
        float r = sqrt(u.y);
        float phi = 2.0 * PBR_PI * u.z;
        lLocal = vec3(r * cos(phi), r * sin(phi), sqrt(max(0.0, 1.0 - u.y)));
    }
    L = t * lLocal.x + bt * lLocal.y + N * lLocal.z;
    pdf = 0.0;
    weight = vec3(0.0);
    if (lLocal.z <= 0.0) return false;
    pdf = pdfBsdf(b, N, V, L);
    if (pdf <= 0.0) return false;
    weight = evalBsdf(b, N, V, L) * lLocal.z / pdf;
    return true;
}

#endif

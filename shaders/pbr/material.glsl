// glTF material table access on top of pbr/brdf.glsl; mirrors GPP::GltfMaterialGpu (std430, 304 bytes).
//
// Define before including:
//   PBR_TEXTURE_CAPACITY  size of the texture array (GPP::PbrShaderDefines)
//   PBR_MATERIAL_TEXTURES name of a `uniform sampler2D NAME[PBR_TEXTURE_CAPACITY]` declared before this include
//                         to enable the sampling helpers
//   PBR_DESCRIPTOR_INDEXING  (optional) index the array with nonuniformEXT; the shader must enable
//                         GL_EXT_nonuniform_qualifier itself
#ifndef GPP_PBR_MATERIAL_GLSL
#define GPP_PBR_MATERIAL_GLSL

#include "brdf.glsl"

const uint PBR_FLAG_DOUBLE_SIDED = 1u;
const uint PBR_ALPHA_OPAQUE = 0u;
const uint PBR_ALPHA_MASK = 1u;
const uint PBR_ALPHA_BLEND = 2u;

struct GltfTextureSlot
{
    int index;
    uint texCoord;
    vec2 offset;
    mat2 uvMatrix;
};

struct GltfMaterial
{
    vec4 baseColorFactor;
    vec3 emissiveRadiance;
    float normalScale;
    float metallic;
    float roughness;
    float occlusionStrength;
    float alphaCutoff;
    float ior;
    float specularFactor;
    uint flags;
    float pad0;
    vec3 specularColorFactor;
    float pad1;
    GltfTextureSlot baseColor;
    GltfTextureSlot metallicRoughness;
    GltfTextureSlot normal;
    GltfTextureSlot occlusion;
    GltfTextureSlot emissive;
    GltfTextureSlot specular;
    GltfTextureSlot specularColor;
};

uint pbrAlphaMode(GltfMaterial m)
{
    return (m.flags >> 1u) & 3u;
}

bool pbrDoubleSided(GltfMaterial m)
{
    return (m.flags & PBR_FLAG_DOUBLE_SIDED) != 0u;
}

vec2 pbrSlotUv(GltfTextureSlot slot, vec2 uv0, vec2 uv1)
{
    return slot.uvMatrix * (slot.texCoord == 0u ? uv0 : uv1) + slot.offset;
}

#ifdef PBR_DESCRIPTOR_INDEXING
#define PBR_TEXTURE_INDEX(i) nonuniformEXT(i)
#else
#define PBR_TEXTURE_INDEX(i) (i)
#endif

#ifdef PBR_MATERIAL_TEXTURES
// Missing textures return `fallback`.
vec4 pbrSampleSlot(GltfTextureSlot slot, vec2 uv0, vec2 uv1, vec4 fallback)
{
    if (slot.index < 0) return fallback;
    return texture(PBR_MATERIAL_TEXTURES[PBR_TEXTURE_INDEX(slot.index)], pbrSlotUv(slot, uv0, uv1));
}

vec4 pbrSampleSlotLod(GltfTextureSlot slot, vec2 uv0, vec2 uv1, float lod, vec4 fallback)
{
    if (slot.index < 0) return fallback;
    return textureLod(PBR_MATERIAL_TEXTURES[PBR_TEXTURE_INDEX(slot.index)], pbrSlotUv(slot, uv0, uv1), lod);
}
#endif

struct PbrMaterialTexels
{
    vec4 baseColor;          // linear (sRGB texture decoded by the sampler)
    vec4 metallicRoughness;  // G roughness, B metallic
    vec3 normal;             // raw texel in [0, 1]
    float occlusion;         // R
    vec3 emissive;           // linear
    float specular;          // A of KHR_materials_specular texture
    vec3 specularColor;
};

PbrMaterialTexels pbrNeutralTexels()
{
    return PbrMaterialTexels(vec4(1.0), vec4(1.0), vec3(0.5, 0.5, 1.0), 1.0, vec3(1.0), 1.0, vec3(1.0));
}

#ifdef PBR_MATERIAL_TEXTURES
PbrMaterialTexels pbrSampleMaterialTexels(GltfMaterial m, vec2 uv0, vec2 uv1)
{
    PbrMaterialTexels t;
    t.baseColor = pbrSampleSlot(m.baseColor, uv0, uv1, vec4(1.0));
    t.metallicRoughness = pbrSampleSlot(m.metallicRoughness, uv0, uv1, vec4(1.0));
    t.normal = pbrSampleSlot(m.normal, uv0, uv1, vec4(0.5, 0.5, 1.0, 1.0)).xyz;
    t.occlusion = pbrSampleSlot(m.occlusion, uv0, uv1, vec4(1.0)).x;
    t.emissive = pbrSampleSlot(m.emissive, uv0, uv1, vec4(1.0)).xyz;
    t.specular = pbrSampleSlot(m.specular, uv0, uv1, vec4(1.0)).w;
    t.specularColor = pbrSampleSlot(m.specularColor, uv0, uv1, vec4(1.0)).xyz;
    return t;
}

PbrMaterialTexels pbrSampleMaterialTexelsLod(GltfMaterial m, vec2 uv0, vec2 uv1, float lod)
{
    PbrMaterialTexels t;
    t.baseColor = pbrSampleSlotLod(m.baseColor, uv0, uv1, lod, vec4(1.0));
    t.metallicRoughness = pbrSampleSlotLod(m.metallicRoughness, uv0, uv1, lod, vec4(1.0));
    t.normal = pbrSampleSlotLod(m.normal, uv0, uv1, lod, vec4(0.5, 0.5, 1.0, 1.0)).xyz;
    t.occlusion = pbrSampleSlotLod(m.occlusion, uv0, uv1, lod, vec4(1.0)).x;
    t.emissive = pbrSampleSlotLod(m.emissive, uv0, uv1, lod, vec4(1.0)).xyz;
    t.specular = pbrSampleSlotLod(m.specular, uv0, uv1, lod, vec4(1.0)).w;
    t.specularColor = pbrSampleSlotLod(m.specularColor, uv0, uv1, lod, vec4(1.0)).xyz;
    return t;
}
#endif

// tangent.w is the bitangent sign. Returns the world-space shading normal.
vec3 pbrApplyNormalMap(vec3 N, vec4 tangent, vec3 texel, float scale)
{
    vec3 T = normalize(tangent.xyz - N * dot(N, tangent.xyz));
    vec3 B = cross(N, T) * tangent.w;
    vec3 n = (texel * 2.0 - 1.0) * vec3(scale, scale, 1.0);
    return normalize(T * n.x + B * n.y + N * n.z);
}

struct PbrMaterialSample
{
    PbrSurface surface;
    vec3 normal;
    float occlusion;
    float alpha;
    vec3 emission;
};

// N: interpolated geometric-side normal (already flipped for back faces of double-sided materials).
// tangent: xyz in the same space as N. Occlusion applies to indirect light only.
PbrMaterialSample pbrEvaluateMaterial(GltfMaterial m, PbrMaterialTexels t, vec3 N, vec4 tangent)
{
    PbrMaterialSample s;
    vec4 base = m.baseColorFactor * t.baseColor;
    s.surface.baseColor = base.rgb;
    s.alpha = base.a;
    s.surface.metallic = clamp(m.metallic * t.metallicRoughness.z, 0.0, 1.0);
    s.surface.roughness = clamp(m.roughness * t.metallicRoughness.y, 0.0, 1.0);
    s.surface.ior = m.ior;
    s.surface.specularFactor = m.specularFactor * t.specular;
    s.surface.specularColor = m.specularColorFactor * t.specularColor;
    s.normal = m.normal.index >= 0 ? pbrApplyNormalMap(N, tangent, t.normal, m.normalScale) : N;
    s.occlusion = 1.0 + m.occlusionStrength * (t.occlusion - 1.0);
    s.emission = m.emissiveRadiance * t.emissive;
    return s;
}

#endif

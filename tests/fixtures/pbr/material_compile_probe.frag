#version 450
#extension GL_EXT_nonuniform_qualifier : require

#define PBR_TEXTURE_CAPACITY 64
#define PBR_DESCRIPTOR_INDEXING 1
layout(set = 0, binding = 1) uniform sampler2D u_pbrTextures[PBR_TEXTURE_CAPACITY];
#define PBR_MATERIAL_TEXTURES u_pbrTextures
#include "pbr/material.glsl"

layout(std430, set = 0, binding = 0) readonly buffer Materials { GltfMaterial materials[]; };
layout(push_constant) uniform Draw { uint materialIndex; };
layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec4 inTangent;
layout(location = 2) in vec2 inUv0;
layout(location = 3) in vec2 inUv1;
layout(location = 4) in vec3 inView;
layout(location = 0) out vec4 outColor;

void main()
{
    GltfMaterial m = materials[materialIndex];
    PbrMaterialTexels t = pbrSampleMaterialTexels(m, inUv0, inUv1);
    vec3 N = normalize(inNormal);
    if (pbrDoubleSided(m) && !gl_FrontFacing) N = -N;
    PbrMaterialSample s = pbrEvaluateMaterial(m, t, N, inTangent);
    if (pbrAlphaMode(m) == PBR_ALPHA_MASK && s.alpha < m.alphaCutoff) discard;
    vec3 V = normalize(inView);
    PbrLobes lobes = pbrLobes(s.surface, s.normal, V);
    vec3 L = normalize(vec3(1.0, 1.0, 1.0));
    vec3 color = evalBsdf(lobes, s.normal, V, L) * max(dot(s.normal, L), 0.0) * 10.0 * s.occlusion + s.emission;
    outColor = vec4(color, s.alpha);
}

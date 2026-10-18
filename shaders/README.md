# Shader source sharing

```glsl
#version 450
#include "common.glsl"
```

The framework resolves quoted includes in this order:

1. Relative to the file containing the include.
2. Relative to each configured `GPP:Graphics:Render:ShaderAssetDirectories` entry.

## Shared PBR library

`pbr/brdf.glsl` (GGX, height-correlated Smith, Fresnel from IOR, multiple scattering, `evalBsdf` / `sampleBsdf` / `pdfBsdf`)
and `pbr/material.glsl` (glTF material table, `GltfMaterialGpu` mirror, texture slots, normal mapping) are included as
`#include "pbr/material.glsl"` with the gpp `shaders` directory in `ShaderAssetDirectories`. The CPU reference is
`GPP::Pbr` (`BuildDfgTable`, `Eval`, `SampleBsdf`), the DFG table for `PBR_DFG_LOOKUP` comes from `GPP::Pbr::BuildDfgTable`.

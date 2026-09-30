# Shader source sharing

```glsl
#version 450
#include "common.glsl"
```

The framework resolves quoted includes in this order:

1. Relative to the file containing the include.
2. Relative to each configured `GPP:Graphics:Render:ShaderAssetDirectories` entry.

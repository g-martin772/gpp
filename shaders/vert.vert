#version 450

#include "common.glsl"

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 0) out vec3 fragColor;

layout(push_constant) uniform PushConstants
{
    FrameData data;
} pc;

void main()
{
    gl_Position = pc.data.viewProjection * pc.data.model * vec4(inPosition, 1.0);
    float pulse = ShaderPulse(pc.data.time);
    fragColor = inColor * pulse;
}

#version 450

#include "common.glsl"

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 0) out vec3 fragColor;

struct PushConstants
{
    mat4 viewProjection;
    mat4 model;
    float time;
};

layout(push_constant) uniform PushConstants pc;

void main()
{
    gl_Position = pc.viewProjection * pc.model * vec4(inPosition, 1.0);
    float pulse = ShaderPulse(pc.time);
    fragColor = inColor * pulse;
}

#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 0) out vec3 fragColor;

layout(push_constant) uniform PushConstants
{
    mat4 viewProjection;
    mat4 model;
    float time;
} pc;

void main()
{
    gl_Position = pc.viewProjection * pc.model * vec4(inPosition, 1.0);
    float pulse = 0.85 + 0.15 * sin(pc.time);
    fragColor = inColor * pulse;
}

struct FrameData
{
    mat4 viewProjection;
    mat4 model;
    float time;
};

float ShaderPulse(float time)
{
    return 0.85 + 0.15 * sin(time);
}

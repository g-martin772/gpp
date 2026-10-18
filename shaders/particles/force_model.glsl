// Selects the acceleration model: PF_FORCE_KIND 1 softened Newtonian point mass, 2 Paczynski-Wiita, anything else
// includes particle_custom_force.glsl from the include directories of the host application.
//
// A custom file defines
//   vec3  pfAcceleration(vec3 x, vec3 v, ForceParams f)
//   float pfStableStep(vec3 x, vec3 v, float eta, ForceParams f)
//   float pfPotential(vec3 x, ForceParams f)
// and may define PF_HAS_STEP_HOOK (bool pfStep(inout Particle p, float dt, ForceParams f), true replaces the
// kick-drift-kick of that substep), PF_HAS_POST_MACRO (void pfPostMacro(inout Particle p, float baseStep, ForceParams f))
// and PF_HAS_ABSORB_HOOK (void pfAbsorbQuantities(Particle p, vec3 sinkCentre, ForceParams f, out float q[8])).

#if PF_FORCE_KIND == 1

vec3 pfAcceleration(vec3 x, vec3 v, ForceParams f)
{
    float r2 = dot(x, x) + f.softening * f.softening;
    float inverse = 1.0 / sqrt(r2);
    return -f.mu * x * (inverse * inverse * inverse);
}

float pfStableStep(vec3 x, vec3 v, float eta, ForceParams f)
{
    float speed = length(v) + 1e-30;
    float r = sqrt(dot(x, x) + f.softening * f.softening);
    return eta * 6.28318530717958647692 * min(sqrt(r * r * r / f.mu), r / speed);
}

float pfPotential(vec3 x, ForceParams f)
{
    return -f.mu / sqrt(dot(x, x) + f.softening * f.softening);
}

#elif PF_FORCE_KIND == 2

float pfPwDistance(float r, float rs)
{
    return max(r - rs, 0.001 * rs);
}

vec3 pfAcceleration(vec3 x, vec3 v, ForceParams f)
{
    float r = max(length(x), 1e-30);
    float d = pfPwDistance(r, f.rs);
    return -f.mu * x / (r * d * d);
}

float pfStableStep(vec3 x, vec3 v, float eta, ForceParams f)
{
    float speed = length(v) + 1e-30;
    float r = max(length(x), 1e-30);
    float d = pfPwDistance(r, f.rs);
    return eta * 6.28318530717958647692 * min(sqrt(r * d * d / f.mu), d / speed);
}

float pfPotential(vec3 x, ForceParams f)
{
    return -f.mu / pfPwDistance(length(x), f.rs);
}

#else

#include "particle_custom_force.glsl"

#endif

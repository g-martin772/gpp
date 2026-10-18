// Shared by every particle kernel. Mirrors simulation/particles/particle_model.cppm; PF_MAX_BIN and PF_FORCE_KIND
// come in as compile defines. Descriptor set 0 is the same for all kernels, each pipeline uses the subset it touches.

#define PF_BINS (PF_MAX_BIN + 2)
#define PF_DEAD (PF_MAX_BIN + 1)

struct ForceParams
{
    float mu;
    float softening;
    float rs;
    uint kind;
    float user[12];
};

struct EmitterParams
{
    vec4 positionShape;
    vec4 axisRadius;
    vec4 params0;
    vec4 params1;
    vec4 bulk;
    vec4 target;
    uvec4 ids;
    uvec4 batch;
};

struct SinkParams
{
    vec4 centerRadius;
    uvec4 info;
};

struct ColliderParams
{
    vec4 centerRadius;
    vec4 halfExtentsShape;
    vec4 axis0;
    vec4 axis1;
    vec4 axis2;
    vec4 velocityRestitution;
    vec4 dragForce;
    uvec4 info;
};

struct RegionParams
{
    vec4 centerRadius;
};

struct MacroParams
{
    float baseStep;
    float eta;
    float invScale;
    uint emitterCount;
    uint sinkCount;
    uint colliderCount;
    uint regionCount;
    uint newTotal;
    uint capacity;
    uint flags;
    uint pad0;
    uint pad1;
    ForceParams force;
    EmitterParams emitters[64];
    SinkParams sinks[16];
    ColliderParams colliders[16];
    RegionParams regions[8];
};

struct Particle
{
    vec3 pos;
    vec3 vel;
    float weight;
    float temperature;
    float age;
    uint species;
    uint owner;
    uint serial;
};

layout(std430, set = 0, binding = 0) buffer CurPos { vec4 curPos[]; };
layout(std430, set = 0, binding = 1) buffer CurVel { vec4 curVel[]; };
layout(std430, set = 0, binding = 2) buffer CurMeta { uvec4 curMeta[]; };
layout(std430, set = 0, binding = 3) buffer AltPos { vec4 altPos[]; };
layout(std430, set = 0, binding = 4) buffer AltVel { vec4 altVel[]; };
layout(std430, set = 0, binding = 5) buffer AltMeta { uvec4 altMeta[]; };
layout(std430, set = 0, binding = 6) buffer State { uint st[]; };
layout(std430, set = 0, binding = 7) readonly buffer Ring { MacroParams ring[]; };
layout(std430, set = 0, binding = 8) buffer Accum { uint acc[]; };
layout(std430, set = 0, binding = 9) buffer Args { uvec4 args[]; };
layout(std430, set = 0, binding = 10) buffer Hist { uint hist[]; };

layout(push_constant) uniform Push
{
    uint paramsIndex;
    uint arg0;
    uint arg1;
    uint arg2;
} pc;

#define MP ring[pc.paramsIndex]

const uint ST_USED = 0u;
const uint ST_PENDING = 1u;
const uint ST_DROPPED = 2u;
const uint ST_BINSTART = 8u;
const uint ST_BINCOUNT = 24u;
const uint ST_SERIAL = 40u;

const uint ACC_SINK = 0u;        // 24 words per slot: 8 fixed-point pairs, count, killed
const uint ACC_COLLIDER = 384u;  // 24 words per slot: 8 pairs, count
const uint ACC_DRAG = 768u;      // 2 words per collider: mass inside the shape
const uint ACC_STATS = 800u;     // 54 pairs: global (6) then 6 per region
const uint ACC_OUTSIDE = 920u;   // deposit: mass whose footprint left the grid (one pair)
const uint ACC_WORDS = 1024u;

// Philox4x32-10, identical to ParticleRng::Philox4x32
uvec4 pfPhilox(uvec4 c, uvec2 k)
{
    for (int round = 0; round < 10; ++round)
    {
        uint hi0, lo0, hi1, lo1;
        umulExtended(0xD2511F53u, c.x, hi0, lo0);
        umulExtended(0xCD9E8D57u, c.z, hi1, lo1);
        c = uvec4(hi1 ^ c.y ^ k.x, lo1, hi0 ^ c.w ^ k.y, lo0);
        k += uvec2(0x9E3779B9u, 0xBB67AE85u);
    }
    return c;
}

float pfUnit(uint bits)
{
    return float(bits >> 8) * (1.0 / 16777216.0);
}

vec2 pfNormal(float u1, float u2)
{
    float radius = sqrt(-2.0 * log(1.0 - u1));
    float angle = 6.28318530717958647692 * u2;
    return radius * vec2(cos(angle), sin(angle));
}

uint pfAssignBin(float baseStep, float stable, uint maxBin)
{
    if (!(stable > 0.0)) return maxBin;
    float ratio = baseStep / stable;
    if (!(ratio > 1.0)) return 0u;
    uint bits = floatBitsToUint(ratio);
    int exponent = int((bits >> 23) & 255u) - 127;
    if ((bits & 0x7FFFFFu) != 0u) exponent++;
    return uint(clamp(exponent, 0, int(maxBin)));
}

// 64-bit two's complement counters built from two 32-bit words (low, high): order independent, so deterministic.
void pfFixedAdd(uint base, float value, float inverseScale)
{
    float scaled = roundEven(value * inverseScale);
    if (isnan(scaled) || isinf(scaled)) return;
    float magnitude = abs(scaled);
    float high = floor(magnitude * (1.0 / 4294967296.0));
    float low = magnitude - high * 4294967296.0;
    uint highBits = uint(high);
    uint lowBits = uint(low);
    if (scaled < 0.0)
    {
        lowBits = ~lowBits + 1u;
        highBits = ~highBits + (lowBits == 0u ? 1u : 0u);
    }
    uint before = atomicAdd(acc[base], lowBits);
    uint carry = (before + lowBits < before) ? 1u : 0u;
    atomicAdd(acc[base + 1u], highBits + carry);
}

float pfFixedToFloat(uint base, float scale)
{
    uint low = acc[base];
    int high = int(acc[base + 1u]);
    return (float(high) * 4294967296.0 + float(low)) * scale;
}

void pfCompensatedAdd(inout vec3 sum, inout vec3 comp, vec3 increment)
{
    precise vec3 total = sum + increment;
    precise vec3 error = mix((increment - total) + sum, (sum - total) + increment, greaterThanEqual(abs(sum), abs(increment)));
    comp += error;
    sum = total;
}

bool pfInside(ColliderParams collider, vec3 position, float elapsed, out vec3 normal, out vec3 surface)
{
    vec3 centre = collider.centerRadius.xyz + collider.velocityRestitution.xyz * elapsed;
    vec3 offset = position - centre;
    normal = vec3(0.0, 1.0, 0.0);
    surface = centre;
    if (collider.halfExtentsShape.w < 0.5)
    {
        float radius = collider.centerRadius.w;
        float distance = length(offset);
        if (!(distance < radius)) return false;
        normal = distance > 0.0 ? offset / distance : vec3(0.0, 1.0, 0.0);
        surface = centre + normal * radius;
        return true;
    }
    vec3 half_ = collider.halfExtentsShape.xyz;
    vec3 axes[3] = vec3[3](collider.axis0.xyz, collider.axis1.xyz, collider.axis2.xyz);
    vec3 local = vec3(dot(axes[0], offset), dot(axes[1], offset), dot(axes[2], offset));
    if (!(abs(local.x) < half_.x && abs(local.y) < half_.y && abs(local.z) < half_.z)) return false;
    vec3 penetration = half_ - abs(local);
    int face = 0;
    if (penetration.y < penetration[face]) face = 1;
    if (penetration.z < penetration[face]) face = 2;
    float side = local[face] < 0.0 ? -1.0 : 1.0;
    normal = axes[face] * side;
    local[face] = side * half_[face];
    surface = centre + axes[0] * local.x + axes[1] * local.y + axes[2] * local.z;
    return true;
}

Particle pfLoad(uint index)
{
    vec4 pm = curPos[index];
    vec4 vt = curVel[index];
    uvec4 meta = curMeta[index];
    Particle p;
    p.pos = pm.xyz;
    p.vel = vt.xyz;
    p.weight = pm.w;
    p.temperature = vt.w;
    p.age = uintBitsToFloat(meta.z);
    p.species = meta.x & 0xFFFFu;
    p.owner = meta.x >> 16;
    p.serial = meta.w;
    return p;
}

void pfStore(uint index, Particle p, bool alive)
{
    curPos[index] = vec4(p.pos, p.weight);
    curVel[index] = vec4(p.vel, p.temperature);
    uvec4 meta = curMeta[index];
    meta.z = floatBitsToUint(p.age);
    if (!alive) meta.y &= ~1u;
    curMeta[index] = meta;
}

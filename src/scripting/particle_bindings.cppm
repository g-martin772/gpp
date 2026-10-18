export module GPP.Scripting:ParticleBindings;

import std;
import GPP.Core;
import GPP.Simulation;
import :Luau;

export namespace GPP
{
    // Installs the `particles` table: emitter{...}, burst, set_rate, set_origin, pause, resume, destroy, count, region_stats.
    // Emitters made here live in the hub, not in the scene, so they vanish when the hub is reset on Stop. Call before
    // LuauVm::Seal().
    void RegisterParticleBindings(LuauVm& vm, const std::shared_ptr<ParticleHub>& hub);

    // Applies one `particles.emitter{ key = value }` entry; false for an unknown key or a value of the wrong kind.
    bool ApplyParticleEmitterKey(ParticleEmitterDef& def, std::string_view key, const LuauValue& value);
}

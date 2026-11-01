export module GPP.Scripting:PhysicsBindings;

import std;
import GPP.Core;
import GPP.Simulation;
import :Luau;
import :Bindings;

export namespace GPP
{
    void RegisterPhysicsBindings(LuauVm& vm, const std::shared_ptr<SceneBindings>& scene,
                                 std::function<PhysicsSimulationModule*()> physics);
}

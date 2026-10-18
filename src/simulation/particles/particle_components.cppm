module;
#include <glm/glm.hpp>
#include <yaml-cpp/yaml.h>
export module GPP.Simulation:Particles.Components;

import std;
import GPP.Core;
import :Reflection;
import :ComponentRegistry;
import :Particles.Types;

export namespace GPP
{
    struct ParticleEmitterComponent
    {
        bool Active{true};
        ParticleEmitterShape Shape{ParticleEmitterShape::Point};
        ParticleRateMode RateMode{ParticleRateMode::ParticleRate};
        double Rate{1000.0};       // particles/s
        double ParticleMass{1.0};  // kg per particle when RateMode is ParticleRate
        double MassRate{1.0};      // kg/s; the per-particle weight is MassRate / Rate
        int Burst{0};              // particles released once when the emitter first runs
        std::uint64_t Seed{1};
        double Radius{1.0};        // m
        double InnerRadius{0.0};   // m
        double Thickness{0.0};     // m
        float HalfAngle{0.3f};     // rad
        ParticleVelocityMode VelocityMode{ParticleVelocityMode::Axial};
        double Speed{0.0};         // m/s
        glm::dvec3 BulkVelocity{0.0, 0.0, 0.0}; // m/s, added to the emitter frame velocity
        double Dispersion{0.0};    // m/s, per-axis Gaussian sigma
        bool ThermalVelocity{false};
        double MeanMolecularWeight{0.6};
        double Temperature{1.0e4}; // K
        std::uint64_t AttachedTo{0};
        std::uint64_t TargetEntity{0};
        glm::dvec3 TargetPosition{0.0, 0.0, 1.0}; // m, world frame
    };

    struct ParticleSinkComponent
    {
        bool Active{true};
        double Radius{1.0}; // m
        ParticleSinkMode Mode{ParticleSinkMode::Absorb};
        bool Accounting{true};
    };

    struct ParticleColliderComponent
    {
        bool Active{true};
        ParticleColliderResponse Response{ParticleColliderResponse::Collide};
        ParticleColliderShape Shape{ParticleColliderShape::Sphere};
        double Radius{0.5};                          // m
        glm::dvec3 HalfExtents{0.5, 0.5, 0.5};       // m, in the body frame
        float Restitution{0.5f};
        float DragCoefficient{1.0f};
        double ReferenceArea{0.0};                   // m^2, 0 projects the shape along the relative velocity
    };

    void RegisterParticleComponents()
    {
        static std::once_flag flag;
        std::call_once(flag, []
        {
            RegisterComponent<ParticleEmitterComponent>("ParticleEmitter", ComponentDescription<ParticleEmitterComponent>{
                .DisplayName = "Particle Emitter",
                .Fields = {
                    Field("Active", &ParticleEmitterComponent::Active),
                    Field("Shape", &ParticleEmitterComponent::Shape,
                          {.Options = {"Point", "Sphere", "Shell", "Disk", "Ring", "Cone", "Stream"}}),
                    Field("RateMode", &ParticleEmitterComponent::RateMode,
                          {.Label = "Rate Mode", .Options = {"Particles", "Mass"}}),
                    Field("Rate", &ParticleEmitterComponent::Rate,
                          {.Label = "Rate (1/s)", .Min = 0.0, .Speed = 10.0}),
                    Field("ParticleMass", &ParticleEmitterComponent::ParticleMass,
                          {.Label = "Particle Mass", .Min = 0.0, .Speed = 1.0, .VisibleField = "RateMode",
                           .VisibleMask = 0b01, .Dim = Units::Dimension::Mass}),
                    Field("MassRate", &ParticleEmitterComponent::MassRate,
                          {.Label = "Mass Rate (kg/s)", .Min = 0.0, .Speed = 1.0, .VisibleField = "RateMode",
                           .VisibleMask = 0b10}),
                    Field("Burst", &ParticleEmitterComponent::Burst, {.Min = 0.0, .Speed = 10.0}),
                    Field("Seed", &ParticleEmitterComponent::Seed),
                    Field("Radius", &ParticleEmitterComponent::Radius,
                          {.Min = 0.0, .Speed = 0.1, .VisibleField = "Shape", .VisibleMask = 0b1011110,
                           .Dim = Units::Dimension::Length}),
                    Field("InnerRadius", &ParticleEmitterComponent::InnerRadius,
                          {.Label = "Inner Radius", .Min = 0.0, .Speed = 0.1, .VisibleField = "Shape",
                           .VisibleMask = 0b1100, .Dim = Units::Dimension::Length}),
                    Field("Thickness", &ParticleEmitterComponent::Thickness,
                          {.Min = 0.0, .Speed = 0.1, .VisibleField = "Shape", .VisibleMask = 0b11000,
                           .Dim = Units::Dimension::Length}),
                    Field("HalfAngle", &ParticleEmitterComponent::HalfAngle,
                          {.Label = "Half Angle (rad)", .Min = 0.0, .Max = 3.14159f, .Speed = 0.01f,
                           .VisibleField = "Shape", .VisibleMask = 0b100000}),
                    Field("VelocityMode", &ParticleEmitterComponent::VelocityMode,
                          {.Label = "Velocity Mode", .Options = {"Axial", "Radial", "Azimuthal"}}),
                    Field("Speed", &ParticleEmitterComponent::Speed,
                          {.Speed = 10.0, .Dim = Units::Dimension::Velocity}),
                    Field("BulkVelocity", &ParticleEmitterComponent::BulkVelocity,
                          {.Label = "Bulk Velocity", .Speed = 10.0, .Dim = Units::Dimension::Velocity}),
                    Field("Dispersion", &ParticleEmitterComponent::Dispersion,
                          {.Min = 0.0, .Speed = 1.0, .Dim = Units::Dimension::Velocity}),
                    Field("ThermalVelocity", &ParticleEmitterComponent::ThermalVelocity, {.Label = "Thermal Velocity"}),
                    Field("MeanMolecularWeight", &ParticleEmitterComponent::MeanMolecularWeight,
                          {.Label = "Mean Molecular Weight", .Min = 0.01, .Speed = 0.01, .VisibleField = "ThermalVelocity",
                           .VisibleMask = 0b1}),
                    Field("Temperature", &ParticleEmitterComponent::Temperature,
                          {.Min = 0.0, .Speed = 100.0, .Dim = Units::Dimension::Temperature}),
                    Field("AttachedTo", &ParticleEmitterComponent::AttachedTo,
                          {.Label = "Attached To", .Kind = FieldKind::EntityRef}),
                    Field("TargetEntity", &ParticleEmitterComponent::TargetEntity,
                          {.Label = "Target Entity", .Kind = FieldKind::EntityRef, .VisibleField = "Shape",
                           .VisibleMask = 0b1000000}),
                    Field("TargetPosition", &ParticleEmitterComponent::TargetPosition,
                          {.Label = "Target Position", .Speed = 1.0, .VisibleField = "Shape", .VisibleMask = 0b1000000,
                           .Dim = Units::Dimension::Length}),
                }});

            RegisterComponent<ParticleSinkComponent>("ParticleSink", ComponentDescription<ParticleSinkComponent>{
                .DisplayName = "Particle Sink",
                .Fields = {
                    Field("Active", &ParticleSinkComponent::Active),
                    Field("Radius", &ParticleSinkComponent::Radius,
                          {.Min = 0.0, .Speed = 0.1, .Dim = Units::Dimension::Length}),
                    Field("Mode", &ParticleSinkComponent::Mode, {.Options = {"Absorb", "Kill"}}),
                    Field("Accounting", &ParticleSinkComponent::Accounting),
                }});

            RegisterComponent<ParticleColliderComponent>("ParticleCollider", ComponentDescription<ParticleColliderComponent>{
                .DisplayName = "Particle Collider",
                .Fields = {
                    Field("Active", &ParticleColliderComponent::Active),
                    Field("Response", &ParticleColliderComponent::Response,
                          {.Options = {"Ignore", "Collide", "Drag", "Absorb"}}),
                    Field("Shape", &ParticleColliderComponent::Shape, {.Options = {"Sphere", "Box"}}),
                    Field("Radius", &ParticleColliderComponent::Radius,
                          {.Min = 1e-9, .Speed = 0.1, .VisibleField = "Shape", .VisibleMask = 0b01,
                           .Dim = Units::Dimension::Length}),
                    Field("HalfExtents", &ParticleColliderComponent::HalfExtents,
                          {.Label = "Half Extents", .Min = 1e-9, .Speed = 0.1, .VisibleField = "Shape",
                           .VisibleMask = 0b10, .Dim = Units::Dimension::Length}),
                    Field("Restitution", &ParticleColliderComponent::Restitution,
                          {.Min = 0.0f, .Max = 1.0f, .Speed = 0.01f, .VisibleField = "Response", .VisibleMask = 0b0010}),
                    Field("DragCoefficient", &ParticleColliderComponent::DragCoefficient,
                          {.Label = "Drag Coefficient", .Min = 0.0f, .Speed = 0.01f, .VisibleField = "Response",
                           .VisibleMask = 0b0100}),
                    Field("ReferenceArea", &ParticleColliderComponent::ReferenceArea,
                          {.Label = "Reference Area (m^2)", .Min = 0.0, .Speed = 0.1, .VisibleField = "Response",
                           .VisibleMask = 0b0100}),
                }});
        });
    }
}

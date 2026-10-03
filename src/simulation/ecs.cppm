module;
#include <entt/entt.hpp>
export module GPP.Simulation:Ecs;

export namespace entt
{
    using entt::entity;
    using entt::null_t;
    using entt::null;
    using entt::tombstone_t;
    using entt::tombstone;
    using entt::id_type;

    using entt::basic_registry;
    using entt::registry;

    using entt::basic_view;
    using entt::view;
    using entt::basic_group;
    using entt::group;

    using entt::dispatcher;
    using entt::sink;
    using entt::sigh;
}

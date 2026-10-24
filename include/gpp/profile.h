#pragma once

// Scoped CPU profiler zones. `import GPP;` in the including file. The name must be a string literal or Profiler::Intern.
//   GPP_ZONE("Sim.Tick");

#define GPP_ZONE_CAT_(a, b) a##b
#define GPP_ZONE_CAT(a, b) GPP_ZONE_CAT_(a, b)

#if defined(GPP_TRACY)
#include <tracy/Tracy.hpp>
#define GPP_ZONE(name) \
    ZoneScopedN(name); \
    ::GPP::ProfileZone GPP_ZONE_CAT(gppZone_, __LINE__)(name)
#else
#define GPP_ZONE(name) ::GPP::ProfileZone GPP_ZONE_CAT(gppZone_, __LINE__)(name)
#endif

#pragma once
// Tracy zones, compiled out unless configured with -DPB_TRACY=ON (the profile preset).
#ifdef PB_TRACY
#include <tracy/Tracy.hpp>
#define PROFILE_ZONE(name) ZoneScopedN(name)
#else
#define PROFILE_ZONE(name)
#endif

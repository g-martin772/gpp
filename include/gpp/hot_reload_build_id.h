#pragma once

#if __has_include(<gpp_hot_reload_generated.h>)
#include <gpp_hot_reload_generated.h>
#endif

#ifndef GPP_HOT_RELOAD_SOURCE_HASH
#define GPP_HOT_RELOAD_SOURCE_HASH "unknown"
#endif

// compiler, standard library, build type and interface source hash (see cmake/GppHotReload.cmake)
#define GPP_HOT_RELOAD_BUILD_ID GPP_HOT_RELOAD_SOURCE_HASH

// The loader scans the plugin file for this marker before dlopen, so a stale plugin never runs static initializers.
#define GPP_HOT_RELOAD_BUILD_ID_MARKER "GPP_BUILD_ID="

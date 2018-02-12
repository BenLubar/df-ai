#pragma once

#include "Core.h"
#include <Console.h>
#include <Export.h>
#include <PluginManager.h>

#include "DataDefs.h"

#include <map>
#include <set>
#include <tuple>

#ifndef HAVE_NULLPTR
#define nullptr 0L
#endif

using namespace DFHack;
using namespace df::enums;

DFhackDataExport extern std::vector<std::string> *plugin_globals;

inline constexpr bool is_release(const char *actual = CMAKE_INTDIR, const char *want = "Release")
{
    return *actual == *want && *actual ? is_release(actual + 1, want + 1) : true;
}

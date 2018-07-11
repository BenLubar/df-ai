#pragma once

#include "Core.h"
#include <Console.h>
#include <Export.h>
#include <PluginManager.h>

#include "DataDefs.h"

#include <map>
#include <set>
#include <tuple>

#ifdef nullptr
#undef nullptr
#endif

using namespace DFHack;
using namespace df::enums;

DFhackDataExport extern std::vector<std::string> *plugin_globals;

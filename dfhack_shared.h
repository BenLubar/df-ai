#pragma once

#include "Core.h"
#include <Console.h>
#include <Export.h>
#include <PluginManager.h>

#include "DataDefs.h"

#include <map>
#include <memory>
#include <set>
#include <tuple>

#ifdef nullptr
#undef nullptr
#endif

#if defined(__GNUC__) && __GNUC__ == 4 && __GNUC_MINOR__ == 8
// GCC 4.8 does not support std::make_unique for some bizzare reason.
namespace std
{
    // Doesn't support array types correctly, but it's good enough for df-ai's purposes.
    template<typename T, typename... Args>
    std::unique_ptr<T> make_unique(Args&&... args)
    {
        return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
    }
}
#endif

using namespace DFHack;
using namespace df::enums;

DFhackDataExport extern std::vector<std::string> *plugin_globals;

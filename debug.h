#pragma once

#include "dfhack_shared.h"

#include <boost/config.hpp>

#ifdef DFAI_RELEASE
// Don't breakpoint in release builds to avoid bug reports *about* the assert system.
#define DFAI_BREAKPOINT()
#else
// Force the debugger to breakpoint. This will probably crash if there's no debugger attached.
#if WIN32
#define DFAI_BREAKPOINT() __debugbreak()
#else
#define DFAI_BREAKPOINT() __asm__ volatile("int $0x03")
#endif
#endif

extern BOOST_NOINLINE std::ostream & dfai_debug_log();

// Get base filename (without directory) as a constexpr so it gets run at compile time. Arguments should be __FILE__, __FILE__.
static inline constexpr const char *dfai_debug_basename(const char *lastSlash, const char *lastChar) noexcept
{
    return (!*lastChar) ? lastSlash :
        ((*lastChar == '/' || *lastChar == '\\') ?
            dfai_debug_basename(lastChar + 1, lastChar + 1) :
            dfai_debug_basename(lastSlash, lastChar + 1));
}

#define DFAI_ASSERT(ok, message) \
    if (BOOST_UNLIKELY(!(ok))) \
    { \
        dfai_debug_log() << "Assertion failed on " << dfai_debug_basename(__FILE__, __FILE__) << " line " << __LINE__ << ": " << BOOST_STRINGIZE(ok) << std::endl; \
        dfai_debug_log() << message << std::endl; \
        dfai_debug_log() << std::endl; \
        DFAI_BREAKPOINT(); \
    }

namespace DFHack
{
    namespace Maps
    {
        extern DFHACK_EXPORT bool isValidTilePos(int32_t x, int32_t y, int32_t z);
    }
}

#define DFAI_ASSERT_VALID_TILE(t, message) \
    DFAI_ASSERT(DFHack::Maps::isValidTilePos((t).x, (t).y, (t).z), "invalid tile at (" << (t).x << ", " << (t).y << ", " << (t).z << ")" << message)

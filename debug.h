#pragma once

#include "dfhack_shared.h"

#include <boost/config.hpp>

#ifdef DFAI_RELEASE
#define DFAI_IS_RELEASE true
#else
#define DFAI_IS_RELEASE false
#endif

#define DFAI_DEBUG_CATEGORIES \
    DFAI_DEBUG_CATEGORY(blueprint) \
    DFAI_DEBUG_CATEGORY(camera) \
    DFAI_DEBUG_CATEGORY(lockstep) \
    DFAI_DEBUG_CATEGORY(tick) \
    DFAI_DEBUG_CATEGORY(dfplex) \
    /* end of list (so last line can have a backslash) */

struct DebugCategoryConfig
{
#define DFAI_DEBUG_CATEGORY(x) int x;
    DFAI_DEBUG_CATEGORIES
#undef DFAI_DEBUG_CATEGORY
};
extern DebugCategoryConfig debug_category_config;

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

#define DFAI_ASSERT_LOC(ok, message, filename, lineno) \
    do \
    { \
        if (BOOST_UNLIKELY(!(ok))) \
        { \
            dfai_debug_log() << "Assertion failed on " << dfai_debug_basename(filename, filename) << " line " << lineno << ": " << BOOST_STRINGIZE(ok) << std::endl; \
            dfai_debug_log() << message << std::endl; \
            dfai_debug_log() << std::endl; \
            DFAI_BREAKPOINT(); \
        } \
    } while (false)

#define DFAI_ASSERT(ok, message) \
    DFAI_ASSERT_LOC(ok, message, __FILE__, __LINE__)

#ifdef DFAI_RELEASE
// only debug levels 0-3 are available in release builds
#define DFAI_DEBUG(category, level, message) \
    do \
    { \
        static_assert(level > 0, "debug log level must be positive"); \
        static_assert(level < 666, "debug log level too high"); \
        if (BOOST_UNLIKELY(level < 4 && debug_category_config.category >= level)) \
        { \
            /* in release builds, write to debug file */ \
            dfai_debug_log() << "[DEBUG:" #category ":" #level << "] " << dfai_debug_basename(__FILE__, __FILE__) << " line " << __LINE__ << ": " << message << std::endl; \
        } \
    } while (false)
#else
#define DFAI_DEBUG(category, level, message) \
    do \
    { \
        static_assert(level > 0, "debug log level must be positive"); \
        static_assert(level < 666, "debug log level too high"); \
        if (BOOST_UNLIKELY(debug_category_config.category >= level)) \
        { \
            /* in debug builds, write to console */ \
            Core::getInstance().getConsole() << "[DEBUG:" #category ":" #level << "] " << dfai_debug_basename(__FILE__, __FILE__) << " line " << __LINE__ << ": " << message << std::endl; \
        } \
        if (BOOST_UNLIKELY(debug_category_config.category == 666)) \
        { \
            DFAI_BREAKPOINT(); \
        } \
    } while (false)
#endif

namespace DFHack
{
    namespace Maps
    {
        extern DFHACK_EXPORT bool isValidTilePos(int32_t x, int32_t y, int32_t z);
    }
}

#define DFAI_ASSERT_VALID_TILE(t, message) \
    DFAI_ASSERT(DFHack::Maps::isValidTilePos((t).x, (t).y, (t).z), "invalid tile at (" << (t).x << ", " << (t).y << ", " << (t).z << ")" << message)

#pragma once

#include "plan.h"

#include <set>
#include <string>

struct horrible_t
{
    horrible_t() {}

#define THE_HORROR(type, name) \
    type name; \
    horrible_t(type const & name) : name(name) {} \
    horrible_t & operator=(type const & name) { this->name = name; return *this; } \
    bool operator==(const type & name) const { return this->name == name; } \
    bool operator!=(const type & name) const { return this->name != name; } \
    operator type() { return this->name; }

    THE_HORROR(std::string, str);
    THE_HORROR(room *, r);
    THE_HORROR(furniture *, f);
    THE_HORROR(int32_t, id);
    THE_HORROR(std::set<int32_t>, ids);

#undef THE_HORROR
};

// vim: et:sw=4:ts=4

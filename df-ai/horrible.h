#pragma once

#include "plan.h"

#include <functional>
#include <set>
#include <string>

#include "df/coord.h"

namespace DFHack
{
    struct color_ostream;
}

namespace df
{
    struct building;
}

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
    THE_HORROR(df::coord, c);

    std::function<void(color_ostream &, df::building *)> bldprops;
    horrible_t(std::function<void(color_ostream &, df::building *)> const & bldprops) : bldprops(bldprops) {}
    horrible_t & operator=(std::function<void(color_ostream &, df::building *)> const & bldprops) { this->bldprops = bldprops; return *this; }
    operator std::function<void(color_ostream &, df::building *)>() { return this->bldprops; }

#undef THE_HORROR
};

// vim: et:sw=4:ts=4

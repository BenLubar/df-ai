#pragma once

#include <map>
#include <string>
#include <vector>

#include "df/coord.h"
#include "df/tiletype_shape_basic.h"

namespace df
{
    struct building;
}

struct horrible_t;

typedef std::map<std::string, horrible_t> furniture;

struct room
{
    std::string status;
    std::string type;
    std::string subtype;
    df::coord min, max;
    std::vector<room *> accesspath;
    std::vector<furniture *> layout;
    int32_t owner;
    std::map<std::string, horrible_t> misc;

    room(df::coord min, df::coord max);
    room(std::string type, std::string subtype, df::coord min, df::coord max);
    ~room();

    inline df::coord size() const { return max - min + 1; }
    inline df::coord pos() const { return min + size() / 2; }

    void dig(std::string mode = "");
    void fixup_open();
    void fixup_open_tile(df::coord t, std::string d, furniture *f);
    void fixup_open_helper(df::coord t, std::string c, furniture *f);

    bool include(df::coord t) const;
    bool safe_include(df::coord t) const;
    std::string dig_mode(df::coord t) const;
    bool is_dug(df::tiletype_shape_basic want = tiletype_shape_basic::None) const;
    bool constructions_done() const;
    df::building *dfbuilding() const;
};

// vim: et:sw=4:ts=4

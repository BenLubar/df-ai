#pragma once

#include <map>
#include <string>
#include <vector>

#include "df/construction_type.h"
#include "df/coord.h"
#include "df/tile_dig_designation.h"
#include "df/tiletype_shape_basic.h"

namespace df
{
    struct building;
}

struct furniture;

struct room
{
    std::string status;
    std::string type;
    std::string subtype;
    std::string comment;
    df::coord min, max;
    std::vector<room *> accesspath;
    std::vector<furniture *> layout;
    int32_t owner;
    int32_t bld_id;
    int32_t squad_id;
    int32_t level;
    int32_t noblesuite;
    room *workshop;
    std::set<int32_t> users;
    df::coord channel_enable;
    bool has_users;
    bool furnished;
    bool queue_dig;
    bool temporary;
    bool outdoor;
    bool channeled;

    room(df::coord min, df::coord max, std::string comment = "");
    room(std::string type, std::string subtype, df::coord min, df::coord max, std::string comment = "");
    ~room();

    inline df::coord size() const { return max - min + df::coord(1, 1, 1); }
    inline df::coord pos() const
    {
        df::coord s = size();
        return min + df::coord(s.x / 2, s.y / 2, s.z / 2);
    }

    void dig(bool plan = false, bool channel = false);
    void fixup_open();
    void fixup_open_tile(df::coord t, df::tile_dig_designation d, furniture *f = nullptr);
    void fixup_open_helper(df::coord t, df::construction_type c, furniture *f = nullptr);

    bool include(df::coord t) const;
    bool safe_include(df::coord t) const;
    df::tile_dig_designation dig_mode(df::coord t) const;
    bool is_dug(df::tiletype_shape_basic want = tiletype_shape_basic::None) const;
    bool constructions_done() const;
    df::building *dfbuilding() const;
};

struct furniture
{
    std::string item;
    std::string subtype;
    df::construction_type construction;
    df::tile_dig_designation dig;
    std::string direction;
    std::string way;
    int32_t bld_id;
    int16_t x;
    int16_t y;
    int16_t z;
    furniture *target;
    std::set<int32_t> users;
    bool has_users;
    bool ignore;
    bool makeroom;
    bool internal;

    furniture() :
        item(),
        subtype(),
        construction(df::construction_type(-1)),
        dig(tile_dig_designation::Default),
        direction(),
        way(),
        bld_id(-1),
        x(0),
        y(0),
        z(0),
        target(nullptr),
        users(),
        has_users(false),
        ignore(false),
        makeroom(false),
        internal(false)
    {
    }
};

// vim: et:sw=4:ts=4

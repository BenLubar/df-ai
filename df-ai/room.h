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
    inline df::coord pos() const { return min + size() / 2; }

    void dig(std::string mode = "");
    void fixup_open();
    void fixup_open_tile(df::coord t, std::string d, furniture *f = nullptr);
    void fixup_open_helper(df::coord t, std::string c, furniture *f = nullptr);

    bool include(df::coord t) const;
    bool safe_include(df::coord t) const;
    std::string dig_mode(df::coord t) const;
    bool is_dug(df::tiletype_shape_basic want = tiletype_shape_basic::None) const;
    bool constructions_done() const;
    df::building *dfbuilding() const;
};

struct furniture
{
    std::string item;
    std::string subtype;
    df::construction_type construction = df::construction_type(-1);
    df::tile_dig_designation dig = tile_dig_designation::Default;
    std::string direction;
    std::string way;
    int32_t bld_id = -1;
    int32_t route_id = -1;
    int16_t x = 0;
    int16_t y = 0;
    int16_t z = 0;
    furniture *target = nullptr;
    std::set<int32_t> users;
    bool has_users = false;
    bool ignore = false;
    bool makeroom = false;
    bool internal = false;
};

// vim: et:sw=4:ts=4

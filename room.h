#pragma once

#include "apply.h"

#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "df/construction_type.h"
#include "df/coord.h"
#include "df/furnace_type.h"
#include "df/stockpile_list.h"
#include "df/tile_dig_designation.h"
#include "df/tiletype_shape_basic.h"
#include "df/workshop_type.h"

namespace df
{
    struct building;
}

struct furniture;

#define ROOM_ENUMS \
BEGIN_ENUM(room, status) \
    ENUM_ITEM(plan) \
    ENUM_ITEM(dig) \
    ENUM_ITEM(dug) \
    ENUM_ITEM(finished) \
END_ENUM(room, status) \
\
BEGIN_ENUM(room, type) \
    ENUM_ITEM(corridor) \
\
    ENUM_ITEM(barracks) \
    ENUM_ITEM(bedroom) \
    ENUM_ITEM(cemetery) \
    ENUM_ITEM(cistern) \
    ENUM_ITEM(dininghall) \
    ENUM_ITEM(farmplot) \
    ENUM_ITEM(furnace) \
    ENUM_ITEM(garbagedump) \
    ENUM_ITEM(infirmary) \
    ENUM_ITEM(jail) \
    ENUM_ITEM(location) \
    ENUM_ITEM(nobleroom) \
    ENUM_ITEM(outpost) \
    ENUM_ITEM(pasture) \
    ENUM_ITEM(pitcage) \
    ENUM_ITEM(pond) \
    ENUM_ITEM(stockpile) \
    ENUM_ITEM(tradedepot) \
    ENUM_ITEM(windmill) \
    ENUM_ITEM(workshop) \
END_ENUM(room, type) \
\
BEGIN_ENUM(corridor, type) \
    ENUM_ITEM(corridor) \
    ENUM_ITEM(veinshaft) \
    ENUM_ITEM(aqueduct) \
    ENUM_ITEM(outpost) \
    ENUM_ITEM(walkable) \
END_ENUM(corridor, type) \
\
BEGIN_ENUM(farm, type) \
    ENUM_ITEM(food) \
    ENUM_ITEM(cloth) \
END_ENUM(farm, type) \
\
BEGIN_ENUM(stockpile, type) \
    ENUM_ITEM(food) \
    ENUM_ITEM(furniture) \
    ENUM_ITEM(wood) \
    ENUM_ITEM(stone) \
    ENUM_ITEM(refuse) \
    ENUM_ITEM(animals) \
    ENUM_ITEM(corpses) \
    ENUM_ITEM(gems) \
    ENUM_ITEM(finished_goods) \
    ENUM_ITEM(cloth) \
    ENUM_ITEM(bars_blocks) \
    ENUM_ITEM(leather) \
    ENUM_ITEM(ammo) \
    ENUM_ITEM(armor) \
    ENUM_ITEM(weapons) \
    ENUM_ITEM(coins) \
    ENUM_ITEM(sheets) \
    ENUM_ITEM(fresh_raw_hide) \
END_ENUM(stockpile, type) \
\
BEGIN_ENUM(nobleroom, type) \
    ENUM_ITEM(tomb) \
    ENUM_ITEM(dining) \
    ENUM_ITEM(bedroom) \
    ENUM_ITEM(office) \
END_ENUM(nobleroom, type) \
\
BEGIN_ENUM(outpost, type) \
    ENUM_ITEM(cavern) \
END_ENUM(outpost, type) \
\
BEGIN_ENUM(location, type) \
    ENUM_ITEM(tavern) \
    ENUM_ITEM(library) \
    ENUM_ITEM(temple) \
END_ENUM(location, type) \
\
BEGIN_ENUM(cistern, type) \
    ENUM_ITEM(well) \
    ENUM_ITEM(reserve) \
END_ENUM(cistern, type) \
\
BEGIN_ENUM(layout, type) \
    ENUM_ITEM(none) \
\
    ENUM_ITEM(archery_target) \
    ENUM_ITEM(armor_stand) \
    ENUM_ITEM(bed) \
    ENUM_ITEM(bookcase) \
    ENUM_ITEM(cabinet) \
    ENUM_ITEM(cage) \
    ENUM_ITEM(cage_trap) \
    ENUM_ITEM(chair) \
    ENUM_ITEM(chest) \
    ENUM_ITEM(coffin) \
    ENUM_ITEM(door) \
    ENUM_ITEM(floodgate) \
    ENUM_ITEM(gear_assembly) \
    ENUM_ITEM(hatch) \
    ENUM_ITEM(hive) \
    ENUM_ITEM(lever) \
    ENUM_ITEM(nest_box) \
    ENUM_ITEM(restraint) \
    ENUM_ITEM(roller) \
    ENUM_ITEM(statue) \
    ENUM_ITEM(table) \
    ENUM_ITEM(track_stop) \
    ENUM_ITEM(traction_bench) \
    ENUM_ITEM(vertical_axle) \
    ENUM_ITEM(weapon_rack) \
    ENUM_ITEM(well) \
END_ENUM(layout, type) \
\
BEGIN_ENUM(task, type) \
    ENUM_ITEM(check_construct) \
    ENUM_ITEM(check_furnish) \
    ENUM_ITEM(check_idle) \
    ENUM_ITEM(check_rooms) \
    ENUM_ITEM(construct_activityzone) \
    ENUM_ITEM(construct_farmplot) \
    ENUM_ITEM(construct_furnace) \
    ENUM_ITEM(construct_stockpile) \
    ENUM_ITEM(construct_tradedepot) \
    ENUM_ITEM(construct_windmill) \
    ENUM_ITEM(construct_workshop) \
    ENUM_ITEM(dig_cistern) \
    ENUM_ITEM(dig_garbage) \
    ENUM_ITEM(dig_room) \
    ENUM_ITEM(dig_room_immediate) \
    ENUM_ITEM(furnish) \
    ENUM_ITEM(monitor_cistern) \
    ENUM_ITEM(monitor_farm_irrigation) \
    ENUM_ITEM(setup_farmplot) \
    ENUM_ITEM(want_dig) \
END_ENUM(task, type)

#define BEGIN_ENUM BEGIN_DECLARE_ENUM
#define ENUM_ITEM DECLARE_ENUM_ITEM
#define END_ENUM END_DECLARE_ENUM
ROOM_ENUMS
#undef BEGIN_ENUM
#undef ENUM_ITEM
#undef END_ENUM

struct room
{
    room_status::status status;
    room_type::type type;
    corridor_type::type corridor_type;
    farm_type::type farm_type;
    stockpile_type::type stockpile_type;
    nobleroom_type::type nobleroom_type;
    outpost_type::type outpost_type;
    location_type::type location_type;
    cistern_type::type cistern_type;
    df::workshop_type workshop_type;
    df::furnace_type furnace_type;
    std::string raw_type;
    std::string comment;
    df::coord min, max;
    std::vector<room *> accesspath;
    std::vector<furniture *> layout;
    int32_t owner;
    int32_t bld_id;
    int32_t squad_id;
    int32_t level;
    int32_t noblesuite;
    int32_t queue;
    room *workshop;
    std::set<int32_t> users;
    df::coord channel_enable;
    std::set<df::stockpile_list> stock_disable;
    bool stock_specific1;
    bool stock_specific2;
    size_t has_users;
    bool furnished;
    bool queue_dig;
    bool temporary;
    bool outdoor;
    bool channeled;

    room(room_type::type type, df::coord min, df::coord max, std::string comment = "");
    room(corridor_type::type subtype, df::coord min, df::coord max, std::string comment = "");
    room(farm_type::type subtype, df::coord min, df::coord max, std::string comment = "");
    room(stockpile_type::type subtype, df::coord min, df::coord max, std::string comment = "");
    room(nobleroom_type::type subtype, df::coord min, df::coord max, std::string comment = "");
    room(outpost_type::type subtype, df::coord min, df::coord max, std::string comment = "");
    room(location_type::type subtype, df::coord min, df::coord max, std::string comment = "");
    room(cistern_type::type subtype, df::coord min, df::coord max, std::string comment = "");
    room(df::workshop_type subtype, df::coord min, df::coord max, std::string comment = "");
    room(df::furnace_type subtype, df::coord min, df::coord max, std::string comment = "");
    ~room();

    inline df::coord size() const { return max - min + df::coord{ 1, 1, 1 }; }
    inline df::coord pos() const
    {
        df::coord s{ size() };
        return df::coord{ uint16_t(min.x + s.x / 2), uint16_t(min.y + s.y / 2), uint16_t(min.z + s.z / 2) };
    }

    void dig(bool plan = false, bool channel = false);

    bool include(df::coord t) const;
    bool safe_include(df::coord t) const;
    df::tile_dig_designation dig_mode(df::coord t) const;
    bool is_dug(df::tiletype_shape_basic want = tiletype_shape_basic::None) const
    {
        std::ostringstream discard;
        return is_dug(discard, want);
    }
    bool is_dug(std::ostream & reason, df::tiletype_shape_basic want = tiletype_shape_basic::None) const;
    bool constructions_done() const
    {
        std::ostringstream discard;
        return constructions_done(discard);
    }
    bool constructions_done(std::ostream & reason) const;
    df::building *dfbuilding() const;
};

struct furniture
{
    layout_type::type type;
    df::construction_type construction;
    df::tile_dig_designation dig;
    int32_t bld_id;
    df::coord pos;
    furniture *target;
    std::set<int32_t> users;
    size_t has_users;
    bool ignore;
    bool makeroom;
    bool internal;
    std::string comment;

    furniture(const std::string & comment = "") :
        type(layout_type::none),
        construction(construction_type::NONE),
        dig(tile_dig_designation::Default),
        bld_id(-1),
        pos(0, 0, 0),
        target(nullptr),
        users(),
        has_users(0),
        ignore(false),
        makeroom(false),
        internal(false),
        comment(comment)
    {
    }
};

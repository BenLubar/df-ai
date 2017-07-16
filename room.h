#pragma once

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

template<typename T>
inline bool df_ai_find_enum_item(T *var, const std::string & name, T count)
{
    std::ostringstream scratch;
    for (T i = T(); i < count; i = (T)(i + 1))
    {
        scratch.str(std::string());
        scratch << i;
        if (scratch.str() == name)
        {
            *var = i;
            return true;
        }
    }
    return false;
}

namespace room_status
{
    enum status
    {
        plan,
        dig,
        dug,
        finished,

        _room_status_count
    };
}

std::ostream & operator <<(std::ostream & stream, room_status::status status);
namespace DFHack
{
    template<> inline bool find_enum_item<room_status::status>(room_status::status *var, const std::string & name) { return df_ai_find_enum_item(var, name, room_status::_room_status_count); }
}

namespace room_type
{
    enum type
    {
        corridor,

        barracks,
        bedroom,
        cemetary,
        cistern,
        dininghall,
        farmplot,
        furnace,
        garbagedump,
        garbagepit,
        infirmary,
        location,
        nobleroom,
        outpost,
        pasture,
        pitcage,
        stockpile,
        tradedepot,
        workshop,

        _room_type_count
    };
}

std::ostream & operator <<(std::ostream & stream, room_type::type type);
namespace DFHack
{
    template<> inline bool find_enum_item<room_type::type>(room_type::type *var, const std::string & name) { return df_ai_find_enum_item(var, name, room_type::_room_type_count); }
}

namespace corridor_type
{
    enum type
    {
        corridor,
        veinshaft,
        aqueduct,
        outpost,
        walkable,

        _corridor_type_count
    };
}

std::ostream & operator <<(std::ostream & stream, corridor_type::type type);
namespace DFHack
{
    template<> inline bool find_enum_item<corridor_type::type>(corridor_type::type *var, const std::string & name) { return df_ai_find_enum_item(var, name, corridor_type::_corridor_type_count); }
}

namespace farm_type
{
    enum type
    {
        food,
        cloth,

        _farm_type_count
    };
}

std::ostream & operator <<(std::ostream & stream, farm_type::type type);
namespace DFHack
{
    template<> inline bool find_enum_item<farm_type::type>(farm_type::type *var, const std::string & name) { return df_ai_find_enum_item(var, name, farm_type::_farm_type_count); }
}

namespace stockpile_type
{
    enum type
    {
        food,
        furniture,
        wood,
        stone,
        refuse,
        animals,
        corpses,
        gems,
        finished_goods,
        cloth,
        bars_blocks,
        leather,
        ammo,
        armor,
        weapons,
        coins,
        sheets,
        fresh_raw_hide,

        _stockpile_type_count
    };
}

std::ostream & operator <<(std::ostream & stream, stockpile_type::type type);
namespace DFHack
{
    template<> inline bool find_enum_item<stockpile_type::type>(stockpile_type::type *var, const std::string & name) { return df_ai_find_enum_item(var, name, stockpile_type::_stockpile_type_count); }
}

namespace nobleroom_type
{
    enum type
    {
        tomb,
        dining,
        bedroom,
        office,

        _nobleroom_type_count
    };
}

std::ostream & operator <<(std::ostream & stream, nobleroom_type::type type);

namespace DFHack
{
    template<> inline bool find_enum_item<nobleroom_type::type>(nobleroom_type::type *var, const std::string & name) { return df_ai_find_enum_item(var, name, nobleroom_type::_nobleroom_type_count); }
}

namespace outpost_type
{
    enum type
    {
        cavern,

        _outpost_type_count
    };
}

std::ostream & operator <<(std::ostream & stream, outpost_type::type type);
namespace DFHack
{
    template<> inline bool find_enum_item<outpost_type::type>(outpost_type::type *var, const std::string & name) { return df_ai_find_enum_item(var, name, outpost_type::_outpost_type_count); }
}

namespace location_type
{
    enum type
    {
        tavern,
        library,
        temple,

        _location_type_count
    };
}

std::ostream & operator <<(std::ostream & stream, location_type::type type);
namespace DFHack
{
    template<> inline bool find_enum_item<location_type::type>(location_type::type *var, const std::string & name) { return df_ai_find_enum_item(var, name, location_type::_location_type_count); }
}

namespace cistern_type
{
    enum type
    {
        well,
        reserve,

        _cistern_type_count
    };
}

std::ostream & operator <<(std::ostream & stream, cistern_type::type type);
namespace DFHack
{
    template<> inline bool find_enum_item<cistern_type::type>(cistern_type::type *var, const std::string & name) { return df_ai_find_enum_item(var, name, cistern_type::_cistern_type_count); }
}

namespace layout_type
{
    enum type
    {
        none,

        archery_target,
        armor_stand,
        bed,
        bookcase,
        cabinet,
        cage_trap,
        chair,
        chest,
        coffin,
        door,
        floodgate,
        gear_assembly,
        hive,
        lever,
        nest_box,
        roller,
        table,
        track_stop,
        traction_bench,
        vertical_axle,
        weapon_rack,
        well,
        windmill,

        _layout_type_count
    };
}

std::ostream & operator <<(std::ostream & stream, layout_type::type type);
namespace DFHack
{
    template<> inline bool find_enum_item<layout_type::type>(layout_type::type *var, const std::string & name) { return df_ai_find_enum_item(var, name, layout_type::_layout_type_count); }
}

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
    bool has_users;
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

    inline df::coord size() const { return max - min + df::coord(1, 1, 1); }
    inline df::coord pos() const
    {
        df::coord s = size();
        return min + df::coord(s.x / 2, s.y / 2, s.z / 2);
    }

    void dig(bool plan = false, bool channel = false);

    bool include(df::coord t) const;
    bool safe_include(df::coord t) const;
    df::tile_dig_designation dig_mode(df::coord t) const;
    bool is_dug(df::tiletype_shape_basic want = tiletype_shape_basic::None) const;
    bool constructions_done() const;
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
    bool has_users;
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
        has_users(false),
        ignore(false),
        makeroom(false),
        internal(false),
        comment(comment)
    {
    }
};

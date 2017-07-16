#include "dfhack_shared.h"
#include "room.h"
#include "ai.h"
#include "plan.h"

#include "modules/Maps.h"

#include "df/building.h"
#include "df/tile_occupancy.h"

std::ostream & operator <<(std::ostream & stream, room_status::status status)
{
    switch (status)
    {
    case room_status::plan:
        return stream << "plan";
    case room_status::dig:
        return stream << "dig";
    case room_status::dug:
        return stream << "dug";
    case room_status::finished:
        return stream << "finished";

    case room_status::_room_status_count:
        return stream << "???";
    }
    return stream << "???";
}

std::ostream & operator <<(std::ostream & stream, room_type::type type)
{
    switch (type)
    {
    case room_type::corridor:
        return stream << "corridor";

    case room_type::barracks:
        return stream << "barracks";
    case room_type::bedroom:
        return stream << "bedroom";
    case room_type::cemetary:
        return stream << "cemetary";
    case room_type::cistern:
        return stream << "cistern";
    case room_type::dininghall:
        return stream << "dininghall";
    case room_type::farmplot:
        return stream << "farmplot";
    case room_type::furnace:
        return stream << "furnace";
    case room_type::garbagedump:
        return stream << "garbagedump";
    case room_type::garbagepit:
        return stream << "garbagepit";
    case room_type::infirmary:
        return stream << "infirmary";
    case room_type::location:
        return stream << "location";
    case room_type::nobleroom:
        return stream << "nobleroom";
    case room_type::outpost:
        return stream << "outpost";
    case room_type::pasture:
        return stream << "pasture";
    case room_type::pitcage:
        return stream << "pitcage";
    case room_type::pond:
        return stream << "pond";
    case room_type::stockpile:
        return stream << "stockpile";
    case room_type::tradedepot:
        return stream << "tradedepot";
    case room_type::workshop:
        return stream << "workshop";

    case room_type::_room_type_count:
        return stream << "???";
    }
    return stream << "???";
}

std::ostream & operator <<(std::ostream & stream, corridor_type::type type)
{
    switch (type)
    {
    case corridor_type::corridor:
        return stream << "corridor";
    case corridor_type::veinshaft:
        return stream << "veinshaft";
    case corridor_type::aqueduct:
        return stream << "aqueduct";
    case corridor_type::outpost:
        return stream << "outpost";
    case corridor_type::walkable:
        return stream << "walkable";

    case corridor_type::_corridor_type_count:
        return stream << "???";
    }
    return stream << "???";
}

std::ostream & operator <<(std::ostream & stream, farm_type::type type)
{
    switch (type)
    {
    case farm_type::food:
        return stream << "food";
    case farm_type::cloth:
        return stream << "cloth";

    case farm_type::_farm_type_count:
        return stream << "???";
    }
    return stream << "???";
}

std::ostream & operator <<(std::ostream & stream, stockpile_type::type type)
{
    switch (type)
    {
    case stockpile_type::food:
        return stream << "food";
    case stockpile_type::furniture:
        return stream << "furniture";
    case stockpile_type::wood:
        return stream << "wood";
    case stockpile_type::stone:
        return stream << "stone";
    case stockpile_type::refuse:
        return stream << "refuse";
    case stockpile_type::animals:
        return stream << "animals";
    case stockpile_type::corpses:
        return stream << "corpses";
    case stockpile_type::gems:
        return stream << "gems";
    case stockpile_type::finished_goods:
        return stream << "finished_goods";
    case stockpile_type::cloth:
        return stream << "cloth";
    case stockpile_type::bars_blocks:
        return stream << "bars_blocks";
    case stockpile_type::leather:
        return stream << "leather";
    case stockpile_type::ammo:
        return stream << "ammo";
    case stockpile_type::armor:
        return stream << "armor";
    case stockpile_type::weapons:
        return stream << "weapons";
    case stockpile_type::coins:
        return stream << "coins";
    case stockpile_type::sheets:
        return stream << "sheets";
    case stockpile_type::fresh_raw_hide:
        return stream << "fresh_raw_hide";

    case stockpile_type::_stockpile_type_count:
        return stream << "???";
    }
    return stream << "???";
}

std::ostream & operator <<(std::ostream & stream, nobleroom_type::type type)
{
    switch (type)
    {
    case nobleroom_type::tomb:
        return stream << "tomb";
    case nobleroom_type::dining:
        return stream << "dining";
    case nobleroom_type::bedroom:
        return stream << "bedroom";
    case nobleroom_type::office:
        return stream << "office";

    case nobleroom_type::_nobleroom_type_count:
        return stream << "???";
    }
    return stream << "???";
}

std::ostream & operator <<(std::ostream & stream, outpost_type::type type)
{
    switch (type)
    {
    case outpost_type::cavern:
        return stream << "cavern";

    case outpost_type::_outpost_type_count:
        return stream << "???";
    }
    return stream << "???";
}

std::ostream & operator <<(std::ostream & stream, location_type::type type)
{
    switch (type)
    {
    case location_type::tavern:
        return stream << "tavern";
    case location_type::library:
        return stream << "library";
    case location_type::temple:
        return stream << "temple";

    case location_type::_location_type_count:
        return stream << "???";
    }
    return stream << "???";
}

std::ostream & operator <<(std::ostream & stream, cistern_type::type type)
{
    switch (type)
    {
    case cistern_type::well:
        return stream << "well";
    case cistern_type::reserve:
        return stream << "reserve";

    case cistern_type::_cistern_type_count:
        return stream << "???";
    }
    return stream << "???";
}

std::ostream & operator <<(std::ostream & stream, layout_type::type type)
{
    switch (type)
    {
    case layout_type::none:
        return stream << "none";

    case layout_type::archery_target:
        return stream << "archery_target";
    case layout_type::armor_stand:
        return stream << "armor_stand";
    case layout_type::bed:
        return stream << "bed";
    case layout_type::bookcase:
        return stream << "bookcase";
    case layout_type::cabinet:
        return stream << "cabinet";
    case layout_type::cage_trap:
        return stream << "cage_trap";
    case layout_type::chair:
        return stream << "chair";
    case layout_type::chest:
        return stream << "chest";
    case layout_type::coffin:
        return stream << "coffin";
    case layout_type::door:
        return stream << "door";
    case layout_type::floodgate:
        return stream << "floodgate";
    case layout_type::gear_assembly:
        return stream << "gear_assembly";
    case layout_type::hive:
        return stream << "hive";
    case layout_type::lever:
        return stream << "lever";
    case layout_type::nest_box:
        return stream << "nest_box";
    case layout_type::roller:
        return stream << "roller";
    case layout_type::table:
        return stream << "table";
    case layout_type::track_stop:
        return stream << "track_stop";
    case layout_type::traction_bench:
        return stream << "traction_bench";
    case layout_type::vertical_axle:
        return stream << "vertical_axle";
    case layout_type::weapon_rack:
        return stream << "weapon_rack";
    case layout_type::well:
        return stream << "well";
    case layout_type::windmill:
        return stream << "windmill";

    case layout_type::_layout_type_count:
        return stream << "???";
    }
    return stream << "???";
}

room::room(room_type::type type, df::coord mins, df::coord maxs, std::string comment) :
    status(room_status::plan),
    type(type),
    corridor_type(),
    farm_type(),
    stockpile_type(),
    nobleroom_type(),
    outpost_type(),
    location_type(),
    cistern_type(),
    workshop_type(),
    furnace_type(),
    raw_type(""),
    comment(comment),
    min(mins),
    max(maxs),
    accesspath(),
    layout(),
    owner(-1),
    bld_id(-1),
    squad_id(-1),
    level(-1),
    noblesuite(-1),
    queue(0),
    workshop(nullptr),
    users(),
    channel_enable(),
    stock_disable(),
    stock_specific1(false),
    stock_specific2(false),
    has_users(false),
    furnished(false),
    queue_dig(false),
    temporary(false),
    outdoor(false),
    channeled(false)
{
    channel_enable.clear();
    if (min.x > max.x)
        std::swap(min.x, max.x);
    if (min.y > max.y)
        std::swap(min.y, max.y);
    if (min.z > max.z)
        std::swap(min.z, max.z);
}

room::room(corridor_type::type subtype, df::coord mins, df::coord maxs, std::string comment) :
    room::room(room_type::corridor, mins, maxs, comment)
{
    corridor_type = subtype;
}

room::room(farm_type::type subtype, df::coord mins, df::coord maxs, std::string comment) :
    room::room(room_type::farmplot, mins, maxs, comment)
{
    farm_type = subtype;
}

room::room(stockpile_type::type subtype, df::coord mins, df::coord maxs, std::string comment) :
    room::room(room_type::stockpile, mins, maxs, comment)
{
    stockpile_type = subtype;
}

room::room(nobleroom_type::type subtype, df::coord mins, df::coord maxs, std::string comment) :
    room::room(room_type::nobleroom, mins, maxs, comment)
{
    nobleroom_type = subtype;
}

room::room(outpost_type::type subtype, df::coord mins, df::coord maxs, std::string comment) :
    room::room(room_type::outpost, mins, maxs, comment)
{
    outpost_type = subtype;
}

room::room(location_type::type subtype, df::coord mins, df::coord maxs, std::string comment) :
    room::room(room_type::location, mins, maxs, comment)
{
    location_type = subtype;
}

room::room(cistern_type::type subtype, df::coord mins, df::coord maxs, std::string comment) :
    room::room(room_type::cistern, mins, maxs, comment)
{
    cistern_type = subtype;
}

room::room(df::workshop_type subtype, df::coord mins, df::coord maxs, std::string comment) :
    room::room(room_type::workshop, mins, maxs, comment)
{
    workshop_type = subtype;
}

room::room(df::furnace_type subtype, df::coord mins, df::coord maxs, std::string comment) :
    room::room(room_type::furnace, mins, maxs, comment)
{
    furnace_type = subtype;
}

room::~room()
{
    for (auto it = layout.begin(); it != layout.end(); it++)
    {
        delete *it;
    }
}

void room::dig(bool plan, bool channel)
{
    for (int16_t x = min.x; x <= max.x; x++)
    {
        for (int16_t y = min.y; y <= max.y; y++)
        {
            for (int16_t z = min.z; z <= max.z; z++)
            {
                df::coord t(x, y, z);
                df::tiletype *tt = Maps::getTileType(t);
                if (tt)
                {
                    if (ENUM_ATTR(tiletype, material, *tt) == tiletype_material::CONSTRUCTION)
                    {
                        continue;
                    }
                    df::tile_dig_designation dm = channel ? tile_dig_designation::Channel : dig_mode(t);
                    if (((dm == tile_dig_designation::DownStair || dm == tile_dig_designation::Channel) && ENUM_ATTR(tiletype, shape, *tt) != tiletype_shape::STAIR_DOWN && ENUM_ATTR(tiletype_shape, basic_shape, ENUM_ATTR(tiletype, shape, *tt)) != tiletype_shape_basic::Open) || ENUM_ATTR(tiletype, shape, *tt) == tiletype_shape::WALL)
                    {
                        Plan::dig_tile(t, dm);
                        if (plan)
                        {
                            Maps::getTileOccupancy(t)->bits.dig_marked = 1;
                        }
                    }
                }
            }
        }
    }

    if (plan)
        return;

    for (auto it = layout.begin(); it != layout.end(); it++)
    {
        furniture *f = *it;
        df::coord t = min + f->pos;
        df::tiletype *tt = Maps::getTileType(t);
        if (tt)
        {
            if (ENUM_ATTR(tiletype, material, *tt) == tiletype_material::CONSTRUCTION)
                continue;

            if (f->dig != tile_dig_designation::Default)
            {
                if (ENUM_ATTR(tiletype_shape, basic_shape, ENUM_ATTR(tiletype, shape, *tt)) == tiletype_shape_basic::Wall || (f->dig == tile_dig_designation::Channel && ENUM_ATTR(tiletype_shape, basic_shape, ENUM_ATTR(tiletype, shape, *tt)) != tiletype_shape_basic::Open))
                {
                    Plan::dig_tile(t, f->dig);
                }
            }
            else
            {
                df::tile_dig_designation dm = dig_mode(t);
                if ((dm == tile_dig_designation::DownStair && ENUM_ATTR(tiletype, shape, *tt) != tiletype_shape::STAIR_DOWN) || ENUM_ATTR(tiletype, shape, *tt) == tiletype_shape::WALL)
                {
                    Plan::dig_tile(t, dm);
                }
            }
        }
    }
}

bool room::include(df::coord t) const
{
    return min.x <= t.x && max.x >= t.x && min.y <= t.y && max.y >= t.y && min.z <= t.z && max.z >= t.z;
}

bool room::safe_include(df::coord t) const
{
    if (min.x - 1 <= t.x && max.x + 1 >= t.x && min.y - 1 <= t.y && max.y + 1 >= t.y && min.z <= t.z && max.z >= t.z)
        return true;

    for (auto it = layout.begin(); it != layout.end(); it++)
    {
        furniture *f = *it;
        df::coord ft = min + f->pos;
        if (ft.x - 1 <= t.x && ft.x + 1 >= t.x && ft.y - 1 <= t.y && ft.y + 1 >= t.y && ft.z == t.z)
            return true;
    }

    return false;
}

df::tile_dig_designation room::dig_mode(df::coord t) const
{
    for (auto f : layout)
    {
        if (min + f->pos == t && f->dig != tile_dig_designation::Default)
        {
            return f->dig;
        }
    }

    if (type != room_type::corridor)
    {
        return tile_dig_designation::Default;
    }
    bool wantup = include(t + df::coord(0, 0, 1));
    bool wantdown = include(t - df::coord(0, 0, 1));

    // XXX
    extern AI *dwarfAI;
    wantup = wantup || dwarfAI->plan->corridor_include_hack(this, t, t + df::coord(0, 0, 1));
    wantdown = wantdown || dwarfAI->plan->corridor_include_hack(this, t, t - df::coord(0, 0, 1));

    if (wantup)
        return wantdown ? tile_dig_designation::UpDownStair : tile_dig_designation::UpStair;
    else
        return wantdown ? tile_dig_designation::DownStair : tile_dig_designation::Default;
}

bool room::is_dug(df::tiletype_shape_basic want) const
{
    std::set<df::coord> holes;
    for (auto it = layout.begin(); it != layout.end(); it++)
    {
        furniture *f = *it;
        if (f->ignore)
            continue;

        df::coord ft = min + f->pos;

        if (f->dig == tile_dig_designation::No)
        {
            holes.insert(ft);
            continue;
        }

        switch (ENUM_ATTR(tiletype_shape, basic_shape, ENUM_ATTR(tiletype, shape, *Maps::getTileType(ft))))
        {
        case tiletype_shape_basic::Wall:
            return false;
        case tiletype_shape_basic::Open:
            break;
        default:
            if (f->dig == tile_dig_designation::Channel)
                return false;
            break;
        }
    }
    for (int16_t x = min.x; x <= max.x; x++)
    {
        for (int16_t y = min.y; y <= max.y; y++)
        {
            for (int16_t z = min.z; z <= max.z; z++)
            {
                if (holes.count(df::coord(x, y, z)))
                {
                    continue;
                }
                df::tiletype_shape s = ENUM_ATTR(tiletype, shape, *Maps::getTileType(x, y, z));
                if (s == tiletype_shape::WALL)
                {
                    return false;
                }
                if (want != tiletype_shape_basic::None)
                {
                    df::tiletype_shape_basic sb = ENUM_ATTR(tiletype_shape, basic_shape, s);
                    if (want != sb)
                    {
                        return false;
                    }
                }
            }
        }
    }
    return true;
}

bool room::constructions_done() const
{
    for (auto it = layout.begin(); it != layout.end(); it++)
    {
        furniture *f = *it;

        df::coord ft = min + f->pos;

        auto ts = ENUM_ATTR(tiletype, shape, *Maps::getTileType(ft));

        bool ok = true;
        switch (f->construction)
        {
        case construction_type::NONE:
            continue;
        case construction_type::Fortification:
            ok = ts == tiletype_shape::FORTIFICATION;
            break;
        case construction_type::Wall:
            ok = ts == tiletype_shape::WALL;
            break;
        case construction_type::Floor:
            ok = ts == tiletype_shape::FLOOR;
            break;
        case construction_type::UpStair:
            ok = ts == tiletype_shape::STAIR_UP;
            break;
        case construction_type::DownStair:
            ok = ts == tiletype_shape::STAIR_DOWN;
            break;
        case construction_type::UpDownStair:
            ok = ts == tiletype_shape::STAIR_UPDOWN;
            break;
        case construction_type::Ramp:
            ok = ts == tiletype_shape::RAMP;
            break;
        default:
            break;
        }

        if (!ok)
        {
            return false;
        }
    }
    return true;
}

df::building *room::dfbuilding() const
{
    return df::building::find(bld_id);
}

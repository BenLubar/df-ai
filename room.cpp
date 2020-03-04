#include "dfhack_shared.h"
#include "room.h"
#include "ai.h"
#include "plan.h"

#include "modules/Maps.h"

#include "df/building.h"
#include "df/tile_occupancy.h"

#define BEGIN_ENUM BEGIN_IMPLEMENT_ENUM
#define ENUM_ITEM IMPLEMENT_ENUM_ITEM
#define END_ENUM END_IMPLEMENT_ENUM
ROOM_ENUMS
#undef BEGIN_ENUM
#undef ENUM_ITEM
#undef END_ENUM

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
    has_users(0),
    furnished(false),
    queue_dig(false),
    temporary(false),
    outdoor(false),
    channeled(false),
    required_value(0)
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
                        AI::dig_tile(t, dm);
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
                    AI::dig_tile(t, f->dig);
                }
            }
            else
            {
                df::tile_dig_designation dm = dig_mode(t);
                if ((dm == tile_dig_designation::DownStair && ENUM_ATTR(tiletype, shape, *tt) != tiletype_shape::STAIR_DOWN) || ENUM_ATTR(tiletype, shape, *tt) == tiletype_shape::WALL)
                {
                    AI::dig_tile(t, dm);
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
    extern std::unique_ptr<AI> dwarfAI;
    wantup = wantup || dwarfAI->plan.corridor_include_hack(this, t, t + df::coord(0, 0, 1));
    wantdown = wantdown || dwarfAI->plan.corridor_include_hack(this, t, t - df::coord(0, 0, 1));

    if (wantup)
        return wantdown ? tile_dig_designation::UpDownStair : tile_dig_designation::UpStair;
    else
        return wantdown ? tile_dig_designation::DownStair : tile_dig_designation::Default;
}

bool room::is_dug(std::ostream & reason, df::tiletype_shape_basic want) const
{
    std::set<df::coord> holes;
    for (auto f : layout)
    {
        if (f->ignore)
            continue;

        df::coord ft = min + f->pos;

        if (f->dig == tile_dig_designation::No)
        {
            holes.insert(ft);
            continue;
        }

        auto sb = ENUM_ATTR(tiletype_shape, basic_shape, ENUM_ATTR(tiletype, shape, *Maps::getTileType(ft)));
        switch (sb)
        {
        case tiletype_shape_basic::Wall:
            reason << enum_item_key(f->dig) << "-designated tile at (" << f->pos.x << ", " << f->pos.y << ", " << f->pos.z << ") is Wall";
            return false;
        case tiletype_shape_basic::Open:
            break;
        default:
            if (f->dig == tile_dig_designation::Channel)
            {
                reason << "Channel-designated tile at (" << f->pos.x << ", " << f->pos.y << ", " << f->pos.z << ") is " << enum_item_key(sb);
                return false;
            }
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
                    reason << "interior tile at (" << (x - min.x) << ", " << (y - min.y) << ", " << (z - min.z) << ") is Wall";
                    return false;
                }
                if (want != tiletype_shape_basic::None)
                {
                    df::tiletype_shape_basic sb = ENUM_ATTR(tiletype_shape, basic_shape, s);
                    if (want != sb)
                    {
                        reason << "tile at (" << (x - min.x) << ", " << (y - min.y) << ", " << (z - min.z) << ") is " << enum_item_key(sb) << " but want " << enum_item_key(want);
                        return false;
                    }
                }
            }
        }
    }
    return true;
}

bool room::constructions_done(std::ostream & reason) const
{
    for (auto it = layout.begin(); it != layout.end(); it++)
    {
        furniture *f = *it;

        df::coord ft = min + f->pos;

        auto ts = ENUM_ATTR(tiletype, shape, *Maps::getTileType(ft));

        df::tiletype_shape want;
        switch (f->construction)
        {
        case construction_type::NONE:
            continue;
        case construction_type::Fortification:
            want = tiletype_shape::FORTIFICATION;
            break;
        case construction_type::Wall:
            want = tiletype_shape::WALL;
            break;
        case construction_type::Floor:
            want = tiletype_shape::FLOOR;
            break;
        case construction_type::UpStair:
            want = tiletype_shape::STAIR_UP;
            break;
        case construction_type::DownStair:
            want = tiletype_shape::STAIR_DOWN;
            break;
        case construction_type::UpDownStair:
            want = tiletype_shape::STAIR_UPDOWN;
            break;
        case construction_type::Ramp:
            want = tiletype_shape::RAMP;
            break;
        default:
            reason << "unknown construction type " << enum_item_key(f->construction) << " at (" << f->pos.x << ", " << f->pos.y << ", " << f->pos.z << ")";
            return false;
        }

        if (ts != want)
        {
            reason << "want construction type " << enum_item_key(f->construction) << " at (" << f->pos.x << ", " << f->pos.y << ", " << f->pos.z << ") but have " << enum_item_key(ts) << " instead of " << enum_item_key(want);
            return false;
        }
    }
    return true;
}

df::building *room::dfbuilding() const
{
    return df::building::find(bld_id);
}

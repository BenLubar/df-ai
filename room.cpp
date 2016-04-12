#include "dfhack_shared.h"
#include "room.h"
#include "ai.h"
#include "plan.h"

#include "modules/Maps.h"

#include "df/building.h"

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
        case room_type::stockpile:
            return stream << "stockpile";
        case room_type::workshop:
            return stream << "workshop";

        case room_type::_room_type_count:
            return stream << "???";
    }
    return stream << "???";
}

room::room(df::coord mins, df::coord maxs, std::string comment) :
    status(room_status::plan),
    type(room_type::corridor),
    subtype(""),
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

room::room(room_type::type type, std::string subtype, df::coord mins, df::coord maxs, std::string comment) :
    status(room_status::plan),
    type(type),
    subtype(subtype),
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

std::string room_type_for_debugging;
std::string room_subtype_for_debugging;

room::~room()
{
    room_type_for_debugging = type;
    room_subtype_for_debugging = subtype;
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
        df::coord t = min + df::coord(f->x, f->y, f->z);
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

void room::fixup_open()
{
    for (int16_t x = min.x; x <= max.x; x++)
    {
        for (int16_t y = min.y; y <= max.y; y++)
        {
            for (int16_t z = min.z; z <= max.z; z++)
            {
                df::coord t(x, y, z);
                for (auto it = layout.begin(); it != layout.end(); it++)
                {
                    furniture *f = *it;
                    df::coord ft = min + df::coord(f->x, f->y, f->z);
                    if (t == ft)
                    {
                        if (f->construction == construction_type::NONE)
                        {
                            fixup_open_tile(ft, f->dig, f);
                        }
                        t.clear();
                        break;
                    }
                }
                if (t.isValid())
                {
                    fixup_open_tile(t, dig_mode(t));
                }
            }
        }
    }
}

void room::fixup_open_tile(df::coord t, df::tile_dig_designation d, furniture *f)
{
    df::tiletype *tt = Maps::getTileType(t);
    if (!tt)
        return;

    df::tiletype_shape_basic sb = ENUM_ATTR(tiletype_shape, basic_shape,
            ENUM_ATTR(tiletype, shape, *tt));

    switch (d)
    {
        case tile_dig_designation::Channel:
            // do nothing
            break;
        case tile_dig_designation::No:
            if (sb == tiletype_shape_basic::Open || sb == tiletype_shape_basic::Floor)
            {
                fixup_open_helper(t, construction_type::Wall, f);
            }
            break;
        case tile_dig_designation::Default:
            if (sb == tiletype_shape_basic::Open)
            {
                fixup_open_helper(t, construction_type::Floor, f);
            }
            break;
        case tile_dig_designation::UpDownStair:
            if (sb == tiletype_shape_basic::Open || sb == tiletype_shape_basic::Floor)
            {
                fixup_open_helper(t, construction_type::UpDownStair, f);
            }
            break;
        case tile_dig_designation::UpStair:
            if (sb == tiletype_shape_basic::Open || sb == tiletype_shape_basic::Floor)
            {
                fixup_open_helper(t, construction_type::UpStair, f);
            }
            break;
        case tile_dig_designation::Ramp:
            if (sb == tiletype_shape_basic::Open || sb == tiletype_shape_basic::Floor)
            {
                fixup_open_helper(t, construction_type::Ramp, f);
            }
            break;
        case tile_dig_designation::DownStair:
            if (sb == tiletype_shape_basic::Open)
            {
                fixup_open_helper(t, construction_type::DownStair, f);
            }
            break;
    }
}

void room::fixup_open_helper(df::coord t, df::construction_type c, furniture *f)
{
    if (!f)
    {
        f = new furniture();
        f->x = t.x - min.x;
        f->y = t.y - min.y;
        f->z = t.z - min.z;
        layout.push_back(f);
    }
    f->construction = c;
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
        df::coord ft = min + df::coord(f->x, f->y, f->z);
        if (ft.x - 1 <= t.x && ft.x + 1 >= t.x && ft.y - 1 <= t.y && ft.y + 1 >= t.y && ft.z == t.z)
            return true;
    }

    return false;
}

df::tile_dig_designation room::dig_mode(df::coord t) const
{
    if (type != room_type::corridor)
    {
        return tile_dig_designation::Default;
    }
    bool wantup = include(t + df::coord(0, 0, 1));
    bool wantdown = include(t - df::coord(0, 0, 1));

    // XXX
    extern AI *dwarfAI;
    wantup = wantup || dwarfAI->plan->corridor_include_hack(this, t + df::coord(0, 0, 1));
    wantdown = wantdown || dwarfAI->plan->corridor_include_hack(this, t - df::coord(0, 0, 1));

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

        df::coord ft = min + df::coord(f->x, f->y, f->z);

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
        if (f->construction == construction_type::NONE)
            continue;
        df::coord ft = min + df::coord(f->x, f->y, f->z);
        // TODO check actual tile shape vs construction type
        if (ENUM_ATTR(tiletype_shape, basic_shape, ENUM_ATTR(tiletype, shape, *Maps::getTileType(ft))) == tiletype_shape_basic::Open)
            return false;
    }
    return true;
}

df::building *room::dfbuilding() const
{
    return df::building::find(bld_id);
}

// vim: et:sw=4:ts=4

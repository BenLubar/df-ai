#include "dfhack_shared.h"
#include "room.h"
#include "ai.h"
#include "plan.h"

#include "modules/Maps.h"

#include "df/building.h"

room::room(df::coord mins, df::coord maxs, std::string comment) :
    status("plan"),
    type("corridor"),
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

room::room(std::string type, std::string subtype, df::coord mins, df::coord maxs, std::string comment) :
    status("plan"),
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

room::~room()
{
    for (furniture *f : layout)
    {
        delete f;
    }
}

void room::dig(std::string mode)
{
    bool plandig = false;
    if (mode == "plan")
    {
        plandig = true;
        mode = "";
    }

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
                    std::string dm = mode.empty() ? dig_mode(t) : mode;
                    if (dm != "No" && ENUM_ATTR(tiletype, material, *tt) == tiletype_material::TREE)
                    {
                        dm = "Default";
                        t = Plan::find_tree_base(t);
                        tt = Maps::getTileType(t);
                    }
                    if (((dm == "DownStair" || dm == "Channel") && ENUM_ATTR(tiletype, shape, *tt) != tiletype_shape::STAIR_DOWN && ENUM_ATTR(tiletype_shape, basic_shape, ENUM_ATTR(tiletype, shape, *tt)) != tiletype_shape_basic::Open) || ENUM_ATTR(tiletype, shape, *tt) == tiletype_shape::WALL)
                    {
                        Plan::dig_tile(t, dm);
                    }
                }
            }
        }
    }

    if (plandig)
        return;

    for (furniture *f : layout)
    {
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
                    Plan::dig_tile(t, ENUM_KEY_STR(tile_dig_designation, f->dig));
                }
            }
            else
            {
                std::string dm = dig_mode(t);
                if ((dm == "DownStair" && ENUM_ATTR(tiletype, shape, *tt) != tiletype_shape::STAIR_DOWN) || ENUM_ATTR(tiletype, shape, *tt) == tiletype_shape::WALL)
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
                for (furniture *f : layout)
                {
                    df::coord ft = min + df::coord(f->x, f->y, f->z);
                    if (t == ft)
                    {
                        if (f->construction == df::construction_type(-1))
                        {
                            fixup_open_tile(ft, ENUM_KEY_STR(tile_dig_designation, f->dig), f);
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

void room::fixup_open_tile(df::coord t, std::string d, furniture *f)
{
    df::tiletype *tt = Maps::getTileType(t);
    if (!tt)
        return;

    df::tiletype_shape_basic sb = ENUM_ATTR(tiletype_shape, basic_shape,
            ENUM_ATTR(tiletype, shape, *tt));

    if (d == "Channel" || d == "No")
    {
        // do nothing
    }
    else if (d == "Default")
    {
        if (sb == tiletype_shape_basic::Open)
        {
            fixup_open_helper(t, "Floor", f);
        }
    }
    else if (d == "UpDownStair" || d == "UpStair" || d == "Ramp")
    {
        if (sb == tiletype_shape_basic::Open || sb == tiletype_shape_basic::Floor)
        {
            fixup_open_helper(t, d, f);
        }
    }
    else if (d == "DownStair")
    {
        if (sb == tiletype_shape_basic::Open)
        {
            fixup_open_helper(t, d, f);
        }
    }
}

void room::fixup_open_helper(df::coord t, std::string c, furniture *f)
{
    if (!f)
    {
        f = new furniture();
        f->x = t.x - min.x;
        f->y = t.y - min.y;
        f->z = t.z - min.z;
        layout.push_back(f);
    }
    find_enum_item(&f->construction, c);
}

bool room::include(df::coord t) const
{
    return min.x <= t.x && max.x >= t.x && min.y <= t.y && max.y >= t.y && min.z <= t.z && max.z >= t.z;
}

bool room::safe_include(df::coord t) const
{
    if (min.x - 1 <= t.x && max.x + 1 >= t.x && min.y - 1 <= t.y && max.y + 1 >= t.y && min.z <= t.z && max.z >= t.z)
        return true;

    for (furniture *f : layout)
    {
        df::coord ft = min + df::coord(f->x, f->y, f->z);
        if (ft.x - 1 <= t.x && ft.x + 1 >= t.x && ft.y - 1 <= t.y && ft.y + 1 >= t.y && ft.z == t.z)
            return true;
    }

    return false;
}

std::string room::dig_mode(df::coord t) const
{
    if (type != "corridor")
    {
        return "Default";
    }
    bool wantup = include(t + df::coord(0, 0, 1));
    bool wantdown = include(t - df::coord(0, 0, 1));

    // XXX
    extern AI *dwarfAI;
    wantup = wantup || dwarfAI->plan->corridor_include_hack(this, t + df::coord(0, 0, 1));
    wantdown = wantdown || dwarfAI->plan->corridor_include_hack(this, t - df::coord(0, 0, 1));

    if (wantup)
        return wantdown ? "UpDownStair" : "UpStair";
    else
        return wantdown ? "DownStair" : "Default";
}

bool room::is_dug(df::tiletype_shape_basic want) const
{
    std::set<df::coord> holes;
    for (furniture *f : layout)
    {
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
    for (furniture *f : layout)
    {
        if (f->construction == df::construction_type(-1))
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

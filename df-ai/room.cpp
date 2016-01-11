#include "dfhack_shared.h"
#include "room.h"
#include "ai.h"
#include "plan.h"

#include "modules/Maps.h"

#include "df/building.h"

room::room(df::coord min, df::coord max) :
    status("plan"),
    type("corridor"),
    subtype(""),
    min(min),
    max(max),
    accesspath(),
    layout(),
    owner(-1),
    misc()
{
    if (min.x > max.x)
        std::swap(min.x, max.x);
    if (min.y > max.y)
        std::swap(min.y, max.y);
    if (min.z > max.z)
        std::swap(min.z, max.z);
}

room::room(std::string type, std::string subtype, df::coord min, df::coord max) :
    status("plan"),
    type(type),
    subtype(subtype),
    min(min),
    max(max),
    accesspath(),
    layout(),
    owner(-1),
    misc()
{
    if (min.x > max.x)
        std::swap(min.x, max.x);
    if (min.y > max.y)
        std::swap(min.y, max.y);
    max.z = min.z;
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
        df::coord t = min + df::coord(f->count("x") ? f->at("x").id : 0, f->count("y") ? f->at("y").id : 0, f->count("z") ? f->at("z").id : 0);
        df::tiletype *tt = Maps::getTileType(t);
        if (tt)
        {
            if (ENUM_ATTR(tiletype, material, *tt) == tiletype_material::CONSTRUCTION)
                continue;

            if (f->count("dig"))
            {
                if (ENUM_ATTR(tiletype_shape, basic_shape, ENUM_ATTR(tiletype, shape, *tt)) == tiletype_shape_basic::Wall || (f->at("dig") == "Channel" && ENUM_ATTR(tiletype_shape, basic_shape, ENUM_ATTR(tiletype, shape, *tt)) == tiletype_shape_basic::Open))
                {
                    Plan::dig_tile(t, f->at("dig").str);
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
                    df::coord ft = min + df::coord(f->count("x") ? f->at("x").id : 0, f->count("y") ? f->at("y").id : 0, f->count("z") ? f->at("z").id : 0);
                    if (t == ft)
                    {
                        if (!f->count("construction"))
                        {
                            fixup_open_tile(ft, f->count("dig") ? f->at("dig").str : "default", f);
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
        f = new furniture{
            {"x", horrible_t(t.x - min.x)},
            {"y", horrible_t(t.y - min.y)},
            {"z", horrible_t(t.z - min.z)},
        };
        layout.push_back(f);
    }
    (*f)["construction"] = c;
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
        df::coord ft = min + df::coord(f->count("x") ? f->at("x").id : 0, f->count("y") ? f->at("y").id : 0, f->count("z") ? f->at("z").id : 0);
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
        if (f->count("ignore"))
            continue;

        df::coord ft = min + df::coord(f->count("x") ? f->at("x").id : 0, f->count("y") ? f->at("y").id : 0, f->count("z") ? f->at("z").id : 0);

        if (f->count("dig") && f->at("dig") == "No")
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
                if (f->count("dig") && f->at("dig") == "Channel")
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
        if (!f->count("construction"))
            continue;
        df::coord ft = min + df::coord(f->count("x") ? f->at("x").id : 0, f->count("y") ? f->at("y").id : 0, f->count("z") ? f->at("z").id : 0);
        // TODO check actual tile shape vs construction type
        if (ENUM_ATTR(tiletype_shape, basic_shape, ENUM_ATTR(tiletype, shape, *Maps::getTileType(ft))) == tiletype_shape_basic::Open)
            return false;
    }
    return true;
}

df::building *room::dfbuilding() const
{
    if (misc.count("bld_id"))
        return df::building::find(misc.at("bld_id").id);
    return nullptr;
}

// vim: et:sw=4:ts=4

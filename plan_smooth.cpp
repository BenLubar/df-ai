#include "ai.h"
#include "plan.h"

#include "modules/Maps.h"

#include "df/engraving.h"
#include "df/map_block.h"
#include "df/tile_designation.h"
#include "df/tile_occupancy.h"
#include "df/world.h"

REQUIRE_GLOBAL(world);

bool Plan::smooth_room(color_ostream &, room *r, bool engrave)
{
    std::set<df::coord> tiles;
    auto insert_tile = [&](df::coord t)
    {
        auto tt = Maps::getTileType(t);
        auto & room_z = room_by_z.at(t.z);
        if (ENUM_ATTR(tiletype_shape, basic_shape, ENUM_ATTR(tiletype, shape, *tt)) != tiletype_shape_basic::Wall ||
            std::find_if(room_z.begin(), room_z.end(), [t](room* o) -> bool { return o->include(t) && o->dig_mode(t) != tile_dig_designation::No &&
                std::find_if(o->layout.begin(), o->layout.end(), [t, o](furniture* f) -> bool { return o->min + f->pos == t &&
                    f->dig == tile_dig_designation::No && f->construction == construction_type::Wall; }) == o->layout.end(); }) == room_z.end())
        {
            tiles.insert(t);
        }
    };
    for (auto f : r->layout)
    {
        if (f->type == layout_type::door)
        {
            continue;
        }

        for (int16_t dx = -1; dx <= 1; dx++)
        {
            for (int16_t dy = -1; dy <= 1; dy++)
            {
                insert_tile(r->min + f->pos + df::coord(dx, dy, 0));
            }
        }
    }
    for (int16_t x = r->min.x - 1; x <= r->max.x + 1; x++)
    {
        for (int16_t y = r->min.y - 1; y <= r->max.y + 1; y++)
        {
            for (int16_t z = r->min.z; z <= r->max.z; z++)
            {
                insert_tile(df::coord(x, y, z));
            }
        }
    }
    return smooth(tiles, engrave);
}

// smooth a room and its accesspath corridors (recursive)
void Plan::smooth_room_access(color_ostream & out, room *r)
{
    smooth_room(out, r);
    for (auto it : r->accesspath)
    {
        smooth_room_access(out, it);
    }
}

bool Plan::smooth_xyz(df::coord min, df::coord max, bool engrave)
{
    std::set<df::coord> tiles;
    for (int16_t x = min.x; x <= max.x; x++)
    {
        for (int16_t y = min.y; y <= max.y; y++)
        {
            for (int16_t z = min.z; z <= max.z; z++)
            {
                tiles.insert(df::coord(x, y, z));
            }
        }
    }
    return smooth(tiles, engrave);
}

bool Plan::smooth(std::set<df::coord> tiles, bool engrave)
{
    bool all_already_smooth = true;

    // remove tiles that are not smoothable
    for (auto it = tiles.begin(); it != tiles.end(); )
    {
        // not a smoothable material
        df::tiletype tt = *Maps::getTileType(*it);
        df::tiletype_material mat = ENUM_ATTR(tiletype, material, tt);
        if (mat != tiletype_material::STONE &&
            mat != tiletype_material::MINERAL)
        {
            tiles.erase(it++);
            continue;
        }

        // already designated for something
        df::tile_designation des = *Maps::getTileDesignation(*it);
        if (des.bits.dig != tile_dig_designation::No ||
            des.bits.smooth != 0 ||
            des.bits.hidden)
        {
            all_already_smooth = false;
            tiles.erase(it++);
            continue;
        }

        // already smooth
        if (is_smooth(*it, engrave))
        {
            tiles.erase(it++);
            continue;
        }

        // wrong shape
        df::tiletype_shape s = ENUM_ATTR(tiletype, shape, tt);
        df::tiletype_shape_basic sb = ENUM_ATTR(tiletype_shape, basic_shape, s);
        if (sb != tiletype_shape_basic::Wall &&
            sb != tiletype_shape_basic::Floor)
        {
            tiles.erase(it++);
            continue;
        }

        it++;
    }

    // remove tiles that are already being smoothed
    for (auto j : world->jobs.list)
    {
        if (j->job_type == job_type::DetailWall ||
            j->job_type == job_type::DetailFloor)
        {
            all_already_smooth = false;
            tiles.erase(j->pos);
        }
    }

    // mark the tiles to be smoothed!
    for (auto t : tiles)
    {
        Maps::getTileDesignation(t)->bits.smooth = engrave ? 2 : 1;
        Maps::getTileOccupancy(t)->bits.dig_marked = 0;
        auto block = Maps::getTileBlock(t);
        block->flags.bits.designated = true;
        block->dsgn_check_cooldown = 0;
    }

    return all_already_smooth;
}

bool Plan::is_smooth(df::coord t, bool engrave)
{
    df::tiletype tt = *Maps::getTileType(t);
    df::tiletype_material mat = ENUM_ATTR(tiletype, material, tt);
    df::tiletype_shape s = ENUM_ATTR(tiletype, shape, tt);
    df::tiletype_shape_basic sb = ENUM_ATTR(tiletype_shape, basic_shape, s);
    df::tiletype_special sp = ENUM_ATTR(tiletype, special, tt);
    df::tile_occupancy occ = *Maps::getTileOccupancy(t);
    df::tile_building_occ bld = occ.bits.building;
    return mat == tiletype_material::SOIL ||
        mat == tiletype_material::GRASS_LIGHT ||
        mat == tiletype_material::GRASS_DARK ||
        mat == tiletype_material::PLANT ||
        mat == tiletype_material::ROOT ||
        mat == tiletype_material::TREE ||
        mat == tiletype_material::FROZEN_LIQUID ||
        sp == tiletype_special::TRACK ||
        (sp == tiletype_special::SMOOTH && (!engrave || std::find_if(world->engravings.begin(), world->engravings.end(), [t](df::engraving *e) -> bool { return e->pos == t; }) != world->engravings.end())) ||
        s == tiletype_shape::FORTIFICATION ||
        sb == tiletype_shape_basic::Open ||
        sb == tiletype_shape_basic::Stair ||
        (bld != tile_building_occ::None &&
            bld != tile_building_occ::Dynamic);
}

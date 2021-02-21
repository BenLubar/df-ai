#include "ai.h"
#include "plan.h"

#include "modules/Maps.h"

#include "df/feature_init_outdoor_riverst.h"
#include "df/feature_outdoor_riverst.h"
#include "df/map_block.h"
#include "df/plant.h"
#include "df/plant_tree_info.h"
#include "df/plant_tree_tile.h"
#include "df/world.h"

REQUIRE_GLOBAL(world);

df::coord AI::fort_entrance_pos()
{
    room *r = plan.fort_entrance;
    return df::coord((r->min.x + r->max.x) / 2, (r->min.y + r->max.y) / 2, r->max.z);
}

room *AI::find_room(room_type::type type)
{
    if (plan.room_category.empty())
    {
        for (auto r : plan.rooms_and_corridors)
        {
            if (r->type == type)
            {
                return r;
            }
        }
        return nullptr;
    }

    auto cat = plan.room_category.find(type);
    if (cat != plan.room_category.end() && !cat->second.empty())
    {
        return cat->second.front();
    }

    return nullptr;
}

room *AI::find_room(room_type::type type, std::function<bool(room *)> b)
{
    if (plan.room_category.empty())
    {
        for (auto r : plan.rooms_and_corridors)
        {
            if (r->type == type && b(r))
            {
                return r;
            }
        }
        return nullptr;
    }

    auto cat = plan.room_category.find(type);
    if (cat != plan.room_category.end())
    {
        for (auto r : cat->second)
        {
            if (b(r))
            {
                return r;
            }
        }
    }

    return nullptr;
}

room *AI::find_room_at(df::coord t)
{
    if (plan.room_by_z.empty())
    {
        for (auto r : plan.rooms_and_corridors)
        {
            if (r->safe_include(t))
            {
                return r;
            }
        }
        return nullptr;
    }

    auto by_z = plan.room_by_z.find(t.z);
    if (by_z == plan.room_by_z.end())
    {
        return nullptr;
    }
    for (auto r : by_z->second)
    {
        if (r->safe_include(t))
        {
            return r;
        }
    }
    return nullptr;
}

df::coord Plan::find_tree_base(df::coord t, df::plant **ptree)
{
    auto find = [t](df::plant *tree) -> bool
    {
        if (tree->pos == t)
        {
            return true;
        }

        if (!tree->tree_info || !tree->tree_info->body)
        {
            return false;
        }

        df::coord s = tree->pos - df::coord(tree->tree_info->dim_x / 2, tree->tree_info->dim_y / 2, 0);
        if (t.x < s.x || t.y < s.y || t.z < s.z ||
            t.x >= s.x + tree->tree_info->dim_x ||
            t.y >= s.y + tree->tree_info->dim_y ||
            t.z >= s.z + tree->tree_info->body_height)
        {
            return false;
        }

        if (!tree->tree_info->body[t.z - s.z])
        {
            return false;
        }
        df::plant_tree_tile tile = tree->tree_info->body[t.z - s.z][(t.x - s.x) + tree->tree_info->dim_x * (t.y - s.y)];
        return tile.whole != 0 && !tile.bits.blocked;
    };

    for (auto tree = world->plants.tree_dry.begin(); tree != world->plants.tree_dry.end(); tree++)
    {
        if (find(*tree))
        {
            if (ptree)
            {
                *ptree = *tree;
            }
            return (*tree)->pos;
        }
    }

    for (auto tree = world->plants.tree_wet.begin(); tree != world->plants.tree_wet.end(); tree++)
    {
        if (find(*tree))
        {
            if (ptree)
            {
                *ptree = *tree;
            }
            return (*tree)->pos;
        }
    }

    df::coord invalid;
    invalid.clear();
    if (ptree)
    {
        *ptree = nullptr;
    }
    return invalid;
}

// same as ruby spiral_search, but search starting with center of each side
df::coord AI::spiral_search(df::coord t, int16_t max, int16_t min, int16_t step, std::function<bool(df::coord)> b)
{
    if (min == 0)
    {
        if (b(t))
            return t;
        min += step;
    }
    const static struct sides
    {
        std::vector<df::coord> vec;
        sides()
        {
            vec.push_back(df::coord(0, 1, 0));
            vec.push_back(df::coord(1, 0, 0));
            vec.push_back(df::coord(0, -1, 0));
            vec.push_back(df::coord(-1, 0, 0));
        };
    } sides;
    for (int16_t r = min; r <= max; r += step)
    {
        for (auto it = sides.vec.begin(); it != sides.vec.end(); it++)
        {
            df::coord tt = t + *it * r;
            if (Maps::isValidTilePos(tt.x, tt.y, tt.z) && b(tt))
                return tt;
        }

        for (size_t s = 0; s < sides.vec.size(); s++)
        {
            df::coord dr = sides.vec[(s + sides.vec.size() - 1) % sides.vec.size()];
            df::coord dv = sides.vec[s];

            for (int16_t v = -r; v < r; v += step)
            {
                if (v == 0)
                    continue;

                df::coord tt = t + dr * r + dv * v;
                if (Maps::isValidTilePos(tt.x, tt.y, tt.z) && b(tt))
                    return tt;
            }
        }
    }

    df::coord invalid;
    invalid.clear();
    return invalid;
}

df::coord Plan::surface_tile_at(int16_t tx, int16_t ty, bool allow_trees)
{
    int16_t dx = tx & 0xf;
    int16_t dy = ty & 0xf;

    bool tree = false;
    for (int16_t z = world->map.z_count - 1; z >= 0; z--)
    {
        df::map_block *b = Maps::getTileBlock(tx, ty, z);
        if (!b)
            continue;
        df::tiletype tt = b->tiletype[dx][dy];
        df::tiletype_shape ts = ENUM_ATTR(tiletype, shape, tt);
        df::tiletype_shape_basic tsb = ENUM_ATTR(tiletype_shape, basic_shape, ts);
        if (tsb == tiletype_shape_basic::Open)
            continue;
        df::tiletype_material tm = ENUM_ATTR(tiletype, material, tt);
        if (tm == tiletype_material::POOL || tm == tiletype_material::RIVER)
        {
            df::coord invalid;
            invalid.clear();
            return invalid;
        }
        if (tm == tiletype_material::TREE)
        {
            tree = true;
        }
        else if (tsb == tiletype_shape_basic::Floor || tsb == tiletype_shape_basic::Ramp)
        {
            return df::coord(tx, ty, z);
        }
        else if (tree)
        {
            df::coord c(tx, ty, z + 1);
            if (!allow_trees)
                c.clear();
            return c;
        }
    }
    df::coord invalid;
    invalid.clear();
    return invalid;
}

// returns one tile of an outdoor river (if one exists)
df::coord Plan::scan_river(color_ostream &)
{
    df::feature_init_outdoor_riverst *ifeat = nullptr;
    for (auto f = world->features.map_features.begin(); f != world->features.map_features.end(); f++)
    {
        ifeat = virtual_cast<df::feature_init_outdoor_riverst>(*f);
        if (ifeat)
            break;
    }
    df::coord invalid;
    invalid.clear();
    if (!ifeat)
        return invalid;
    df::feature_outdoor_riverst *feat = ifeat->feature;

    for (size_t i = 0; i < feat->embark_pos.x.size(); i++)
    {
        int16_t x = 48 * (feat->embark_pos.x[i] - world->map.region_x);
        int16_t y = 48 * (feat->embark_pos.y[i] - world->map.region_y);
        if (x < 0 || x >= world->map.x_count ||
            y < 0 || y >= world->map.y_count)
            continue;
        int16_t z1 = feat->min_map_z[i];
        int16_t z2 = feat->max_map_z[i];
        for (int16_t z = z1; z <= z2; z++)
        {
            for (int16_t dx = 0; dx < 48; dx++)
            {
                for (int16_t dy = 0; dy < 48; dy++)
                {
                    df::coord t(x + dx, y + dy, z);
                    if (Maps::getTileDesignation(t)->bits.feature_local)
                    {
                        return t;
                    }
                }
            }
        }
    }

    return invalid;
}

room *Plan::find_typed_corridor(color_ostream & out, corridor_type::type type, df::coord origin)
{
    auto start_room = ai.find_room_at(origin);
    if (start_room)
    {
        if (start_room->type == room_type::corridor && start_room->corridor_type == type)
        {
            return start_room;
        }

        return nullptr;
    }

    // TODO
    out << "[TODO] Failed to find corridor of type '" << type << "' for coordinate (" << origin.x << ", " << origin.y << ", " << origin.z << ")" << std::endl;
    return nullptr;
}

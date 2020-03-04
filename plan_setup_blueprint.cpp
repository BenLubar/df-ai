#include "ai.h"
#include "blueprint.h"
#include "plan_setup.h"
#include "debug.h"

#include "modules/Maps.h"

#include "df/block_square_event_grassst.h"
#include "df/map_block.h"
#include "df/world.h"

#define DBG_ROOM(rb) (rb).type << "/" << (rb).tmpl_name << "/" << (rb).name
#define DBG_COORD(c) "(" << (c).x << ", " << (c).y << ", " << (c).z << ")"
#define DBG_COORD_PLUS(c1, c2) "(" << (c1).x << "+" << (c2).x << ", " << (c1).y << "+" << (c2).y << ", " << (c1).z << "+" << (c2).z << ")"

REQUIRE_GLOBAL(world);

bool PlanSetup::add(const room_blueprint & rb, std::string & error, df::coord VARIABLE_IS_NOT_USED exit_location)
{
    for (auto c : rb.corridor)
    {
        if (no_corridor.count(c))
        {
            error = "room corridor intersects no_corridor tile (" + no_corridor.at(c) + ")";
            return false;
        }
    }
    for (auto c : rb.interior)
    {
        if (no_room.count(c))
        {
            error = "room interior intersects no_room tile (" + no_room.at(c) + ")";
            return false;
        }
    }
    for (auto c : rb.no_corridor)
    {
        if (corridor.count(c))
        {
            error = "room no_corridor intersects corridor tile (" + corridor.at(c) + ")";
            return false;
        }
    }
    for (auto c : rb.no_room)
    {
        if (interior.count(c))
        {
            error = "room no_room intersects interior tile (" + interior.at(c) + ")";
            return false;
        }
    }

    room_base::layoutindex_t layout_start = layout.size();
    room_base::roomindex_t room_start = rooms.size();
    int32_t noblesuite_start = next_noblesuite;
    next_noblesuite += rb.max_noblesuite + 1;

    for (auto f : rb.layout)
    {
        f = new room_base::furniture_t(*f);
        f->shift(layout_start, room_start);
        layout.push_back(f);
    }

    for (auto r : rb.rooms)
    {
        r = new room_base::room_t(*r);
        r->shift(layout_start, room_start);
        if (r->noblesuite != -1)
        {
            r->noblesuite += noblesuite_start;
        }
        rooms.push_back(r);
        for (auto & exit : r->exits)
        {
            if (!no_room.count(r->min + exit.first))
            {
                room_connect[r->min + exit.first] = std::make_pair(rooms.size() - 1, variable_string::context_t::map(r->context, exit.second));
            }
#ifndef DFAI_RELEASE
            else
            {
                df::coord c = r->min + exit.first;
                for (auto t : exit.second)
                {
                    DFAI_DEBUG(blueprint, 5, "Not adding already-blocked " << t.first << " exit on " << r->blueprint << " at (" << c.x << ", " << c.y << ", " << c.z << ")");
                }
            }
#endif
        }
    }

    for (auto c : rb.corridor)
    {
        corridor[c] = rb.type + "/" + rb.tmpl_name + "/" + rb.name;
    }

    for (auto c : rb.interior)
    {
        interior[c] = rb.type + "/" + rb.tmpl_name + "/" + rb.name;
    }

    for (auto c : rb.no_room)
    {
        no_room[c] = rb.type + "/" + rb.tmpl_name + "/" + rb.name;
#ifndef DFAI_RELEASE
        auto old_connect = room_connect.find(c);
        if (old_connect != room_connect.end())
        {
            auto r = rooms.at(old_connect->second.first);
            df::coord rp = r->min;
            for (auto exit : old_connect->second.second)
            {
                if (c != exit_location || exit.first != rb.type)
                {
                    DFAI_DEBUG(blueprint, 5, "Removing blocked " << exit.first << " exit on " << r->blueprint << " at " << DBG_COORD_PLUS(rp, c - rp) << " - blocked by " << DBG_ROOM(rb) << " " << DBG_COORD(c - rb.origin) << ")");
                }
            }
        }
#endif
        room_connect.erase(c);
    }

    for (auto c : rb.no_corridor)
    {
        no_corridor[c] = rb.type + "/" + rb.tmpl_name + "/" + rb.name;
    }

    return true;
}

bool PlanSetup::add(const room_blueprint & rb, room_base::roomindex_t parent, std::string & error, df::coord exit_location)
{
    parent += (~rooms.size()) + 1;

    room_blueprint rb_parent(rb);
    for (auto r : rb_parent.rooms)
    {
        r->accesspath.push_back(parent);
    }

    return add(rb_parent, error, exit_location);
}

bool PlanSetup::build(const blueprints_t & blueprints, const blueprint_plan_template & plan)
{
    std::map<std::string, size_t> counts;
    std::map<std::string, std::map<std::string, size_t>> instance_counts;

    DFAI_DEBUG(blueprint, 1, "Placing starting room...");
    place_rooms(counts, instance_counts, blueprints, plan, &PlanSetup::find_available_blueprints_start, &PlanSetup::try_add_room_start);
    if (rooms.empty())
    {
        DFAI_DEBUG(blueprint, 1, "No rooms placed by initial phase. Cannot continue building.");
        return false;
    }

    DFAI_DEBUG(blueprint, 1, "Placing outdoor rooms...");
    place_rooms(counts, instance_counts, blueprints, plan, &PlanSetup::find_available_blueprints_outdoor, &PlanSetup::try_add_room_outdoor);

    DFAI_DEBUG(blueprint, 1, "Building remainder of fortress...");
    place_rooms(counts, instance_counts, blueprints, plan, &PlanSetup::find_available_blueprints_connect, &PlanSetup::try_add_room_connect);

    if (!have_minimum_requirements(counts, instance_counts, plan))
    {
        DFAI_DEBUG(blueprint, 1, "Cannot place rooms, but minimum requirements were not met.");
        return false;
    }

    DFAI_DEBUG(blueprint, 1, "Removing unused rooms...");
    remove_unused_rooms();

    return true;
}

void PlanSetup::place_rooms(std::map<std::string, size_t> & counts, std::map<std::string, std::map<std::string, size_t>> & instance_counts, const blueprints_t & blueprints, const blueprint_plan_template & plan, PlanSetup::find_fn find, PlanSetup::try_add_fn try_add)
{
    std::vector<const room_blueprint *> available_blueprints;
    for (;;)
    {
        available_blueprints.clear();
        (this->*find)(available_blueprints, counts, instance_counts, blueprints, plan);
        if (available_blueprints.empty())
        {
            DFAI_DEBUG(blueprint, 4, "No available blueprints.");
            break;
        }

        std::shuffle(available_blueprints.begin(), available_blueprints.end(), ai.rng);

        bool stop = false;
        size_t failures = 0;
        while (!stop)
        {
            for (auto rb : available_blueprints)
            {
                if (!(this->*try_add)(*rb, counts, instance_counts, plan))
                {
                    failures++;
                    DFAI_DEBUG(blueprint, 4, "Failed to place room " << DBG_ROOM(*rb) << ". Failure count: " << failures << " of " << plan.max_failures << ".");
                    if (failures >= plan.max_failures)
                    {
                        stop = true;
                        break;
                    }
                }
                else
                {
                    stop = true;
                    break;
                }
            }
        }
        if (failures >= plan.max_failures)
        {
            DFAI_DEBUG(blueprint, 1, "Failed too many times in a row.");
            break;
        }
    }
}

void PlanSetup::clear()
{
    for (auto f : layout)
    {
        delete f;
    }
    layout.clear();

    for (auto r : rooms)
    {
        delete r;
    }
    rooms.clear();

    next_noblesuite = 0;
    room_connect.clear();
    corridor.clear();
    interior.clear();
    no_room.clear();
    no_corridor.clear();
}

void PlanSetup::find_available_blueprints(std::vector<const room_blueprint *> & available_blueprints, const std::map<std::string, size_t> & counts, const std::map<std::string, std::map<std::string, size_t>> & instance_counts, const blueprints_t & blueprints, const blueprint_plan_template & plan, const std::set<std::string> & available_tags_base, const std::function<bool(const room_blueprint &)> & check)
{
    const static std::map<std::string, size_t> no_instance_counts;
    const static std::map<std::string, std::pair<size_t, size_t>> no_instance_limits;

    std::set<std::string> available_tags;
    for (auto tag : available_tags_base)
    {
        available_tags.insert(tag);

        auto it = plan.tags.find(tag);
        if (it != plan.tags.end())
        {
            for (auto & t : it->second)
            {
                available_tags.insert(t);
            }
        }
    }

    for (auto & type : blueprints.blueprints)
    {
        if (!available_tags.count(type.first))
        {
            continue;
        }

        auto type_limit = plan.limits.find(type.first);
        auto type_count_it = counts.find(type.first);
        size_t type_count = type_count_it == counts.end() ? 0 : type_count_it->second;
        if (type_limit != plan.limits.end() && type_count >= type_limit->second.second)
        {
            continue;
        }

        auto instance_limits_it = plan.instance_limits.find(type.first);
        if (type_limit == plan.limits.end() && instance_limits_it == plan.instance_limits.end())
        {
            continue;
        }
        const auto & instance_limits = instance_limits_it == plan.instance_limits.end() ? no_instance_limits : instance_limits_it->second;

        auto type_instance_counts_it = instance_counts.find(type.first);
        const auto & type_instance_counts = type_instance_counts_it == instance_counts.end() ? no_instance_counts : type_instance_counts_it->second;

        for (auto & rb : type.second)
        {
            auto instance_limit = instance_limits.find(rb->name);
            auto instance_count_it = type_instance_counts.find(rb->name);
            size_t instance_count = instance_count_it == type_instance_counts.end() ? 0 : instance_count_it->second;

            if (type_limit == plan.limits.end() && instance_limit == instance_limits.end())
            {
                continue;
            }

            if (instance_limit != instance_limits.end() && instance_count >= instance_limit->second.second)
            {
                continue;
            }

            if (check(*rb))
            {
                available_blueprints.push_back(rb);
            }
        }
    }
}

void PlanSetup::find_available_blueprints_start(std::vector<const room_blueprint *> & available_blueprints, const std::map<std::string, size_t> & counts, const std::map<std::string, std::map<std::string, size_t>> & instance_counts, const blueprints_t & blueprints, const blueprint_plan_template & plan)
{
    std::set<std::string> available_tags;
    available_tags.insert(plan.start);

    find_available_blueprints(available_blueprints, counts, instance_counts, blueprints, plan, available_tags, [](const room_blueprint & rb) -> bool
    {
        for (auto r : rb.rooms)
        {
            if (r->outdoor)
            {
                return true;
            }
        }

        return false;
    });
}

void PlanSetup::find_available_blueprints_outdoor(std::vector<const room_blueprint *> & available_blueprints, const std::map<std::string, size_t> & counts, const std::map<std::string, std::map<std::string, size_t>> & instance_counts, const blueprints_t & blueprints, const blueprint_plan_template & plan)
{
    find_available_blueprints(available_blueprints, counts, instance_counts, blueprints, plan, plan.outdoor, [](const room_blueprint & rb) -> bool
    {
        for (auto r : rb.rooms)
        {
            if (r->outdoor)
            {
                return true;
            }
        }

        return false;
    });
}

void PlanSetup::find_available_blueprints_connect(std::vector<const room_blueprint *> & available_blueprints, const std::map<std::string, size_t> & counts, const std::map<std::string, std::map<std::string, size_t>> & instance_counts, const blueprints_t & blueprints, const blueprint_plan_template & plan)
{
    std::set<std::string> available_tags;
    for (auto & c : room_connect)
    {
        for (auto & tag : c.second.second)
        {
            available_tags.insert(tag.first);
        }
    }

    find_available_blueprints(available_blueprints, counts, instance_counts, blueprints, plan, available_tags, [](const room_blueprint &) -> bool { return true; });
}

bool PlanSetup::can_add_room(const room_blueprint & rb, df::coord pos)
{
    for (auto c : rb.no_room)
    {
        if (!Maps::isValidTilePos(c + pos))
        {
            DFAI_DEBUG(blueprint, 4, "Error placing " << DBG_ROOM(rb) << " at " << DBG_COORD_PLUS(pos, c) << ": out of bounds (0-" << (world->map.x_count - 1) << ", 0-" << (world->map.y_count - 1) << ", 0-" << (world->map.z_count - 1) << ")");
            return false;
        }
    }

    for (auto & r : rb.rooms)
    {
        df::coord min = r->min + pos;
        df::coord max = r->max + pos;

        if (!Maps::isValidTilePos(min + df::coord(-2, -2, -1)) || !Maps::isValidTilePos(max + df::coord(2, 2, 1)))
        {
            DFAI_DEBUG(blueprint, 4, "Error placing " << DBG_ROOM(rb) << " at " << DBG_COORD(pos) << ": out of bounds");
            return false;
        }

        if (r->outdoor)
        {
            for (df::coord t = min; t.x <= max.x; t.x++)
            {
                for (t.y = min.y; t.y <= max.y; t.y++)
                {
                    for (t.z = min.z; t.z <= max.z; t.z++)
                    {
                        df::tiletype tt = *Maps::getTileType(t);
                        if (ENUM_ATTR(tiletype_shape, basic_shape, ENUM_ATTR(tiletype, shape, tt)) == tiletype_shape_basic::Wall && ENUM_ATTR(tiletype, material, tt) != tiletype_material::TREE)
                        {
                            DFAI_DEBUG(blueprint, 4, "Error placing " << DBG_ROOM(rb) << " at " << DBG_COORD(pos) << ": " << DBG_COORD(t) << " is underground");
                            return false;
                        }

                        if (r->require_floor && Plan::surface_tile_at(t.x, t.y, true).z != t.z)
                        {
                            DFAI_DEBUG(blueprint, 4, "Error placing " << DBG_ROOM(rb) << " at " << DBG_COORD(pos) << ": " << DBG_COORD(t) << " is not on the ground");
                            return false;
                        }

                        auto building = Maps::getTileOccupancy(t)->bits.building;
                        if (building != tile_building_occ::None)
                        {
                            DFAI_DEBUG(blueprint, 4, "Error placing " << DBG_ROOM(rb) << " at " << DBG_COORD(pos) << ": " << DBG_COORD(t) << " contains building " << enum_item_key_str(building));
                            return false;
                        }
                    }
                }
            }

            if (r->require_grass > 0)
            {
                int32_t grass_count = 0;

                for (df::coord t = min; t.x <= max.x; t.x++)
                {
                    for (t.y = min.y; t.y <= max.y; t.y++)
                    {
                        for (t.z = min.z; t.z <= max.z; t.z++)
                        {
                            auto & events = Maps::getTileBlock(t)->block_events;
                            for (auto be : events)
                            {
                                df::block_square_event_grassst *grass = virtual_cast<df::block_square_event_grassst>(be);
                                if (grass && grass->amount[t.x & 0xf][t.y & 0xf] > 0)
                                {
                                    grass_count++;
                                    break;
                                }
                            }
                        }
                    }
                }

                if (grass_count < r->require_grass)
                {
                    DFAI_DEBUG(blueprint, 4, "Error placing " << DBG_ROOM(rb) << " at " << DBG_COORD(pos) << ": need at least " << r->require_grass << " square meters of grass, but only have " << grass_count);
                    return false;
                }
            }

            if (r->single_biome)
            {
                df::coord2d base_region(Maps::getTileBiomeRgn(min));
                df::biome_type base_biome = Maps::GetBiomeType(base_region.x, base_region.y);

                for (df::coord t = min; t.x <= max.x; t.x++)
                {
                    for (t.y = min.y; t.y <= max.y; t.y++)
                    {
                        for (t.z = min.z; t.z <= max.z; t.z++)
                        {
                            df::coord2d region(Maps::getTileBiomeRgn(t));
                            df::biome_type biome = region == base_region ? base_biome : Maps::GetBiomeType(region.x, region.y);
                            if (biome != base_biome)
                            {
                                DFAI_DEBUG(blueprint, 4, "Error placing " << DBG_ROOM(rb) << " at " << DBG_COORD(pos) << ": area contains multiple biomes (" << enum_item_key_str(base_biome) << " and " << enum_item_key_str(biome) << ")");
                                return false;
                            }
                        }
                    }
                }
            }
        }
        else
        {
            for (auto fi : r->layout)
            {
                auto f = rb.layout.at(fi);
                if (f->dig != tile_dig_designation::No && f->dig != tile_dig_designation::Default && f->dig != tile_dig_designation::UpStair && f->dig != tile_dig_designation::Ramp)
                {
                    df::coord t = min + f->pos;
                    for (int16_t dx = -1; dx <= 1; dx++)
                    {
                        for (int16_t dy = -1; dy <= 1; dy++)
                        {
                            df::tiletype tt = *Maps::getTileType(t + df::coord(dx, dy, -1));
                            if (ENUM_ATTR(tiletype_shape, basic_shape, ENUM_ATTR(tiletype, shape, tt)) != tiletype_shape_basic::Wall || ENUM_ATTR(tiletype, material, tt) == tiletype_material::TREE)
                            {
                                DFAI_DEBUG(blueprint, 4, "Error placing " << DBG_ROOM(rb) << " at " << DBG_COORD(pos) << ": " << enum_item_key_str(f->dig) << " is directly above a cavern");
                                return false;
                            }
                        }
                    }
                }
            }

            for (df::coord t = min - df::coord(1, 1, 0); t.x <= max.x + 1; t.x++)
            {
                for (t.y = min.y - 1; t.y <= max.y + 1; t.y++)
                {
                    for (t.z = min.z; t.z <= max.z + 1; t.z++)
                    {
                        df::tiletype tt = *Maps::getTileType(t);
                        auto des = *Maps::getTileDesignation(t);
                        if (des.bits.flow_size > 0 || ENUM_ATTR(tiletype, material, tt) == tiletype_material::POOL || ENUM_ATTR(tiletype, material, tt) == tiletype_material::RIVER || ENUM_ATTR(tiletype, material, tt) == tiletype_material::BROOK)
                        {
                            DFAI_DEBUG(blueprint, 4, "Error placing " << DBG_ROOM(rb) << " at " << DBG_COORD(pos) << ": " << DBG_COORD(t) << " has water");
                            return false;
                        }

                        if (t.z == max.z + 1)
                        {
                            continue;
                        }

                        if (ENUM_ATTR(tiletype_shape, basic_shape, ENUM_ATTR(tiletype, shape, tt)) != tiletype_shape_basic::Wall || ENUM_ATTR(tiletype, material, tt) == tiletype_material::TREE)
                        {
                            DFAI_DEBUG(blueprint, 4, "Error placing " << DBG_ROOM(rb) << " at " << DBG_COORD(pos) << ": " << DBG_COORD(t) << " is above ground");
                            return false;
                        }

                        auto building = Maps::getTileOccupancy(t)->bits.building;
                        if (building != tile_building_occ::None)
                        {
                            DFAI_DEBUG(blueprint, 4, "Error placing " << DBG_ROOM(rb) << " at " << DBG_COORD(pos) << ": " << DBG_COORD(t) << " contains building (" << enum_item_key_str(building) << ")");
                            return false;
                        }
                    }
                }
            }
        }

        if (r->type == room_type::farmplot)
        {
            for (df::coord t = min - df::coord(0, 0, 1); t.x <= max.x; t.x++)
            {
                for (t.y = min.y; t.y <= max.y; t.y++)
                {
                    if (ENUM_ATTR(tiletype, material, *Maps::getTileType(t)) == tiletype_material::FROZEN_LIQUID)
                    {
                        DFAI_DEBUG(blueprint, 4, "Error placing " << DBG_ROOM(rb) << " at " << DBG_COORD(pos) << ": " << DBG_COORD(t) << " is ice");
                        return false;
                    }
                }
            }
        }
    }

    return true;
}

bool PlanSetup::try_add_room_start(const room_blueprint & rb, std::map<std::string, size_t> & counts, std::map<std::string, std::map<std::string, size_t>> & instance_counts, const blueprint_plan_template & plan)
{
    int16_t min_x = plan.padding_x.first, max_x = plan.padding_x.second, min_y = plan.padding_y.first, max_y = plan.padding_y.second;
    for (auto c : rb.no_room)
    {
        min_x = std::min(min_x, c.x);
        max_x = std::max(max_x, c.x);
        min_y = std::min(min_y, c.y);
        max_y = std::max(max_y, c.y);
    }

    int16_t x = std::uniform_int_distribution<int16_t>(2 - min_x, world->map.x_count - 3 - max_x)(ai.rng);
    int16_t y = std::uniform_int_distribution<int16_t>(2 - min_y, world->map.y_count - 3 - max_y)(ai.rng);

    return try_add_room_outdoor_shared(rb, counts, instance_counts, plan, x, y);
}

bool PlanSetup::try_add_room_outdoor(const room_blueprint & rb, std::map<std::string, size_t> & counts, std::map<std::string, std::map<std::string, size_t>> & instance_counts, const blueprint_plan_template & plan)
{
    int16_t min_x = 0, max_x = 0, min_y = 0, max_y = 0;
    for (auto c : rb.no_room)
    {
        min_x = std::min(min_x, c.x);
        max_x = std::max(max_x, c.x);
        min_y = std::min(min_y, c.y);
        max_y = std::max(max_y, c.y);
    }

    int16_t x = std::uniform_int_distribution<int16_t>(2 - min_x, world->map.x_count - 3 - max_x)(ai.rng);
    int16_t y = std::uniform_int_distribution<int16_t>(2 - min_y, world->map.y_count - 3 - max_y)(ai.rng);

    return try_add_room_outdoor_shared(rb, counts, instance_counts, plan, x, y);
}

bool PlanSetup::try_add_room_outdoor_shared(const room_blueprint & rb, std::map<std::string, size_t> & counts, std::map<std::string, std::map<std::string, size_t>> & instance_counts, const blueprint_plan_template & plan, int16_t x, int16_t y)
{
    df::coord pos = Plan::surface_tile_at(x, y, true);

    if (!pos.isValid())
    {
        DFAI_DEBUG(blueprint, 4, "Error placing " << DBG_ROOM(rb) << " at (" << x << ", " << y << ", ?): no surface position");
        return false;
    }

    if (can_add_room(rb, pos))
    {
        std::string error;
        if (add(room_blueprint(rb, pos, plan.context), error))
        {
            counts[rb.type]++;
            instance_counts[rb.type][rb.name]++;
            DFAI_DEBUG(blueprint, 3, "Placed " << DBG_ROOM(rb) << " at " << DBG_COORD(pos) << ".");
            LogQuiet(stl_sprintf("Placed %s/%s/%s at (%d, %d, %d)", rb.type.c_str(), rb.tmpl_name.c_str(), rb.name.c_str(), pos.x, pos.y, pos.z));
            return true;
        }

        DFAI_DEBUG(blueprint, 4, "Error placing " << DBG_ROOM(rb) << " at " << DBG_COORD(pos) << ": " << error);
    }

    return false;
}

bool PlanSetup::try_add_room_connect(const room_blueprint & rb, std::map<std::string, size_t> & counts, std::map<std::string, std::map<std::string, size_t>> & instance_counts, const blueprint_plan_template & plan)
{
    std::set<std::string> tags;
    tags.insert(rb.type);
    for (auto & t : plan.tags)
    {
        if (t.second.count(rb.type))
        {
            tags.insert(t.first);
        }
    }

    std::vector<std::tuple<room_base::roomindex_t, df::coord, variable_string::context_t>> connectors;

    for (auto & c : room_connect)
    {
        for (auto & t : c.second.second)
        {
            if (tags.count(t.first))
            {
                connectors.push_back(std::make_tuple(c.second.first, c.first, t.second));
                break;
            }
        }
    }

    auto chosen = connectors.at(std::uniform_int_distribution<size_t>(0, connectors.size() - 1)(ai.rng));

    if (can_add_room(rb, std::get<1>(chosen)))
    {
        std::string error;
        if (add(room_blueprint(rb, std::get<1>(chosen), std::get<2>(chosen)), std::get<0>(chosen), error, std::get<1>(chosen)))
        {
            counts[rb.type]++;
            instance_counts[rb.type][rb.name]++;
            DFAI_DEBUG(blueprint, 3, "Placed " << DBG_ROOM(rb) << " at " << DBG_COORD(std::get<1>(chosen)) << ".");
            LogQuiet(stl_sprintf("Placed %s/%s/%s at (%d, %d, %d)", rb.type.c_str(), rb.tmpl_name.c_str(), rb.name.c_str(), std::get<1>(chosen).x, std::get<1>(chosen).y, std::get<1>(chosen).z));
            return true;
        }

        DFAI_DEBUG(blueprint, 4, "Error placing " << DBG_ROOM(rb) << " at " << DBG_COORD(std::get<1>(chosen)) << ": " << error);
    }

    return false;
}

bool PlanSetup::have_minimum_requirements(std::map<std::string, size_t> & counts, std::map<std::string, std::map<std::string, size_t>> & instance_counts, const blueprint_plan_template & plan)
{
    bool ok = true;

    for (auto & limit : plan.limits)
    {
        auto type = counts.find(limit.first);
        if (type == counts.end())
        {
            if (limit.second.first > 0)
            {
                DFAI_DEBUG(blueprint, 2, "Requirement not met: have 0 " << limit.first << " but want between " << limit.second.first << " and " << limit.second.second << ".");
                Log(stl_sprintf("Requirement not met: have 0 %s but want between %zu and %zu.", limit.first.c_str(), limit.second.first, limit.second.second));
                ok = false;
            }
            else
            {
                DFAI_DEBUG(blueprint, 2, "have 0 " << limit.first << " (want between " << limit.second.first << " and " << limit.second.second << ")");
            }
        }
        else
        {
            if (limit.second.first > type->second)
            {
                DFAI_DEBUG(blueprint, 2, "Requirement not met: have " << type->second << " " << limit.first << " but want between " << limit.second.first << " and " << limit.second.second << ".");
                Log(stl_sprintf("Requirement not met: have %zu %s but want between %zu and %zu.", type->second, limit.first.c_str(), limit.second.first, limit.second.second));
                ok = false;
            }
            else
            {
                DFAI_DEBUG(blueprint, 2, "have " << type->second << " " << limit.first << " (want between " << limit.second.first << " and " << limit.second.second << ")");
            }
        }
    }

    for (auto & type_limits : plan.instance_limits)
    {
        auto type_counts = instance_counts.find(type_limits.first);
        if (type_counts == instance_counts.end())
        {
            for (auto & limit : type_limits.second)
            {
                if (limit.second.first > 0)
                {
                    DFAI_DEBUG(blueprint, 2, "Requirement not met: have 0 " << type_limits.first << "/" << limit.first << " but want between " << limit.second.first << " and " << limit.second.second << ".");
                    Log(stl_sprintf("Requirement not met: have 0 %s/%s but want between %zu and %zu.", type_limits.first.c_str(), limit.first.c_str(), limit.second.first, limit.second.second));
                    ok = false;
                }
                else
                {
                    DFAI_DEBUG(blueprint, 2, "have 0 " << type_limits.first << "/" << limit.first << " (want between " << limit.second.first << " and " << limit.second.second << ")");
                }
            }
        }
        else
        {
            for (auto & limit : type_limits.second)
            {
                auto count = type_counts->second.find(limit.first);
                if (count == type_counts->second.end())
                {
                    if (limit.second.first > 0)
                    {
                        DFAI_DEBUG(blueprint, 2, "Requirement not met: have 0 " << type_limits.first << "/" << limit.first << " but want between " << limit.second.first << " and " << limit.second.second << ".");
                        Log(stl_sprintf("Requirement not met: have 0 %s/%s but want between %zu and %zu.", type_limits.first.c_str(), limit.first.c_str(), limit.second.first, limit.second.second));
                        ok = false;
                    }
                    else
                    {
                        DFAI_DEBUG(blueprint, 2, "have 0 " << type_limits.first << "/" << limit.first << " (want between " << limit.second.first << " and " << limit.second.second << ")");
                    }
                }
                else
                {
                    if (limit.second.first > count->second)
                    {
                        DFAI_DEBUG(blueprint, 2, "Requirement not met: have " << count->second << " " << type_limits.first << "/" << limit.first << " but want between " << limit.second.first << " and " << limit.second.second << ".");
                        Log(stl_sprintf("Requirement not met: have %zu %s/%s but want between %zu and %zu.", count->second, type_limits.first.c_str(), limit.first.c_str(), limit.second.first, limit.second.second));
                        ok = false;
                    }
                    else
                    {
                        DFAI_DEBUG(blueprint, 2, "have " << count->second << " " << type_limits.first << "/" << limit.first << " (want between " << limit.second.first << " and " << limit.second.second << ")");
                    }
                }
            }
        }
    }

    return ok;
}

void PlanSetup::remove_unused_rooms()
{
    bool found_use = true;
    while (found_use)
    {
        found_use = false;

        for (auto r : rooms)
        {
            if (r->remove_if_unused)
                continue;

            for (auto i : r->accesspath)
            {
                auto ar = rooms.at(i);

                if (ar->remove_if_unused)
                {
                    ar->remove_if_unused = false;
                    found_use = true;
                }
            }
        }
    }

    for (auto it = rooms.begin(); it != rooms.end(); it++)
    {
        auto r = *it;
        if (!r->remove_if_unused)
            continue;

        for (auto i : r->layout)
        {
            delete layout.at(i);
            layout.at(i) = nullptr;
        }

        delete r;
        *it = nullptr;
    }
}

#include "ai.h"
#include "blueprint.h"
#include "plan.h"
#include "debug.h"

#include "modules/Maps.h"

#include "df/block_square_event_grassst.h"
#include "df/map_block.h"
#include "df/world.h"

#define DBG_ROOM(rb) (rb).type << "/" << (rb).tmpl_name << "/" << (rb).name
#define DBG_COORD(c) "(" << (c).x << ", " << (c).y << ", " << (c).z << ")"
#define DBG_COORD_PLUS(c1, c2) "(" << (c1).x << "+" << (c2).x << ", " << (c1).y << "+" << (c2).y << ", " << (c1).z << "+" << (c2).z << ")"

REQUIRE_GLOBAL(world);

static std::mt19937 & rng()
{
    extern std::unique_ptr<AI> dwarfAI;
    return dwarfAI->rng;
}

blueprint_plan::blueprint_plan() :
    next_noblesuite(0)
{
}

blueprint_plan::~blueprint_plan()
{
    clear();
}

bool blueprint_plan::build(const blueprints_t & blueprints)
{
    std::vector<const blueprint_plan_template *> templates;
    for (auto & bp : blueprints.plans)
    {
        templates.push_back(bp.second);
    }

    std::shuffle(templates.begin(), templates.end(), rng());

    for (size_t i = 0; i < templates.size(); i++)
    {
        const blueprint_plan_template & plan = *templates.at(i);
        for (size_t retries = 0; retries < plan.max_retries; retries++)
        {
            DFAI_DEBUG(blueprint, 1, "Trying to create a blueprint using plan " << (i + 1) << " of " << templates.size() << ": " << plan.name << " (attempt " << (retries + 1) << " of " << plan.max_retries << ")");

            if (build(blueprints, plan))
            {
                priorities = plan.priorities;
                DFAI_DEBUG(blueprint, 1, "Successfully created a blueprint using plan: " << plan.name);
                return true;
            }

            DFAI_DEBUG(blueprint, 1, "Plan failed. Resetting.");
            clear();
        }
    }

    DFAI_DEBUG(blueprint, 1, "Ran out of plans. Failed to create a blueprint.");
    return false;
}

void blueprint_plan::create(room * & fort_entrance, std::vector<room *> & real_rooms_and_corridors, std::vector<plan_priority_t> & real_priorities) const
{
    std::vector<furniture *> real_layout;

    for (size_t i = 0; i < layout.size(); i++)
    {
        real_layout.push_back(new furniture());
    }
    for (size_t i = 0; i < rooms.size(); i++)
    {
        real_rooms_and_corridors.push_back(new room(room_type::type(), df::coord(), df::coord()));
    }

    for (size_t i = 0; i < layout.size(); i++)
    {
        auto in = layout.at(i);
        auto out = real_layout.at(i);

        out->type = in->type;
        out->construction = in->construction;
        out->dig = in->dig;
        out->pos = in->pos;

        if (in->has_target)
        {
            out->target = real_layout.at(in->target);
        }
        else
        {
            out->target = nullptr;
        }

        out->has_users = in->has_users;
        out->ignore = in->ignore;
        out->makeroom = in->makeroom;
        out->internal = in->internal;

        out->comment = in->comment(in->context);
    }
    for (size_t i = 0; i < rooms.size(); i++)
    {
        auto in = rooms.at(i);
        auto out = real_rooms_and_corridors.at(i);

        out->type = in->type;

        out->corridor_type = in->corridor_type;
        out->farm_type = in->farm_type;
        out->stockpile_type = in->stockpile_type;
        out->nobleroom_type = in->nobleroom_type;
        out->outpost_type = in->outpost_type;
        out->location_type = in->location_type;
        out->cistern_type = in->cistern_type;
        out->workshop_type = in->workshop_type;
        out->furnace_type = in->furnace_type;

        out->raw_type = in->raw_type(in->context);

        out->comment = in->comment(in->context);

        out->min = in->min;
        out->max = in->max;

        for (auto ri : in->accesspath)
        {
            out->accesspath.push_back(real_rooms_and_corridors.at(ri));
        }
        for (auto fi : in->layout)
        {
            out->layout.push_back(real_layout.at(fi));
        }

        out->level = in->level;
        out->noblesuite = in->noblesuite;
        out->queue = in->queue;

        if (in->has_workshop)
        {
            out->workshop = real_rooms_and_corridors.at(in->workshop);
        }
        else
        {
            out->workshop = nullptr;
        }

        out->stock_disable = in->stock_disable;
        out->stock_specific1 = in->stock_specific1;
        out->stock_specific2 = in->stock_specific2;

        out->has_users = in->has_users;
        out->temporary = in->temporary;
        out->outdoor = in->outdoor;

        if (in->outdoor && !in->require_floor)
        {
            for (df::coord t = in->min; t.x <= in->max.x; t.x++)
            {
                for (t.y = in->min.y; t.y <= in->max.y; t.y++)
                {
                    int16_t z = Plan::surface_tile_at(t.x, t.y, true).z;
                    for (t.z = in->min.z; t.z <= in->max.z; t.z++)
                    {
                        bool has_layout = false;
                        for (auto f : out->layout)
                        {
                            if (in->min + f->pos == t)
                            {
                                has_layout = true;
                                break;
                            }
                        }
                        if (!has_layout)
                        {
                            furniture *f = new furniture();
                            if (t.z == z)
                            {
                                f->construction = construction_type::Floor;
                            }
                            else
                            {
                                f->dig = tile_dig_designation::Channel;
                            }
                            f->pos = t - in->min;
                            out->layout.push_back(f);
                        }
                    }
                }
            }
        }
    }

    fort_entrance = real_rooms_and_corridors.at(0);
    std::sort(real_rooms_and_corridors.begin(), real_rooms_and_corridors.end(), [fort_entrance](const room *a, const room *b) -> bool
    {
        if (b == fort_entrance)
        {
            return false;
        }
        if (a == fort_entrance)
        {
            return true;
        }
        df::coord da(fort_entrance->pos() - a->pos());
        df::coord db(fort_entrance->pos() - b->pos());
        df::coord da_abs(std::abs(da.x), std::abs(da.y), std::abs(da.z));
        df::coord db_abs(std::abs(db.x), std::abs(db.y), std::abs(db.z));
        if (da_abs.z != db_abs.z)
        {
            return da_abs.z < db_abs.z;
        }
        if (da.z != db.z)
        {
            return da.z < db.z;
        }
        if (da_abs.x != db_abs.x)
        {
            return da_abs.x < db_abs.x;
        }
        if (da.x != db.x)
        {
            return da.x < db.x;
        }
        if (da_abs.y != db_abs.y)
        {
            return da_abs.y < db_abs.y;
        }
        if (da.y != db.y)
        {
            return da.y < db.y;
        }
        return false;
    });
    real_priorities = priorities;
}

bool blueprint_plan::add(const room_blueprint & rb, std::string & error, df::coord exit_location)
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

bool blueprint_plan::add(const room_blueprint & rb, room_base::roomindex_t parent, std::string & error, df::coord exit_location)
{
    parent += (~rooms.size()) + 1;

    room_blueprint rb_parent(rb);
    for (auto r : rb_parent.rooms)
    {
        r->accesspath.push_back(parent);
    }

    return add(rb_parent, error, exit_location);
}

bool blueprint_plan::build(const blueprints_t & blueprints, const blueprint_plan_template & plan)
{
    std::map<std::string, size_t> counts;
    std::map<std::string, std::map<std::string, size_t>> instance_counts;

    DFAI_DEBUG(blueprint, 1, "Placing starting room...");
    place_rooms(counts, instance_counts, blueprints, plan, &blueprint_plan::find_available_blueprints_start, &blueprint_plan::try_add_room_start);
    if (rooms.empty())
    {
        DFAI_DEBUG(blueprint, 1, "No rooms placed by initial phase. Cannot continue building.");
        return false;
    }

    DFAI_DEBUG(blueprint, 1, "Placing outdoor rooms...");
    place_rooms(counts, instance_counts, blueprints, plan, &blueprint_plan::find_available_blueprints_outdoor, &blueprint_plan::try_add_room_outdoor);

    DFAI_DEBUG(blueprint, 1, "Building remainder of fortress...");
    place_rooms(counts, instance_counts, blueprints, plan, &blueprint_plan::find_available_blueprints_connect, &blueprint_plan::try_add_room_connect);

    if (!plan.have_minimum_requirements(counts, instance_counts))
    {
        DFAI_DEBUG(blueprint, 1, "Cannot place rooms, but minimum requirements were not met.");
        return false;
    }

    return true;
}

void blueprint_plan::place_rooms(std::map<std::string, size_t> & counts, std::map<std::string, std::map<std::string, size_t>> & instance_counts, const blueprints_t & blueprints, const blueprint_plan_template & plan, blueprint_plan::find_fn find, blueprint_plan::try_add_fn try_add)
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

        std::shuffle(available_blueprints.begin(), available_blueprints.end(), rng());

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

void blueprint_plan::clear()
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

void blueprint_plan::find_available_blueprints(std::vector<const room_blueprint *> & available_blueprints, const std::map<std::string, size_t> & counts, const std::map<std::string, std::map<std::string, size_t>> & instance_counts, const blueprints_t & blueprints, const blueprint_plan_template & plan, const std::set<std::string> & available_tags_base, const std::function<bool(const room_blueprint &)> & check)
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

void blueprint_plan::find_available_blueprints_start(std::vector<const room_blueprint *> & available_blueprints, const std::map<std::string, size_t> & counts, const std::map<std::string, std::map<std::string, size_t>> & instance_counts, const blueprints_t & blueprints, const blueprint_plan_template & plan)
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

void blueprint_plan::find_available_blueprints_outdoor(std::vector<const room_blueprint *> & available_blueprints, const std::map<std::string, size_t> & counts, const std::map<std::string, std::map<std::string, size_t>> & instance_counts, const blueprints_t & blueprints, const blueprint_plan_template & plan)
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

void blueprint_plan::find_available_blueprints_connect(std::vector<const room_blueprint *> & available_blueprints, const std::map<std::string, size_t> & counts, const std::map<std::string, std::map<std::string, size_t>> & instance_counts, const blueprints_t & blueprints, const blueprint_plan_template & plan)
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

bool blueprint_plan::can_add_room(const room_blueprint & rb, df::coord pos)
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

bool blueprint_plan::try_add_room_start(const room_blueprint & rb, std::map<std::string, size_t> & counts, std::map<std::string, std::map<std::string, size_t>> & instance_counts, const blueprint_plan_template & plan)
{
    int16_t min_x = plan.padding_x.first, max_x = plan.padding_x.second, min_y = plan.padding_y.first, max_y = plan.padding_y.second;
    for (auto c : rb.no_room)
    {
        min_x = std::min(min_x, c.x);
        max_x = std::max(max_x, c.x);
        min_y = std::min(min_y, c.y);
        max_y = std::max(max_y, c.y);
    }

    int16_t x = std::uniform_int_distribution<int16_t>(2 - min_x, world->map.x_count - 3 - max_x)(rng());
    int16_t y = std::uniform_int_distribution<int16_t>(2 - min_y, world->map.y_count - 3 - max_y)(rng());

    return try_add_room_outdoor_shared(rb, counts, instance_counts, plan, x, y);
}

bool blueprint_plan::try_add_room_outdoor(const room_blueprint & rb, std::map<std::string, size_t> & counts, std::map<std::string, std::map<std::string, size_t>> & instance_counts, const blueprint_plan_template & plan)
{
    int16_t min_x = 0, max_x = 0, min_y = 0, max_y = 0;
    for (auto c : rb.no_room)
    {
        min_x = std::min(min_x, c.x);
        max_x = std::max(max_x, c.x);
        min_y = std::min(min_y, c.y);
        max_y = std::max(max_y, c.y);
    }

    int16_t x = std::uniform_int_distribution<int16_t>(2 - min_x, world->map.x_count - 3 - max_x)(rng());
    int16_t y = std::uniform_int_distribution<int16_t>(2 - min_y, world->map.y_count - 3 - max_y)(rng());

    return try_add_room_outdoor_shared(rb, counts, instance_counts, plan, x, y);
}

bool blueprint_plan::try_add_room_outdoor_shared(const room_blueprint & rb, std::map<std::string, size_t> & counts, std::map<std::string, std::map<std::string, size_t>> & instance_counts, const blueprint_plan_template & plan, int16_t x, int16_t y)
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
            return true;
        }

        DFAI_DEBUG(blueprint, 4, "Error placing " << DBG_ROOM(rb) << " at " << DBG_COORD(pos) << ": " << error);
    }

    return false;
}

bool blueprint_plan::try_add_room_connect(const room_blueprint & rb, std::map<std::string, size_t> & counts, std::map<std::string, std::map<std::string, size_t>> & instance_counts, const blueprint_plan_template & plan)
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

    auto chosen = connectors.at(std::uniform_int_distribution<size_t>(0, connectors.size() - 1)(rng()));

    if (can_add_room(rb, std::get<1>(chosen)))
    {
        std::string error;
        if (add(room_blueprint(rb, std::get<1>(chosen), std::get<2>(chosen)), std::get<0>(chosen), error, std::get<1>(chosen)))
        {
            counts[rb.type]++;
            instance_counts[rb.type][rb.name]++;
            DFAI_DEBUG(blueprint, 3, "Placed " << DBG_ROOM(rb) << " at " << DBG_COORD(std::get<1>(chosen)) << ".");
            return true;
        }

        DFAI_DEBUG(blueprint, 4, "Error placing " << DBG_ROOM(rb) << " at " << DBG_COORD(std::get<1>(chosen)) << ": " << error);
    }

    return false;
}

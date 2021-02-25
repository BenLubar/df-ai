#include "ai.h"
#include "plan.h"
#include "debug.h"
#include "plan_setup.h"

#include "modules/Buildings.h"
#include "modules/Maps.h"
#include "modules/World.h"

#include "df/block_square_event_mineralst.h"
#include "df/building_civzonest.h"
#include "df/feature_init_outdoor_riverst.h"
#include "df/feature_outdoor_riverst.h"
#include "df/map_block.h"
#include "df/tile_designation.h"
#include "df/world.h"
#include "df/world_underground_region.h"

REQUIRE_GLOBAL(cur_year);
REQUIRE_GLOBAL(cur_year_tick);
REQUIRE_GLOBAL(world);

farm_allowed_materials_t::farm_allowed_materials_t()
{
    set.insert(tiletype_material::GRASS_DARK);
    set.insert(tiletype_material::GRASS_LIGHT);
    set.insert(tiletype_material::GRASS_DRY);
    set.insert(tiletype_material::GRASS_DEAD);
    set.insert(tiletype_material::SOIL);
    set.insert(tiletype_material::PLANT);
    set.insert(tiletype_material::TREE);
}

farm_allowed_materials_t farm_allowed_materials;

Plan::Plan(AI & ai) :
    ai(ai),
    onupdate_handle(nullptr),
    nrdig(),
    tasks_generic(),
    tasks_furniture(),
    bg_idx_generic(tasks_generic.end()),
    bg_idx_furniture(tasks_furniture.end()),
    rooms_and_corridors(),
    priorities(),
    room_category(),
    room_by_z(),
    cache_nofurnish(),
    fort_entrance(nullptr),
    map_veins(),
    important_workshops(),
    important_workshops2(),
    important_workshops3(),
    m_c_lever_in(nullptr),
    m_c_lever_out(nullptr),
    m_c_cistern(nullptr),
    m_c_reserve(nullptr),
    m_c_testgate_delay(-1),
    checkroom_idx(0),
    trycistern_count(0),
    map_vein_queue(),
    dug_veins(),
    noblesuite(-1),
    cavern_max_level(-1),
    last_idle_year(-1),
    allow_ice(false),
    should_search_for_metal(false),
    past_initial_phase(false),
    cistern_channel_requested(false),
    deconstructed_wagons(false),
    last_update_year(-1),
    last_update_tick(-1)
{
    add_task(task_type::check_rooms);

    important_workshops.push_back(workshop_type::Butchers);
    important_workshops.push_back(workshop_type::Quern);
    important_workshops.push_back(workshop_type::Farmers);
    important_workshops.push_back(workshop_type::Mechanics);
    important_workshops.push_back(workshop_type::Still);

    important_workshops2.push_back(furnace_type::Smelter);
    important_workshops2.push_back(furnace_type::WoodFurnace);

    important_workshops3.push_back(workshop_type::Loom);
    important_workshops3.push_back(workshop_type::Craftsdwarfs);
    important_workshops3.push_back(workshop_type::Tanners);
    important_workshops3.push_back(workshop_type::Kitchen);

    categorize_all();
}

Plan::~Plan()
{
    for (auto it = tasks_generic.begin(); it != tasks_generic.end(); it++)
    {
        delete *it;
    }
    for (auto it = tasks_furniture.begin(); it != tasks_furniture.end(); it++)
    {
        delete *it;
    }
    for (auto it = rooms_and_corridors.begin(); it != rooms_and_corridors.end(); it++)
    {
        delete *it;
    }
}

command_result Plan::startup(color_ostream & out)
{
    if (Core::getInstance().isMapLoaded())
    {
        std::ifstream active_persist("data/save/current/df-ai-plan.dat");
        if (active_persist.good())
        {
            load(active_persist);
            return CR_OK;
        }
    }

    std::ifstream persist(("data/save/" + World::ReadWorldFolder() + "/df-ai-plan.dat").c_str());
    if (persist.good())
    {
        load(persist);
        return CR_OK;
    }

    return setup_blueprint(out);
}

command_result Plan::onupdate_register(color_ostream &)
{
    onupdate_handle = events.onupdate_register("df-ai plan", 240, 20, [this](color_ostream & out) { update(out); });
    return CR_OK;
}

command_result Plan::onupdate_unregister(color_ostream &)
{
    events.onupdate_unregister(onupdate_handle);
    return CR_OK;
}

uint16_t Maps::getTileWalkable(df::coord t)
{
    DFAI_ASSERT_VALID_TILE(t, "");
    df::map_block *b = getTileBlock(t);
    if (BOOST_LIKELY(b != nullptr))
        return b->walkable[t.x & 0xf][t.y & 0xf];
    return 0;
}

void AI::dig_tile(df::coord t, df::tile_dig_designation dig)
{
    DFAI_ASSERT_VALID_TILE(t, " (designation: " << enum_item_key(dig) << ")");
    if (BOOST_UNLIKELY(ENUM_ATTR(tiletype, material, *Maps::getTileType(t)) == tiletype_material::TREE && dig != tile_dig_designation::No))
    {
        dig = tile_dig_designation::Default;
        t = Plan::find_tree_base(t);
    }

    df::tile_designation *des = Maps::getTileDesignation(t);
    if (dig != tile_dig_designation::No && des->bits.dig == tile_dig_designation::No)
    {
        for (auto job : world->jobs.list)
        {
            if ((ENUM_ATTR(job_type, type, job->job_type) == job_type_class::Digging || ENUM_ATTR(job_type, type, job->job_type) == job_type_class::Gathering) && job->pos == t)
            {
                // someone already enroute to dig here, avoid 'Inappropriate
                // dig square' spam
                return;
            }
        }
    }

    des->bits.dig = dig;
    Maps::getTileOccupancy(t)->bits.dig_marked = 0;
    if (dig != tile_dig_designation::No)
    {
        Maps::getTileBlock(t)->flags.bits.designated = 1;
    }
}

// marks all items in room and its access for dumping
// return true if any item is found
bool Plan::dump_items_access(color_ostream & out, room *r)
{
    bool found = false;
    room_items(out, r, [&found](df::item *i)
    {
        found = true;
        i->flags.bits.dump = 1;
    });
    for (auto it : r->accesspath)
    {
        found = dump_items_access(out, it) || found;
    }
    return found;
}

// yield every on_ground items in the room
void Plan::room_items(color_ostream &, room *r, std::function<void(df::item *)> f)
{
    for (int16_t z = r->min.z; z <= r->max.z; z++)
    {
        for (int16_t x = r->min.x & -16; x <= r->max.x; x += 16)
        {
            for (int16_t y = r->min.y & -16; y <= r->max.y; y += 16)
            {
                auto & items = Maps::getTileBlock(x, y, z)->items;
                for (auto id : items)
                {
                    auto i = df::item::find(id);
                    if (BOOST_LIKELY(i && i->flags.bits.on_ground &&
                        r->min.x <= i->pos.x && i->pos.x <= r->max.x &&
                        r->min.y <= i->pos.y && i->pos.y <= r->max.y &&
                        z == i->pos.z))
                    {
                        f(i);
                    }
                }
            }
        }
    }
}

command_result Plan::make_map_walkable(color_ostream &)
{
    events.onupdate_register_once("df-ai plan make_map_walkable", [this](color_ostream & out) -> bool
    {
        if (!Core::getInstance().isMapLoaded())
        {
            return true;
        }

        if (ai.find_room(room_type::corridor, [](room *r) -> bool { return r->corridor_type == corridor_type::walkable && r->status == room_status::dig; }))
        {
            return false;
        }

        // if we don't have a river, we're fine
        df::coord river = scan_river(out);
        if (!river.isValid())
            return true;
        df::coord surface = surface_tile_at(river.x, river.y);
        if (surface.isValid() && ENUM_ATTR(tiletype, material, *Maps::getTileType(surface)) == tiletype_material::BROOK)
            return true;

        Plan *plan = this;

        int16_t fort_miny = 0x7fff;
        int16_t fort_maxy = -1;
        for (room *r : rooms_and_corridors)
        {
            if (!r->outdoor && (r->type != room_type::corridor || r->corridor_type == corridor_type::corridor))
            {
                fort_miny = std::min(fort_miny, r->min.y);
                fort_maxy = std::max(fort_maxy, r->max.y);
            }
        }

        river = AI::spiral_search(river, [plan, fort_miny, fort_maxy](df::coord t) -> bool
        {
            // TODO rooms outline
            if (t.y >= fort_miny && t.y <= fort_maxy)
                return false;
            df::tile_designation *td = Maps::getTileDesignation(t);
            return td && td->bits.feature_local;
        });
        if (!river.isValid())
            return true;

        // find a safe place for the first tile
        df::coord t1 = AI::spiral_search(river, [plan](df::coord t) -> bool
        {
            if (!plan->map_tile_in_rock(t))
                return false;
            df::coord st = plan->surface_tile_at(t.x, t.y);
            return st.isValid() && plan->map_tile_in_rock(st + df::coord(0, 0, -1));
        });
        if (!t1.isValid())
            return true;
        t1 = surface_tile_at(t1.x, t1.y);

        // if the game hasn't done any pathfinding yet, wait until the next frame and try again
        uint16_t t1w = Maps::getTileWalkable(t1);
        if (t1w == 0)
            return false;

        // find the second tile
        df::coord t2 = AI::spiral_search(t1, [t1w](df::coord t) -> bool
        {
            uint16_t tw = Maps::getTileWalkable(t);
            return tw != 0 && tw != t1w;
        });
        if (!t2.isValid())
            return true;
        uint16_t t2w = Maps::getTileWalkable(t2);

        // make sure the second tile is in a safe place
        t2 = AI::spiral_search(t2, [plan, t2w](df::coord t) -> bool
        {
            return Maps::getTileWalkable(t) == t2w && plan->map_tile_in_rock(t - df::coord(0, 0, 1));
        });
        if (!t2.isValid())
            return true;

        // find the bottom of the staircases
        int16_t z = -1;
        for (auto f = world->features.map_features.begin(); f != world->features.map_features.end(); f++)
        {
            df::feature_init_outdoor_riverst *r = virtual_cast<df::feature_init_outdoor_riverst>(*f);
            if (r)
            {
                for (auto zz : r->feature->min_map_z)
                {
                    if (zz > 0 && (z == -1 || z > zz))
                    {
                        z = zz;
                    }
                }
                break;
            }
        }

        // make the corridors
        std::vector<room *> cor;
        cor.push_back(new room(corridor_type::walkable, t1, df::coord(t1, z)));
        cor.push_back(new room(corridor_type::walkable, t2, df::coord(t2, z)));

        int16_t dx = t1.x - t2.x;
        if (-1 > dx)
            cor.push_back(new room(corridor_type::walkable, df::coord(t1.x + 1, t1.y, z), df::coord(t2.x - 1, t1.y, z)));
        else if (dx > 1)
            cor.push_back(new room(corridor_type::walkable, df::coord(t1.x - 1, t1.y, z), df::coord(t2.x + 1, t1.y, z)));

        int16_t dy = t1.y - t2.y;
        if (-1 > dy)
            cor.push_back(new room(corridor_type::walkable, df::coord(t2.x, t1.y + 1, z), df::coord(t2.x, t2.y - 1, z)));
        else if (dy > 1)
            cor.push_back(new room(corridor_type::walkable, df::coord(t2.x, t1.y - 1, z), df::coord(t2.x, t2.y + 1, z)));

        if ((-1 > dx || dx > 1) && (-1 > dy || dy > 1))
            cor.push_back(new room(corridor_type::walkable, df::coord(t2.x, t1.y, z), df::coord(t2.x, t1.y, z)));

        for (auto c = cor.begin(); c != cor.end(); c++)
        {
            rooms_and_corridors.push_back(*c);
            digroom(out, *c); // skip the queue
        }
        categorize_all();
        return true;
    });
    return CR_OK;
}

static int32_t count_tile_bitmask(const df::tile_bitmask & mask)
{
    static int8_t bits_set[] = {0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4};

    int32_t count = 0;
    for (int i = 0; i < df::tile_bitmask::SIZE; i++)
    {
        count += bits_set[(mask.bits[i] >> 0) & 0xf];
        count += bits_set[(mask.bits[i] >> 4) & 0xf];
        count += bits_set[(mask.bits[i] >> 8) & 0xf];
        count += bits_set[(mask.bits[i] >> 12) & 0xf];
    }

    return count;
}

// scan the map, list all map veins in @map_veins (mat_index => [block coords],
// sorted by z)
command_result Plan::list_map_veins(color_ostream &)
{
    map_veins.clear();
    for (int16_t z = 0; z < world->map.z_count; z++)
    {
        for (int16_t xb = 0; xb < world->map.x_count_block; xb++)
        {
            for (int16_t yb = 0; yb < world->map.y_count_block; yb++)
            {
                df::map_block *block = world->map.block_index[xb][yb][z];
                if (!block)
                {
                    continue;
                }
                for (auto event : block->block_events)
                {
                    if (auto vein = virtual_cast<df::block_square_event_mineralst>(event))
                    {
                        map_veins[vein->inorganic_mat][block->map_pos] = count_tile_bitmask(vein->tile_bitmask);
                    }
                }
            }
        }
    }
    return CR_OK;
}

static int32_t mat_index_vein(const df::map_block *block, df::coord t)
{
    for (auto event : block->block_events)
    {
        if (auto mineral = virtual_cast<df::block_square_event_mineralst>(event))
        {
            if (mineral->getassignment(t.x & 0xf, t.y & 0xf))
            {
                return mineral->inorganic_mat;
            }
        }
    }

    return -1;
}

int32_t Plan::can_dig_vein(int32_t mat)
{
    int32_t count = 0;

    if (map_vein_queue.count(mat))
    {
        for (auto queued : map_vein_queue.at(mat))
        {
            df::map_block *block = Maps::getTileBlock(queued.first);
            df::tiletype tt = block->tiletype[queued.first.x & 0xf][queued.first.y & 0xf];
            if (ENUM_ATTR(tiletype_shape, basic_shape, ENUM_ATTR(tiletype, shape, tt)) == tiletype_shape_basic::Wall &&
                ENUM_ATTR(tiletype, material, tt) == tiletype_material::MINERAL &&
                mat_index_vein(block, queued.first) == mat)
            {
                count++;
            }
        }
    }

    if (map_veins.count(mat))
    {
        for (auto vein : map_veins.at(mat))
        {
            count += vein.second;
        }
    }

    return count / 4;
}

// mark a vein of a mat for digging, return expected boulder count
int32_t Plan::dig_vein(color_ostream & out, int32_t mat, int32_t want_boulders)
{
    // mat => [x, y, z, dig_mode] marked for to dig
    int32_t count = 0;
    // check previously queued veins
    if (map_vein_queue.count(mat))
    {
        auto & q = map_vein_queue.at(mat);
        q.erase(std::remove_if(q.begin(), q.end(), [this, mat, &count, &out](std::pair<df::coord, df::tile_dig_designation> d) -> bool
        {
            df::map_block *block = Maps::getTileBlock(d.first);
            df::tiletype tt = block->tiletype[d.first.x & 0xf][d.first.y & 0xf];
            df::tiletype_shape_basic sb = ENUM_ATTR(tiletype_shape, basic_shape, ENUM_ATTR(tiletype, shape, tt));
            if (sb == tiletype_shape_basic::Open)
            {
                df::construction_type ctype = construction_type::Floor;
                if (d.second == tile_dig_designation::Default)
                {
                    ctype = construction_type::Floor;
                }
                else if (!find_enum_item(&ctype, ENUM_KEY_STR(tile_dig_designation, d.second)))
                {
                    ai.debug(out, "[ERROR] could not find construction_type::" + ENUM_KEY_STR(tile_dig_designation, d.second));
                    return false;
                }

                std::ofstream discard;
                return try_furnish_construction(out, ctype, d.first, discard);
            }

            if (sb != tiletype_shape_basic::Wall)
            {
                return true;
            }

            if (block->designation[d.first.x & 0xf][d.first.y & 0xf].bits.dig == tile_dig_designation::No) // warm/wet tile
            {
                AI::dig_tile(d.first, d.second);
            }

            if (ENUM_ATTR(tiletype, material, tt) == tiletype_material::MINERAL && mat_index_vein(block, d.first) == mat)
            {
                count++;
            }

            return false;
        }), q.end());

        if (q.empty())
        {
            map_vein_queue.erase(mat);
        }
    }

    if (!map_veins.count(mat))
    {
        return count / 4;
    }

    // queue new vein
    // delete it from map_veins
    // discard tiles that would dig into a plan room/corridor, or into a cavern
    // (hidden + !wall)
    // try to find a vein close to one we already dug
    for (size_t i = 0; i < 16; i++)
    {
        if (count / 4 >= want_boulders)
        {
            break;
        }

        auto & veins = map_veins.at(mat);
        auto it = std::find_if(veins.begin(), veins.end(), [this](std::pair<df::coord, int32_t> c) -> bool
        {
            for (int16_t vx = -1; vx <= 1; vx++)
            {
                for (int16_t vy = -1; vy <= 1; vy++)
                {
                    if (dug_veins.count(c.first + df::coord(16 * vx, 16 * vy, 0)))
                    {
                        return true;
                    }
                }
            }
            return false;
        });
        if (it == veins.end())
        {
            it--;
        }

        auto v = *it;
        map_veins.at(mat).erase(it);

        int32_t cnt = do_dig_vein(out, mat, v.first);
        if (cnt > 0)
        {
            dug_veins.insert(v.first);
        }

        count += cnt;

        if (map_veins.at(mat).empty())
        {
            map_veins.erase(mat);
            break;
        }
    }

    return count / 4;
}

int32_t Plan::do_dig_vein(color_ostream & out, int32_t mat, df::coord b)
{
    ai.debug(out, "dig_vein " + world->raws.inorganics[mat]->id);

    int32_t count = 0;
    int16_t fort_miny = 0x7fff;
    int16_t fort_maxy = -1;
    int16_t fort_minz = 0x7fff;
    for (room *r : rooms_and_corridors)
    {
        if (!r->outdoor && (r->type != room_type::corridor || r->corridor_type == corridor_type::corridor))
        {
            fort_miny = std::min(fort_miny, r->min.y);
            fort_maxy = std::max(fort_maxy, r->max.y);
            fort_minz = std::min(fort_minz, r->min.z);
        }
    }

    std::set<df::coord> disallow_tile;

    if (b.z >= fort_minz && b.y > fort_miny && b.y < fort_maxy)
    {
        for (uint16_t dx = 0; dx < 16; dx++)
        {
            for (uint16_t dy = 0; dy < 16; dy++)
            {
                df::coord c = b + df::coord(dx, dy, 0);
                if (ai.find_room_at(c))
                {
                    disallow_tile.insert(c);
                }
            }
        }
    }

    auto & q = map_vein_queue[mat];

    // dig whole block
    // TODO have the dwarves search for the vein
    // TODO mine in (visible?) chunks
    DFAI_ASSERT_VALID_TILE(b, " (base position for dig_vein(" << world->raws.inorganics[mat]->id << ")");
    df::map_block *block = Maps::getTileBlock(b);
    int16_t minx = 16, maxx = -1, miny = 16, maxy = -1;
    for (int16_t dx = 0; dx < 16; dx++)
    {
        if (b.x + dx == 0 || b.x + dx >= world->map.x_count - 1)
            continue;

        for (int16_t dy = 0; dy < 16; dy++)
        {
            if (b.y + dy == 0 || b.y + dy >= world->map.y_count - 1)
                continue;

            df::coord t = b + df::coord(dx, dy, 0);
            if (ENUM_ATTR(tiletype, material, block->tiletype[dx][dy]) == tiletype_material::MINERAL && mat_index_vein(block, t) == mat)
            {
                minx = std::min(minx, dx);
                maxx = std::max(maxx, dx);
                miny = std::min(miny, dy);
                maxy = std::max(maxy, dy);
            }
        }
    }

    if (minx > maxx)
        return count;

    bool need_shaft = true;
    std::vector<std::pair<df::coord, df::tile_dig_designation>> todo;
    for (int16_t dx = minx; dx <= maxx; dx++)
    {
        for (int16_t dy = miny; dy <= maxy; dy++)
        {
            df::coord t = b + df::coord(dx, dy, 0);
            if (!Maps::isValidTilePos(t) || disallow_tile.count(t))
            {
                continue;
            }

            if (Maps::getTileDesignation(t)->bits.dig == tile_dig_designation::No)
            {
                bool ok = true;
                bool ns = need_shaft;
                for (int16_t ddx = -1; ddx <= 1; ddx++)
                {
                    for (int16_t ddy = -1; ddy <= 1; ddy++)
                    {
                        df::coord tt = t + df::coord(ddx, ddy, 0);
                        if (ENUM_ATTR(tiletype_shape, basic_shape, ENUM_ATTR(tiletype, shape, *Maps::getTileType(tt))) != tiletype_shape_basic::Wall)
                        {
                            if (Maps::getTileDesignation(tt)->bits.hidden)
                            {
                                ok = false;
                            }
                            else
                            {
                                ns = false;
                            }
                        }
                        else if (Maps::getTileDesignation(tt)->bits.dig != tile_dig_designation::No)
                        {
                            ns = false;
                        }
                    }
                }

                if (ok)
                {
                    todo.push_back(std::make_pair(t, tile_dig_designation::Default));
                    if (ENUM_ATTR(tiletype, material, *Maps::getTileType(t)) == tiletype_material::MINERAL && mat_index_vein(block, t) == mat)
                    {
                        count++;
                    }
                    need_shaft = ns;
                }
            }
        }
    }
    for (auto d = todo.begin(); d != todo.end(); d++)
    {
        q.push_back(*d);
        AI::dig_tile(d->first, d->second);
    }

    if (need_shaft)
    {
        df::coord t0{ uint16_t(b.x + (minx + maxx) / 2), uint16_t(b.y + (miny + maxy) / 2), uint16_t(b.z) };
        room* r = find_typed_corridor(out, corridor_type::veinshaft, t0);
        if (r)
        {
            categorize_all();
        }
    }

    return count;
}

// check that tile is surrounded by solid rock/soil walls
bool Plan::map_tile_in_rock(df::coord tile)
{
    for (int16_t dx = -1; dx <= 1; dx++)
    {
        for (int16_t dy = -1; dy <= 1; dy++)
        {
            df::tiletype *tt = Maps::getTileType(tile + df::coord(dx, dy, 0));
            if (!tt)
            {
                return false;
            }

            if (ENUM_ATTR(tiletype_shape, basic_shape, ENUM_ATTR(tiletype, shape, *tt)) != tiletype_shape_basic::Wall)
            {
                return false;
            }

            df::tiletype_material tm = ENUM_ATTR(tiletype, material, *tt);
            if (tm != tiletype_material::STONE && tm != tiletype_material::MINERAL && tm != tiletype_material::SOIL && tm != tiletype_material::ROOT && (!allow_ice || tm != tiletype_material::FROZEN_LIQUID))
            {
                return false;
            }
        }
    }
    return true;
}

// check tile is surrounded by solid walls or visible tile
bool Plan::map_tile_nocavern(df::coord tile)
{
    for (int16_t dx = -1; dx <= 1; dx++)
    {
        for (int16_t dy = -1; dy <= 1; dy++)
        {
            df::coord t = tile + df::coord(dx, dy, 0);
            df::tile_designation *td = Maps::getTileDesignation(t);
            if (!td)
            {
                continue;
            }


            if (!td->bits.hidden)
            {
                if (td->bits.flow_size >= 4)
                    return false;
                if (!allow_ice && ENUM_ATTR(tiletype, material, *Maps::getTileType(t)) == tiletype_material::FROZEN_LIQUID)
                    return false;
                continue;
            }

            df::tiletype tt = *Maps::getTileType(t);

            if (ENUM_ATTR(tiletype_shape, basic_shape, ENUM_ATTR(tiletype, shape, tt)) != tiletype_shape_basic::Wall)
            {
                return false;
            }

            df::tiletype_material tm = ENUM_ATTR(tiletype, material, tt);
            if (tm != tiletype_material::STONE && tm != tiletype_material::MINERAL && tm != tiletype_material::SOIL && tm != tiletype_material::ROOT && (!allow_ice || tm != tiletype_material::FROZEN_LIQUID))
            {
                return false;
            }
        }
    }
    return true;
}

// check tile is a hidden floor
bool Plan::map_tile_cavernfloor(df::coord t)
{
    df::tile_designation *td = Maps::getTileDesignation(t);
    if (!td)
        return false;
    if (!td->bits.hidden)
        return false;
    if (td->bits.flow_size != 0)
        return false;
    df::tiletype tt = *Maps::getTileType(t);
    df::tiletype_material tm = ENUM_ATTR(tiletype, material, tt);
    if (tm != tiletype_material::STONE && tm != tiletype_material::MINERAL && tm != tiletype_material::SOIL && tm != tiletype_material::ROOT && tm != tiletype_material::GRASS_LIGHT && tm != tiletype_material::GRASS_DARK && tm != tiletype_material::PLANT && tm != tiletype_material::SOIL)
        return false;
    return ENUM_ATTR(tiletype_shape, basic_shape, ENUM_ATTR(tiletype, shape, tt)) == tiletype_shape_basic::Floor;
}

bool Plan::map_tile_undiscovered_cavern(df::coord t)
{
    df::tile_designation *td = Maps::getTileDesignation(t);
    if (!td->bits.feature_global)
    {
        return false;
    }

    auto region = df::world_underground_region::find(Maps::getTileBlock(t)->global_feature);
    if (!region || !region->feature_init)
    {
        return false;
    }
    return !region->feature_init->flags.is_set(feature_init_flags::Discovered);
}

std::string Plan::status()
{
    std::map<task_type::type, size_t> task_count;
    std::map<layout_type::type, std::set<df::coord>> furnishing;
    for (auto t : tasks_generic)
    {
        task_count[t->type]++;
    }
    for (auto t : tasks_furniture)
    {
        task_count[t->type]++;
        if (!t->f->type == layout_type::none)
        {
            furnishing[t->f->type].insert(t->r->min + t->f->pos);
        }
    }
    std::ostringstream s;
    bool first = true;
    for (auto tc = task_count.begin(); tc != task_count.end(); tc++)
    {
        if (first)
        {
            first = false;
        }
        else
        {
            s << ", ";
        }
        s << tc->first << ": " << tc->second;
    }
    if (task *t = is_digging())
    {
        if (t->type == task_type::dig_room || t->type == task_type::dig_room_immediate)
        {
            s << ", digging: " << AI::describe_room(t->r);
        }
        else if (t->type == task_type::want_dig)
        {
            s << ", waiting to dig: " << AI::describe_room(t->r);
        }
    }
    first = true;
    for (auto f = furnishing.begin(); f != furnishing.end(); f++)
    {
        if (first)
        {
            first = false;
            s << "\nfurnishing: ";
        }
        else
        {
            s << ", ";
        }
        s << f->first << ": " << f->second.size();
    }
    return s.str();
}

static void report_task(std::ostream & out, bool html, task *t)
{
    if (html)
    {
        out << "<b>" << t->type << "</b>";
    }
    else
    {
        out << "- " << t->type;
    }

    if (t->r != nullptr)
    {
        if (t->type == task_type::want_dig || t->type == task_type::dig_room || t->type == task_type::dig_room_immediate)
        {
            out << " (queue " << t->r->queue << ")";
        }

        if (html)
        {
            out << "<br/>";
        }
        else
        {
            out << "\n  ";
        }
        out << AI::describe_room(t->r, html);
    }

    if (t->f != nullptr)
    {
        if (html)
        {
            out << "<br/>";
        }
        else
        {
            out << "\n  ";
        }
        out << AI::describe_furniture(t->f, html);
    }

    if (!t->last_status.empty())
    {
        if (html)
        {
            out << "<br/><i>" << html_escape(t->last_status) << "</i>";
        }
        else
        {
            out << "\n  " << t->last_status;
        }
    }
}

void Plan::report_helper(std::ostream & out, bool html, const std::string & title, const std::list<task *> & tasks, const std::list<task *>::iterator & bg_idx, const std::set<task *> & seen_tasks)
{
    if (html)
    {
        out << "<h2 id=\"Plan_Tasks_" << title << "\">" << title << "</h2><ul>";
    }
    else
    {
        out << "## Tasks\n### " << title << "\n";
    }
    for (auto it = tasks.begin(); it != tasks.end(); it++)
    {
        if (seen_tasks.count(*it) && bg_idx != it)
        {
            continue;
        }

        if (html)
        {
            out << "<li>";
        }
        if (bg_idx == it)
        {
            if (html)
            {
                out << "<i>--- current position ---</i><br/>";
            }
            else
            {
                out << "--- current position ---\n";
            }
        }

        if (!seen_tasks.count(*it))
        {
            report_task(out, html, *it);
        }

        out << (html ? "</li>" : "\n");
    }
    if (html)
    {
        out << "</ul>";
    }
    if (bg_idx == tasks.end())
    {
        int32_t tick_diff = 240 - ((*cur_year_tick - last_update_tick) + (*cur_year - last_update_year) * 12 * 28 * 24 * 50);
        if (html)
        {
            out << "<p><i>";
        }
        out << "--- next check in " << tick_diff << " ticks ---";
        if (html)
        {
            out << "</i></p>";
        }
        else
        {
            out << "\n";
        }
    }
}

void Plan::report(std::ostream & out, bool html)
{
    std::set<task *> seen_tasks;

    if (html)
    {
        out << "<style>.plan-priority::before{display:inline-block;width:1.5em;text-align:center;padding-right:0.5em}.priority-Future::before{content:'\\1F6D1 '}.priority-Working::before{content:'\\231B '}.priority-Done::before{content:'\\2705 '}</style>";
    }

    out << (html ? "<h2 id=\"Plan_Tasks_Priorities\">Priorities</h2><ul>" : "## Priorities\n");
    for (auto & priority : priorities)
    {
        out << (html ? "<li class=\"plan-priority priority-" : "\n- **");
        if (!priority.checked)
        {
            out << "Future";
        }
        else if (priority.working)
        {
            // if the task is done but the priority checker hasn't run yet, let's mark it as done
            bool any_tasks = false;
            for (auto t : tasks_generic)
            {
                if (priority.match_task(t))
                {
                    any_tasks = true;
                    break;
                }
            }
            if (!any_tasks)
            {
                for (auto t : tasks_furniture)
                {
                    if (priority.match_task(t))
                    {
                        any_tasks = true;
                        break;
                    }
                }
            }
            if (any_tasks)
            {
                out << "Working";
            }
            else
            {
                priority.working = false;
                out << "Done";
            }
        }
        else
        {
            out << "Done";
        }

        bool any = false;
        auto check_task = [&](task* t)
        {
            if (!priority.match_task(t))
            {
                return;
            }

            seen_tasks.insert(t);

            if (!any)
            {
                any = true;

                if (html)
                {
                    out << "<ul>";
                }
            }

            out << (html ? "<li>" : "\n   -");

            report_task(out, html, t);

            if (html)
            {
                out << "</li>";
            }
        };

        if (html)
        {
            out << "\">" << html_escape(priority.name);
        }
        else
        {
            out << "**: " << priority.name;
        }

        if (priority.checked && priority.working)
        {
            for (auto t : tasks_generic)
            {
                check_task(t);
            }
            for (auto t : tasks_furniture)
            {
                check_task(t);
            }
        }

        if (html)
        {
            if (any)
            {
                out << "</ul>";
            }

            out << "</li>";
        }
    }
    out << (html ? "</ul>" : "\n");

    report_helper(out, html, "Generic", tasks_generic, bg_idx_generic, seen_tasks);
    report_helper(out, html, "Furniture", tasks_furniture, bg_idx_furniture, seen_tasks);
}

void Plan::categorize_all()
{
    std::sort(rooms_and_corridors.begin(), rooms_and_corridors.end(), [&](room *a, room *b) -> bool
        {
            if (a->outdoor != b->outdoor)
            {
                return a->outdoor;
            }

            if (a->outdoor)
            {
                auto base_pos = fort_entrance->pos();
                auto a_diff = a->pos() - base_pos;
                auto b_diff = b->pos() - base_pos;

                if (a_diff.x * a_diff.x + a_diff.y * a_diff.y < b_diff.x * b_diff.x + b_diff.y * b_diff.y)
                {
                    return true;
                }
                if (a_diff.x * a_diff.x + a_diff.y * a_diff.y > b_diff.x * b_diff.x + b_diff.y * b_diff.y)
                {
                    return false;
                }
                if (a_diff.z < b_diff.z)
                {
                    return true;
                }
                if (a_diff.z > b_diff.z)
                {
                    return false;
                }
                if (a_diff.x < b_diff.x)
                {
                    return true;
                }
                if (a_diff.x > b_diff.x)
                {
                    return false;
                }
                return a_diff.y < b_diff.y;
            }

            return a->distance_to(fort_entrance) < b->distance_to(fort_entrance);
        });

    room_category.clear();
    room_by_z.clear();
    for (auto r : rooms_and_corridors)
    {
        room_category[r->type].push_back(r);
        for (int32_t z = r->min.z; z <= r->max.z; z++)
        {
            room_by_z[z].insert(r);
        }
        for (auto f : r->layout)
        {
            room_by_z[r->min.z + f->pos.z].insert(r);
        }
    }

    if (room_category.count(room_type::stockpile))
    {
        auto & stockpiles = room_category.at(room_type::stockpile);
        std::sort(stockpiles.begin(), stockpiles.end(), [this](room *a, room *b) -> bool
        {
            if (a->level < b->level)
                return true;
            if (a->level > b->level)
                return false;

            int16_t ax = a->min.x < fort_entrance->min.x ? -a->min.x : a->min.x;
            int16_t bx = b->min.x < fort_entrance->min.x ? -b->min.x : b->min.x;
            if (ax < bx)
                return true;
            if (ax > bx)
                return false;

            return a->min.y < b->min.y;
        });
    }
}

void Plan::fixup_open(color_ostream & out, room *r)
{
    if (r->type == room_type::pasture)
    {
        return;
    }

    for (int16_t x = r->min.x; x <= r->max.x; x++)
    {
        for (int16_t y = r->min.y; y <= r->max.y; y++)
        {
            for (int16_t z = r->min.z; z <= r->max.z; z++)
            {
                df::coord t(x, y, z);
                if (ENUM_ATTR(tiletype, shape, *Maps::getTileType(t)) == tiletype_shape::WALL)
                {
                    continue;
                }

                for (auto it = r->layout.begin(); it != r->layout.end(); it++)
                {
                    furniture *f = *it;
                    df::coord ft = r->min + f->pos;
                    if (t == ft)
                    {
                        if (f->construction == construction_type::NONE)
                        {
                            fixup_open_tile(out, r, ft, f->dig, f);
                        }
                        t.clear();
                        break;
                    }
                }
                if (t.isValid())
                {
                    fixup_open_tile(out, r, t, r->dig_mode(t));
                }
            }
        }
    }
}

void Plan::fixup_open_tile(color_ostream & out, room *r, df::coord t, df::tile_dig_designation d, furniture *f)
{
    df::tiletype *tt = Maps::getTileType(t);
    if (!tt)
        return;

    auto ts = ENUM_ATTR(tiletype, shape, *tt);

    switch (d)
    {
    case tile_dig_designation::Channel:
        // do nothing
        break;
    case tile_dig_designation::No:
        if (ts != tiletype_shape::WALL)
        {
            fixup_open_helper(out, r, t, construction_type::Wall, f, *tt);
        }
        break;
    case tile_dig_designation::Default:
        if (ts == tiletype_shape::BOULDER || ts == tiletype_shape::PEBBLES)
        {
            // do nothing
        }
        else if (ts == tiletype_shape::SHRUB)
        {
            // harvest
            AI::dig_tile(t, tile_dig_designation::Default);
        }
        else if (ts == tiletype_shape::SAPLING && r->type == room_type::pasture)
        {
            // saplings can grow in pastures
        }
        else if (ts != tiletype_shape::FLOOR)
        {
            fixup_open_helper(out, r, t, construction_type::Floor, f, *tt);
        }
        break;
    case tile_dig_designation::UpDownStair:
        if (ts != tiletype_shape::STAIR_UPDOWN)
        {
            fixup_open_helper(out, r, t, construction_type::UpDownStair, f, *tt);
        }
        break;
    case tile_dig_designation::UpStair:
        if (ts != tiletype_shape::STAIR_UP)
        {
            fixup_open_helper(out, r, t, construction_type::UpStair, f, *tt);
        }
        break;
    case tile_dig_designation::Ramp:
        if (ts != tiletype_shape::RAMP)
        {
            fixup_open_helper(out, r, t, construction_type::Ramp, f, *tt);
        }
        break;
    case tile_dig_designation::DownStair:
        if (ts == tiletype_shape::FLOOR)
        {
            AI::dig_tile(t, tile_dig_designation::DownStair);
        }
        else if (ts != tiletype_shape::STAIR_DOWN)
        {
            fixup_open_helper(out, r, t, construction_type::DownStair, f, *tt);
        }
        break;
    }
}

void Plan::fixup_open_helper(color_ostream & out, room *r, df::coord t, df::construction_type c, furniture *f, df::tiletype tt)
{
    if (!f)
    {
        f = new furniture();
        f->pos = t - r->min;
        r->layout.push_back(f);
    }
    if (f->construction != c)
    {
        ai.debug(out, stl_sprintf("plan fixup_open %s %s(%d, %d, %d) %s", AI::describe_room(r).c_str(), ENUM_KEY_STR(construction_type, c).c_str(), f->pos.x, f->pos.y, f->pos.z, ENUM_KEY_STR(tiletype, tt).c_str()));
        add_task(task_type::furnish, r, f);
    }
    f->construction = c;
}

command_result Plan::setup_blueprint(color_ostream &)
{
    events.queue_exclusive(std::make_unique<PlanSetup>(ai));

    return CR_OK;
}

static int16_t setup_outdoor_gathering_zones_counters[3];
static std::map<int16_t, std::set<df::coord2d>> setup_outdoor_gathering_zones_ground;

command_result Plan::setup_outdoor_gathering_zones(color_ostream &)
{
    setup_outdoor_gathering_zones_counters[0] = 0;
    setup_outdoor_gathering_zones_counters[1] = 0;
    setup_outdoor_gathering_zones_counters[2] = 0;
    setup_outdoor_gathering_zones_ground.clear();
    events.onupdate_register_once("df-ai plan setup_outdoor_gathering_zones", 10, [this](color_ostream & out) -> bool
    {
        int16_t & x = setup_outdoor_gathering_zones_counters[0];
        int16_t & y = setup_outdoor_gathering_zones_counters[1];
        int16_t & i = setup_outdoor_gathering_zones_counters[2];
        std::map<int16_t, std::set<df::coord2d>> & ground = setup_outdoor_gathering_zones_ground;
        if (i == 31 || x + i == world->map.x_count)
        {
            for (auto g = ground.begin(); g != ground.end(); g++)
            {
                df::building_civzonest *bld = virtual_cast<df::building_civzonest>(Buildings::allocInstance(df::coord(x, y, g->first), building_type::Civzone, civzone_type::ActivityZone));
                int16_t w = 31;
                int16_t h = 31;
                if (x + 31 > world->map.x_count)
                    w = world->map.x_count % 31;
                if (y + 31 > world->map.y_count)
                    h = world->map.y_count % 31;
                Buildings::setSize(bld, df::coord(w, h, 1));
                delete[] bld->room.extents;
                bld->room.extents = new df::building_extents_type[w * h]();
                bld->room.x = x;
                bld->room.y = y;
                bld->room.width = w;
                bld->room.height = h;
                for (int16_t dx = 0; dx < w; dx++)
                {
                    for (int16_t dy = 0; dy < h; dy++)
                    {
                        bld->room.extents[dx + w * dy] = g->second.count(df::coord2d(dx, dy)) ? building_extents_type::Stockpile : building_extents_type::None;
                    }
                }
                Buildings::constructAbstract(bld);
                bld->is_room = true;

                bld->zone_flags.bits.active = 1;
                bld->zone_flags.bits.gather = 1;
                bld->gather_flags.bits.pick_trees = 1;
                bld->gather_flags.bits.pick_shrubs = 1;
                bld->gather_flags.bits.gather_fallen = 1;
            }

            ground.clear();
            i = 0;
            x += 31;
            if (x >= world->map.x_count)
            {
                x = 0;
                y += 31;
                if (y >= world->map.y_count)
                {
                    ai.debug(out, "plan setup_outdoor_gathering_zones finished");
                    return true;
                }
            }
            return false;
        }

        int16_t tx = x + i;
        for (int16_t ty = y; ty < y + 31 && ty < world->map.y_count; ty++)
        {
            df::coord t = surface_tile_at(tx, ty, true);
            if (!t.isValid())
                continue;
            ground[t.z].insert(df::coord2d(tx % 31, ty % 31));
        }
        i++;
        return false;
    });

    return CR_OK;
}

command_result Plan::setup_blueprint_caverns(color_ostream & out)
{
    df::coord wall;
    wall.clear();
    int16_t & z = cavern_max_level;
    if (z == -1)
    {
        z = world->map.z_count - 1;
    }
    if (z == 0)
    {
        return CR_FAILURE;
    }
    df::coord target;
    for (; !wall.isValid() && z > 0; z--)
    {
        ai.debug(out, stl_sprintf("outpost: searching z-level %d", z));
        for (int16_t x = 0; !wall.isValid() && x < world->map.x_count; x++)
        {
            for (int16_t y = 0; !wall.isValid() && y < world->map.y_count; y++)
            {
                df::coord t(x, y, z);
                if (!map_tile_in_rock(t))
                    continue;
                // find a floor next to the wall
                target = AI::spiral_search(t, 2, 2, [this](df::coord _t) -> bool
                {
                    return map_tile_cavernfloor(_t) && map_tile_undiscovered_cavern(_t);
                });
                if (target.isValid())
                    wall = t;
            }
        }
    }
    if (!wall.isValid())
    {
        ai.debug(out, "outpost: could not find a cavern wall tile");
        return CR_FAILURE;
    }

    room *r = new room(outpost_type::cavern, target, target);

    std::set<room *> corridors;

    if (wall.x != target.x)
    {
        room *cor = new room(corridor_type::outpost, df::coord(wall.x, wall.y, wall.z), df::coord(target.x, wall.y, wall.z));
        rooms_and_corridors.push_back(cor);
        corridors.insert(cor);
        r->accesspath.push_back(cor);
    }

    if (wall.y != target.y)
    {
        room *cor = new room(corridor_type::outpost, df::coord(target.x, wall.y, wall.z), df::coord(target.x, target.y, wall.z));
        rooms_and_corridors.push_back(cor);
        corridors.insert(cor);
        r->accesspath.push_back(cor);
    }

    rooms_and_corridors.push_back(r);
    categorize_all();

    room *up = find_typed_corridor(out, corridor_type::outpost, wall, corridors);
    if (!up)
    {
        return CR_FAILURE;
    }

    r->accesspath.push_back(up);
    categorize_all();

    ai.debug(out, stl_sprintf("outpost: wall (%d, %d, %d)", wall.x, wall.y, wall.z));
    ai.debug(out, stl_sprintf("outpost: target (%d, %d, %d)", target.x, target.y, target.z));
    ai.debug(out, stl_sprintf("outpost: up (%d, %d, %d)", up->max.x, up->max.y, up->max.z));

    return CR_OK;
}

// create a new Corridor from origin to surface, through rock
// may create multiple chunks to avoid obstacles, all parts are added to corridors
// returns an array of Corridors, 1st = origin, last = surface
std::vector<room *> Plan::find_corridor_tosurface(color_ostream & out, corridor_type::type type, df::coord origin)
{
    std::vector<room *> cors;
    for (;;)
    {
        room *cor = new room(type, origin, origin);
        if (!cors.empty())
        {
            cors.back()->accesspath.push_back(cor);
        }
        cors.push_back(cor);

        while (map_tile_in_rock(cor->max) && !ai.map_tile_intersects_room(cor->max + df::coord(0, 0, 1)) && Maps::isValidTilePos(cor->max.x, cor->max.y, cor->max.z + 1))
        {
            cor->max.z++;
        }

        df::tiletype tt = *Maps::getTileType(cor->max);
        df::tiletype_shape_basic sb = ENUM_ATTR(tiletype_shape, basic_shape, ENUM_ATTR(tiletype, shape, tt));
        df::tiletype_material tm = ENUM_ATTR(tiletype, material, tt);
        df::tile_designation td = *Maps::getTileDesignation(cor->max);
        if ((sb == tiletype_shape_basic::Ramp ||
            sb == tiletype_shape_basic::Floor) &&
            tm != tiletype_material::TREE &&
            td.bits.flow_size == 0 &&
            !td.bits.hidden)
        {
            break;
        }

        df::coord out2 = AI::spiral_search(cor->max, [this](df::coord t) -> bool
        {
            while (map_tile_in_rock(t) && !ai.map_tile_intersects_room(t + df::coord(0, 0, 1)))
            {
                t.z++;
            }

            df::tiletype *tt = Maps::getTileType(t);
            if (!tt)
                return false;

            df::tiletype_shape_basic sb = ENUM_ATTR(tiletype_shape, basic_shape, ENUM_ATTR(tiletype, shape, *tt));
            df::tile_designation td = *Maps::getTileDesignation(t);

            return (sb == tiletype_shape_basic::Ramp || sb == tiletype_shape_basic::Floor) &&
                ENUM_ATTR(tiletype, material, *tt) != tiletype_material::TREE &&
                td.bits.flow_size == 0 &&
                !td.bits.hidden &&
                !ai.map_tile_intersects_room(t);
        });

        if (!out2.isValid())
        {
            ai.debug(out, stl_sprintf("[ERROR] could not find corridor to surface (%d, %d, %d)", cor->max.x, cor->max.y, cor->max.z));
            break;
        }

        if (Maps::getTileDesignation(cor->max)->bits.flow_size > 0)
        {
            // damp stone located
            cor->max.z--;
            out2.z--;
        }
        cor->max.z--;
        out2.z--;
        room *cor2 = cor;

        if (cor2->max.x != out2.x)
        {
            cor = new room(type, df::coord(out2.x, out2.y, out2.z), df::coord(cor2->max.x, out2.y, out2.z));
            cors.back()->accesspath.push_back(cor);
            cors.push_back(cor);
        }

        if (cor2->max.y != out2.y)
        {
            cor = new room(type, df::coord(cor2->max.x, out2.y, out2.z), df::coord(cor2->max.x, cor2->max.y, out2.z));
            cors.back()->accesspath.push_back(cor);
            cors.push_back(cor);
        }

        if (origin == out2)
        {
            ai.debug(out, stl_sprintf("[ERROR] find_corridor_tosurface: loop: %d, %d, %d", origin.x, origin.y, origin.z));
            break;
        }
        ai.debug(out, stl_sprintf("find_corridor_tosurface: %d, %d, %d -> %d, %d, %d", origin.x, origin.y, origin.z, out2.x, out2.y, out2.z));

        origin = out2;
    }
    rooms_and_corridors.insert(rooms_and_corridors.end(), cors.begin(), cors.end());
    return cors;
}

// XXX
bool Plan::corridor_include_hack(const room *r, df::coord t1, df::coord t2)
{
    return ai.find_room(room_type::corridor, [r, t1, t2](room *c) -> bool
    {
        if (!c->include(t1) || !c->include(t2))
        {
            return false;
        }

        if (c->min.z != c->max.z)
        {
            return true;
        }
        for (auto a : c->accesspath)
        {
            if (a == r)
            {
                return true;
            }
        }
        return false;
    }) != nullptr;
}

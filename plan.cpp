#include "ai.h"
#include "camera.h"
#include "plan.h"
#include "stocks.h"

#include <tuple>

#include "modules/Buildings.h"
#include "modules/Gui.h"
#include "modules/Items.h"
#include "modules/Job.h"
#include "modules/Maps.h"
#include "modules/Materials.h"
#include "modules/Screen.h"
#include "modules/Translation.h"
#include "modules/Units.h"

#include "df/building_archerytargetst.h"
#include "df/building_civzonest.h"
#include "df/building_coffinst.h"
#include "df/building_def.h"
#include "df/building_doorst.h"
#include "df/building_floodgatest.h"
#include "df/building_squad_use.h"
#include "df/building_tablest.h"
#include "df/building_trapst.h"
#include "df/building_wagonst.h"
#include "df/buildings_other_id.h"
#include "df/builtin_mats.h"
#include "df/caste_raw.h"
#include "df/creature_raw.h"
#include "df/entity_position.h"
#include "df/feature_init_outdoor_riverst.h"
#include "df/feature_outdoor_riverst.h"
#include "df/furniture_type.h"
#include "df/general_ref_building_holderst.h"
#include "df/general_ref_building_triggertargetst.h"
#include "df/item_boulderst.h"
#include "df/item_toolst.h"
#include "df/itemdef_toolst.h"
#include "df/items_other_id.h"
#include "df/job.h"
#include "df/organic_mat_category.h"
#include "df/plant.h"
#include "df/plant_tree_info.h"
#include "df/plant_tree_tile.h"
#include "df/squad.h"
#include "df/stop_depart_condition.h"
#include "df/trap_type.h"
#include "df/ui.h"
#include "df/unit.h"
#include "df/vehicle.h"
#include "df/viewscreen_layer_stockpilest.h"
#include "df/world.h"

REQUIRE_GLOBAL(cur_year);
REQUIRE_GLOBAL(cursor);
REQUIRE_GLOBAL(ui);
REQUIRE_GLOBAL(world);

const static int32_t manager_taskmax = 4; // when stacking manager jobs, do not stack more than this
const static int32_t manager_maxbacklog = 10; // add new masonly if more that this much mason manager orders
const static int32_t dwarves_per_table = 3; // number of dwarves per dininghall table/chair
const static int32_t dwarves_per_farmtile_num = 3; // number of dwarves per farmplot tile
const static int32_t dwarves_per_farmtile_den = 2;
const static int32_t wantdig_max = 2; // dig at most this much wantdig rooms at a time
const static int32_t spare_bedroom = 3; // dig this much free bedroom in advance when idle

const static int16_t farm_w = 3;
const static int16_t farm_h = 3;
const static int32_t dpf = farm_w * farm_h * dwarves_per_farmtile_num / dwarves_per_farmtile_den;
const static int32_t extra_farms = 7; // built after utilities are finished
const static int32_t nrfarms = (220 + dpf - 1) / dpf + extra_farms;

const int16_t Plan::MinX = -48, Plan::MinY = -22, Plan::MinZ = -5;
const int16_t Plan::MaxX = 48, Plan::MaxY = 22, Plan::MaxZ = 1;

Plan::Plan(AI *ai) :
    ai(ai),
    onupdate_handle(nullptr),
    nrdig(0),
    tasks(),
    bg_idx(tasks.end()),
    rooms(),
    room_category(),
    corridors(),
    cache_nofurnish(),
    fort_entrance(nullptr),
    map_veins(),
    important_workshops(),
    important_workshops2(),
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
    past_initial_phase(false),
    cistern_channel_requested(false)
{
    tasks.push_back(new task("checkrooms"));

    important_workshops.push_back("Butchers");
    important_workshops.push_back("Quern");
    important_workshops.push_back("Farmers");
    important_workshops.push_back("Mechanics");
    important_workshops.push_back("Still");

    important_workshops2.push_back("Smelter");
    important_workshops2.push_back("WoodFurnace");
    important_workshops2.push_back("Loom");
    important_workshops2.push_back("Craftsdwarfs");
    important_workshops2.push_back("Tanners");
    important_workshops2.push_back("Kitchen");

    categorize_all();
}

Plan::~Plan()
{
    for (auto it = tasks.begin(); it != tasks.end(); it++)
    {
        delete *it;
    }
    for (auto it = rooms.begin(); it != rooms.end(); it++)
    {
        delete *it;
    }
    for (auto it = corridors.begin(); it != corridors.end(); it++)
    {
        delete *it;
    }
}

static bool find_item(df::items_other_id idx, df::item *&item, bool fire_safe = false, bool non_economic = false)
{
    for (auto it = world->items.other[idx].begin(); it != world->items.other[idx].end(); it++)
    {
        df::item *i = *it;
        if (Stocks::is_item_free(i) &&
                (!fire_safe || i->isTemperatureSafe(1)) &&
                (!non_economic || !ui->economic_stone[virtual_cast<df::item_boulderst>(i)->mat_index]))
        {
            item = i;
            return true;
        }
    }
    return false;
}

static bool find_items(df::items_other_id idx, std::vector<df::item *> & items, size_t n, bool fire_safe = false, bool non_economic = false)
{
    size_t j = 0;
    for (auto it = world->items.other[idx].begin(); it != world->items.other[idx].end(); it++)
    {
        df::item *i = *it;
        if (Stocks::is_item_free(i) &&
                (!fire_safe || i->isTemperatureSafe(1)) &&
                (!non_economic || !ui->economic_stone[virtual_cast<df::item_boulderst>(i)->mat_index]))
        {
            items.push_back(i);
            j++;
            if (j == n)
                return true;
        }
    }
    return false;
}

command_result Plan::startup(color_ostream & out)
{
    command_result res = setup_blueprint(out);
    if (res != CR_OK)
        return res;

    categorize_all();

    digroom(out, find_room("workshop", [](room *r) -> bool { return r->subtype == "Masons" && r->level == 0; }));
    digroom(out, find_room("workshop", [](room *r) -> bool { return r->subtype == "Carpenters" && r->level == 0; }));
    find_room("workshop", [](room *r) -> bool { return r->subtype.empty() && r->level == 0; })->subtype = "Masons";
    find_room("workshop", [](room *r) -> bool { return r->subtype.empty() && r->level == 1; })->subtype = "Masons";
    find_room("workshop", [](room *r) -> bool { return r->subtype.empty() && r->level == 2; })->subtype = "Masons";
    wantdig(out, find_room("stockpile", [](room *r) -> bool { return r->subtype == "food" && r->level == 0 && r->workshop && r->workshop->type == "farmplot"; }));

    dig_garbagedump(out);

    return res;
}

command_result Plan::onupdate_register(color_ostream & out)
{
    onupdate_handle = events.onupdate_register("df-ai plan", 240, 20, [this](color_ostream & out) { update(out); });
    return CR_OK;
}

command_result Plan::onupdate_unregister(color_ostream & out)
{
    events.onupdate_unregister(onupdate_handle);
    return CR_OK;
}

static bool want_reupdate = false;

void Plan::update(color_ostream & out)
{
    if (bg_idx != tasks.end())
        return;

    bg_idx = tasks.begin();

    cache_nofurnish.clear();

    nrdig = 0;
    for (auto it = tasks.begin(); it != tasks.end(); it++)
    {
        task *t = *it;
        if (t->type != "digroom")
            continue;
        df::coord size = t->r->size();
        if (t->r->type != "corridor" || size.z > 1)
            nrdig++;
        if (t->r->type != "corridor" && size.x * size.y * size.z >= 10)
            nrdig++;
    }

    want_reupdate = false;
    events.onupdate_register_once("df-ai plan bg", [this](color_ostream & out) -> bool
            {
                if (bg_idx == tasks.end())
                {
                    if (want_reupdate)
                    {
                        update(out);
                    }
                    return true;
                }
                task & t = **bg_idx;

                bool del = false;
                if (t.type == "wantdig")
                {
                    if (t.r->is_dug() || nrdig < wantdig_max)
                    {
                        digroom(out, t.r);
                        del = true;
                    }
                }
                else if (t.type == "digroom")
                {
                    if (t.r->is_dug())
                    {
                        t.r->status = "dug";
                        construct_room(out, t.r);
                        want_reupdate = true; // wantdig asap
                        del = true;
                    }
                }
                else if (t.type == "construct_workshop")
                {
                    del = try_construct_workshop(out, t.r);
                }
                else if (t.type == "construct_stockpile")
                {
                    del = try_construct_stockpile(out, t.r);
                }
                else if (t.type == "construct_activityzone")
                {
                    del = try_construct_activityzone(out, t.r);
                }
                else if (t.type == "setup_farmplot")
                {
                    del = try_setup_farmplot(out, t.r);
                }
                else if (t.type == "furnish")
                {
                    del = try_furnish(out, t.r, t.f);
                }
                else if (t.type == "checkfurnish")
                {
                    del = try_endfurnish(out, t.r, t.f);
                }
                else if (t.type == "checkconstruct")
                {
                    del = try_endconstruct(out, t.r);
                }
                else if (t.type == "dig_cistern")
                {
                    del = try_digcistern(out, t.r);
                }
                else if (t.type == "dig_garbage")
                {
                    del = try_diggarbage(out, t.r);
                }
                else if (t.type == "checkidle")
                {
                    del = checkidle(out);
                }
                else if (t.type == "checkrooms")
                {
                    checkrooms(out);
                }
                else if (t.type == "monitor_cistern")
                {
                    monitor_cistern(out);
                }

                if (del)
                {
                    delete *bg_idx;
                    tasks.erase(bg_idx++);
                }
                else
                {
                    bg_idx++;
                }
                return false;
            });
}

uint16_t Plan::getTileWalkable(df::coord t)
{
    if (df::map_block *b = Maps::getTileBlock(t))
        return b->walkable[t.x & 0xf][t.y & 0xf];
    return 0;
}

task *Plan::is_digging()
{
    for (auto it = tasks.begin(); it != tasks.end(); it++)
    {
        task *t = *it;
        if ((t->type == "wantdig" || t->type == "digroom") && t->r->type != "corridor")
            return t;
    }
    return nullptr;
}

bool Plan::is_idle()
{
    for (auto it = tasks.begin(); it != tasks.end(); it++)
    {
        task *t = *it;
        if (t->type != "monitor_cistern" && t->type != "checkrooms" && t->type != "checkidle")
            return false;
    }
    return true;
}

void Plan::new_citizen(color_ostream & out, int32_t uid)
{
    if (std::find_if(tasks.begin(), tasks.end(), [](task *t) -> bool { return t->type == "checkidle"; }) == tasks.end())
    {
        tasks.push_back(new task("checkidle"));
    }
    getdiningroom(out, uid);
    getbedroom(out, uid);
}

void Plan::del_citizen(color_ostream & out, int32_t uid)
{
    freecommonrooms(out, uid);
    freebedroom(out, uid);
}

bool Plan::checkidle(color_ostream & out)
{
    if (is_digging())
        return false;

    // if nothing better to do, order the miners to dig remaining
    // stockpiles, workshops, and a few bedrooms
    auto ifplan = [](room *r) -> bool
    {
        return r->status == "plan";
    };
    int32_t freebed = spare_bedroom;
    room *r = nullptr;
#define FIND_ROOM(cond, type, lambda) \
    if (r == nullptr && (cond)) \
        r = find_room(type, lambda)

    FIND_ROOM(true, "stockpile", [](room *r) -> bool
            {
                return r->subtype == "food" &&
                        r->level == 0 &&
                        !r->workshop &&
                        r->status == "plan";
            });
    FIND_ROOM(!important_workshops.empty(), "workshop", [this](room *r) -> bool
            {
                if (r->subtype == important_workshops.back() &&
                        r->status == "plan" &&
                        r->level == 0)
                {
                    important_workshops.pop_back();
                    return true;
                }
                return false;
            });
    FIND_ROOM(true, "cistern", ifplan);
    FIND_ROOM(true, "location", [](room *r) -> bool { return r->status == "plan" && r->subtype == "tavern"; });
    FIND_ROOM(true, "infirmary", ifplan);
    FIND_ROOM(!find_room("cemetary", [](room *r) -> bool { return r->status != "plan"; }), "cemetary", ifplan);
    FIND_ROOM(!important_workshops2.empty(), "workshop", [this](room *r) -> bool
            {
                if (r->subtype == important_workshops2.back() &&
                        r->status == "plan" &&
                        r->level == 0)
                {
                    important_workshops2.pop_back();
                    return true;
                }
                return false;
            });
    FIND_ROOM(true, "pitcage", ifplan);
    FIND_ROOM(true, "stockpile", [](room *r) -> bool
            {
                return r->status == "plan" &&
                        r->level <= 1;
            });
    FIND_ROOM(true, "workshop", [](room *r) -> bool
            {
                return !r->subtype.empty() &&
                        r->status == "plan" &&
                        r->level == 0;
            });
    if (r == nullptr && !fort_entrance->furnished)
        r = fort_entrance;
    FIND_ROOM(true, "location", ifplan);
    if (r == nullptr)
        past_initial_phase = true;
    int32_t need_food = extra_farms;
    int32_t need_cloth = extra_farms;
    FIND_ROOM(true, "farmplot", ([&need_food, &need_cloth](room *r) -> bool
            {
                if (!r->users.empty())
                {
                    return false;
                }

                if (r->subtype == "food")
                {
                    if (need_food <= 0)
                    {
                        return false;
                    }

                    if (r->status == "plan")
                    {
                        return true;
                    }

                    need_food--;
                }
                else if (r->subtype == "cloth")
                {
                    if (need_cloth <= 0)
                    {
                        return false;
                    }

                    if (r->status == "plan")
                    {
                        return true;
                    }

                    need_cloth--;
                }
                return false;
            }));
    FIND_ROOM(true, "outpost", [](room *r) -> bool
            {
                return r->status == "plan" && r->subtype == "cavern";
            });
    FIND_ROOM(true, "workshop", [](room *r) -> bool
            {
                return !r->subtype.empty() &&
                        r->status == "plan" &&
                        r->level == 1;
            });
    FIND_ROOM(true, "bedroom", [&freebed](room *r) -> bool
            {
                if (r->owner == -1)
                {
                    freebed--;
                    return freebed >= 0 && r->status == "plan";
                }
                return false;
            });
    FIND_ROOM(true, "stockpile", [](room *r) -> bool
            {
                return r->status == "plan" &&
                        r->level <= 2;
            });
    auto finished_nofurnished = [](room *r) -> bool
    {
        return r->status == "finished" && !r->furnished;
    };
    FIND_ROOM(true, "nobleroom", finished_nofurnished);
    FIND_ROOM(true, "bedroom", finished_nofurnished);
    auto nousers_noplan = [](room *r) -> bool
    {
        return r->status != "plan" && std::find_if(r->layout.begin(), r->layout.end(), [](furniture *f) -> bool
                {
                    return f->has_users && f->users.empty();
                }) != r->layout.end();
    };
    auto nousers_plan = [](room *r) -> bool
    {
        return r->status == "plan" && std::find_if(r->layout.begin(), r->layout.end(), [](furniture *f) -> bool
                {
                    return f->has_users && f->users.empty();
                }) != r->layout.end();
    };
    FIND_ROOM(!find_room("dininghall", nousers_noplan), "dininghall", nousers_plan);
    FIND_ROOM(!find_room("barracks", nousers_noplan), "barracks", nousers_plan);
    FIND_ROOM(true, "stockpile", [](room *r) -> bool
            {
                return r->status == "plan" &&
                        r->level <= 3;
            });
    FIND_ROOM(true, "workshop", [](room *r) -> bool
            {
                return !r->subtype.empty() &&
                        r->status == "plan";
            });
    FIND_ROOM(true, "stockpile", ifplan);
#undef FIND_ROOM

    if (r)
    {
        ai->debug(out, "checkidle " + describe_room(r));
        wantdig(out, r);
        if (r->status == "finished")
        {
            r->furnished = true;
            for (auto it = r->layout.begin(); it != r->layout.end(); it++)
            {
                (*it)->ignore = false;
            }
            furnish_room(out, r);
            smooth_room(out, r);
        }
        return false;
    }

    if (is_idle())
    {
        if (setup_blueprint_caverns(out) == CR_OK)
        {
            ai->debug(out, "found next cavern");
            categorize_all();
            return false;
        }
        idleidle(out);
        return true;
    }

    if (last_idle_year != *cur_year)
    {
        last_idle_year = *cur_year;
        idleidle(out);
    }
    return false;
}

static std::vector<room *> idleidle_tab;

void Plan::idleidle(color_ostream & out)
{
    ai->debug(out, "smooth fort");
    idleidle_tab.clear();
    for (auto it = rooms.begin(); it != rooms.end(); it++)
    {
        room *r = *it;
        if (r->status != "plan" && r->status != "dig" &&
                (r->type == "nobleroom" ||
                 r->type == "bedroom" ||
                 r->type == "dininghall" ||
                 r->type == "cemetary" ||
                 r->type == "infirmary" ||
                 r->type == "barracks" ||
                 r->type == "location"))
            idleidle_tab.push_back(r);
    }
    for (auto it = corridors.begin(); it != corridors.end(); it++)
    {
        room *r = *it;
        if (r->status != "plan" && r->status != "dig")
            idleidle_tab.push_back(r);
    }

    events.onupdate_register_once("df-ai plan idleidle", 4, [this](color_ostream & out) -> bool
            {
                if (idleidle_tab.empty())
                {
                    return true;
                }
                smooth_room(out, idleidle_tab.back());
                idleidle_tab.pop_back();
                return false;
            });
}

void Plan::checkrooms(color_ostream & out)
{
    size_t ncheck = 4;
    for (size_t i = ncheck * 4; i > 0; i--)
    {
        if (checkroom_idx < corridors.size() && corridors[checkroom_idx]->status != "plan")
        {
            checkroom(out, corridors[checkroom_idx]);
        }

        if (checkroom_idx < rooms.size() && rooms[checkroom_idx]->status != "plan")
        {
            checkroom(out, rooms[checkroom_idx]);
            ncheck--;
        }
        checkroom_idx++;
        if (ncheck <= 0)
            break;
    }
    if (checkroom_idx >= rooms.size() && checkroom_idx >= corridors.size())
        checkroom_idx = 0;
}

// ensure room was not tantrumed etc
void Plan::checkroom(color_ostream & out, room *r)
{
    if (r->status == "plan")
    {
        // moot
    }
    else if (r->status == "dig")
    {
        // designation cancelled: damp stone etc
        r->dig();
    }
    else if (r->status == "dug" || r->status == "finished")
    {
        // cavein / tree
        r->dig();
        // tantrumed furniture
        for (auto it = r->layout.begin(); it != r->layout.end(); it++)
        {
            furniture *f = *it;
            if (f->ignore)
                continue;
            df::coord t = r->min + df::coord(f->x, f->y, f->z);
            if (f->bld_id != -1 && !df::building::find(f->bld_id))
            {
                ai->debug(out, "fix furniture " + f->item + " in " + describe_room(r), t);
                f->bld_id = -1;

                tasks.push_back(new task("furnish", r, f));
            }
            if (f->construction != construction_type::NONE)
            {
                try_furnish_construction(out, f->construction, t);
            }
        }
        // tantrumed building
        if (r->bld_id != -1 && !r->dfbuilding())
        {
            ai->debug(out, "rebuild " + describe_room(r), r->pos());
            r->bld_id = -1;
            construct_room(out, r);
        }
    }
}

void Plan::getbedroom(color_ostream & out, int32_t id)
{
    room *r = find_room("bedroom", [id](room *r) -> bool { return r->owner == id; });
    if (!r)
        r = find_room("bedroom", [](room *r) -> bool { return r->status != "plan" && r->owner == -1; });
    if (!r)
        r = find_room("bedroom", [](room *r) -> bool { return r->status == "plan" && !r->queue_dig; });
    if (r)
    {
        wantdig(out, r);
        set_owner(out, r, id);
        ai->debug(out, "assign " + describe_room(r), r->pos());
        if (r->status == "finished")
            furnish_room(out, r);
    }
    else
    {
        ai->debug(out, stl_sprintf("[ERROR] AI can't getbedroom(%d)", id));
    }
}

void Plan::getdiningroom(color_ostream & out, int32_t id)
{
    if (room *r = find_room("farmplot", [](room *r_) -> bool
                {
                    df::coord size = r_->size();
                    return r_->subtype == "food" &&
                        !r_->outdoor &&
                        int32_t(r_->users.size()) < size.x * size.y *
                        dwarves_per_farmtile_num / dwarves_per_farmtile_den;
                }))
    {
        wantdig(out, r);
        r->users.insert(id);
    }

    if (room *r = find_room("farmplot", [](room *r_) -> bool
                {
                    df::coord size = r_->size();
                    return r_->subtype == "cloth" &&
                        !r_->outdoor &&
                        int32_t(r_->users.size()) < size.x * size.y *
                        dwarves_per_farmtile_num / dwarves_per_farmtile_den;
                }))
    {
        wantdig(out, r);
        r->users.insert(id);
    }

    if (room *r = find_room("farmplot", [](room *r_) -> bool
                {
                    df::coord size = r_->size();
                    return r_->subtype == "food" &&
                        r_->outdoor &&
                        int32_t(r_->users.size()) < size.x * size.y *
                        dwarves_per_farmtile_num / dwarves_per_farmtile_den;
                }))
    {
        wantdig(out, r);
        r->users.insert(id);
    }

    if (room *r = find_room("farmplot", [](room *r_) -> bool
                {
                    df::coord size = r_->size();
                    return r_->subtype == "cloth" &&
                        r_->outdoor &&
                        int32_t(r_->users.size()) < size.x * size.y *
                        dwarves_per_farmtile_num / dwarves_per_farmtile_den;
                }))
    {
        wantdig(out, r);
        r->users.insert(id);
    }

    if (room *r = find_room("dininghall", [](room *r_) -> bool
                {
                    for (auto it = r_->layout.begin(); it != r_->layout.end(); it++)
                    {
                        furniture *f = *it;
                        if (f->has_users && f->users.size() < dwarves_per_table)
                            return true;
                    }
                    return false;
                }))
    {
        wantdig(out, r);
        for (auto it = r->layout.begin(); it != r->layout.end(); it++)
        {
            furniture *f = *it;
            if (f->item == "table" && f->users.size() < dwarves_per_table)
            {
                f->ignore = false;
                f->users.insert(id);
                break;
            }
        }
        for (auto it = r->layout.begin(); it != r->layout.end(); it++)
        {
            furniture *f = *it;
            if (f->item == "chair" && f->users.size() < dwarves_per_table)
            {
                f->ignore = false;
                f->users.insert(id);
                break;
            }
        }
        if (r->status == "finished")
        {
            furnish_room(out, r);
        }
    }
}

void Plan::attribute_noblerooms(color_ostream & out, const std::set<int32_t> & id_list)
{
    // XXX tomb may be populated...
    while (room *old = find_room("nobleroom", [id_list](room *r) -> bool { return r->owner != -1 && !id_list.count(r->owner); }))
    {
        set_owner(out, old, -1);
    }

    for (auto it = id_list.begin(); it != id_list.end(); it++)
    {
        std::vector<Units::NoblePosition> entpos;
        Units::getNoblePositions(&entpos, df::unit::find(*it));
        room *base = find_room("nobleroom", [it](room *r) -> bool { return r->owner == *it; });
        if (!base)
            base = find_room("nobleroom", [](room *r) -> bool { return r->owner == -1; });
        std::set<std::string> seen;
        while (room *r = find_room("nobleroom", [base, seen](room *r_) -> bool { return r_->noblesuite == base->noblesuite && !seen.count(r_->subtype); }))
        {
            seen.insert(r->subtype);
            set_owner(out, r, *it);
#define DIG_ROOM_IF(type, req) \
            if (r->subtype == type && std::find_if(entpos.begin(), entpos.end(), [](Units::NoblePosition np) -> bool { return np.position->required_ ## req > 0; }) != entpos.end()) \
                wantdig(out, r)
            DIG_ROOM_IF("tomb", tomb);
            DIG_ROOM_IF("diningroom", dining);
            DIG_ROOM_IF("bedroom", bedroom);
            DIG_ROOM_IF("office", office);
#undef DIG_ROOM_IF
        }
    }
}

void Plan::getsoldierbarrack(color_ostream & out, int32_t id)
{
    df::unit *u = df::unit::find(id);
    if (!u)
        return;
    int32_t squad_id = u->military.squad_id;
    if (squad_id == -1)
        return;


    room *r = find_room("barracks", [squad_id](room *r) -> bool { return r->squad_id == squad_id; });
    if (!r)
    {
        r = find_room("barracks", [](room *r) -> bool { return r->squad_id == -1; });
        if (!r)
        {
            ai->debug(out, "[ERROR] no free barracks");
            return;
        }
        r->squad_id = squad_id;
        ai->debug(out, stl_sprintf("squad %d assign %s", squad_id, describe_room(r).c_str()));
        wantdig(out, r);
        if (df::building *bld = r->dfbuilding())
        {
            assign_barrack_squad(out, bld, squad_id);
        }
    }

    auto find_furniture = [id, r](const std::string & type)
    {
        for (auto it = r->layout.begin(); it != r->layout.end(); it++)
        {
            furniture *f = *it;
            if (f->item == type && f->users.count(id))
            {
                f->ignore = false;
                return;
            }
        }
        for (auto it = r->layout.begin(); it != r->layout.end(); it++)
        {
            furniture *f = *it;
            if (f->item == type && (type == "archerytarget" || f->users.size() < (type == "weaponrack" ? 4 : 1)))
            {
                f->users.insert(id);
                f->ignore = false;
                return;
            }
        }
    };
    find_furniture("weaponrack");
    find_furniture("armorstand");
    find_furniture("bed");
    find_furniture("cabinet");
    find_furniture("chest");
    find_furniture("archerytarget");

    if (r->status == "finished")
    {
        furnish_room(out, r);
    }
}

void Plan::assign_barrack_squad(color_ostream & out, df::building *bld, int32_t squad_id)
{
    std::vector<df::building_squad_use *> *squads = bld->getSquads();
    if (squads) // archerytarget has no such field
    {
        auto su = std::find_if(squads->begin(), squads->end(), [squad_id](df::building_squad_use *su) -> bool { return su->squad_id == squad_id; });
        if (su == squads->end())
        {
            df::building_squad_use *newSquad = df::allocate<df::building_squad_use>();
            newSquad->squad_id = squad_id;
            su = squads->insert(su, newSquad);
        }
        (*su)->mode.bits.sleep = 1;
        (*su)->mode.bits.train = 1;
        (*su)->mode.bits.indiv_eq = 1;
        (*su)->mode.bits.squad_eq = 1;
    }

    df::squad *squad = df::squad::find(squad_id);
    auto sr = std::find_if(squad->rooms.begin(), squad->rooms.end(), [bld](df::squad::T_rooms *sr) -> bool { return sr->building_id == bld->id; });
    if (sr == squad->rooms.end())
    {
        df::squad::T_rooms *newRoom = df::allocate<df::squad::T_rooms>();
        newRoom->building_id = bld->id;
        sr = squad->rooms.insert(sr, newRoom);
    }
    (*sr)->mode.bits.sleep = 1;
    (*sr)->mode.bits.train = 1;
    (*sr)->mode.bits.indiv_eq = 1;
    (*sr)->mode.bits.squad_eq = 1;
}

void Plan::getcoffin(color_ostream & out)
{
    if (room *r = find_room("cemetary", [](room *r_) -> bool { return std::find_if(r_->layout.begin(), r_->layout.end(), [](furniture *f) -> bool { return f->has_users && f->users.empty(); }) != r_->layout.end(); }))
    {
        wantdig(out, r);
        for (auto it = r->layout.begin(); it != r->layout.end(); it++)
        {
            furniture *f = *it;
            if (f->item == "coffin" && f->users.empty())
            {
                f->users.insert(0);
                f->ignore = false;
                break;
            }
        }
        if (r->status == "finished")
        {
            furnish_room(out, r);
        }
    }
}

// free / deconstruct the bedroom assigned to this dwarf
void Plan::freebedroom(color_ostream & out, int32_t id)
{
    if (room *r = find_room("bedroom", [id](room *r_) -> bool { return r_->owner == id; }))
    {
        ai->debug(out, "free " + describe_room(r), r->pos());
        set_owner(out, r, -1);
        for (auto it = r->layout.begin(); it != r->layout.end(); it++)
        {
            furniture *f = *it;
            if (f->ignore)
                continue;
            if (f->item == "door")
                continue;
            if (df::building *bld = df::building::find(f->bld_id))
            {
                Buildings::deconstruct(bld);
            }
            f->bld_id = -1;
        }
        r->bld_id = -1;
    }
}

// free / deconstruct the common facilities assigned to this dwarf
// optionnaly restricted to a single subtype among:
//  [dininghall, farmplots, barracks]
void Plan::freecommonrooms(color_ostream & out, int32_t id)
{
    freecommonrooms(out, id, "dininghall");
    freecommonrooms(out, id, "farmplot");
    freecommonrooms(out, id, "barracks");
}

void Plan::freecommonrooms(color_ostream & out, int32_t id, std::string subtype)
{
    if (subtype == "farmplot")
    {
        find_room(subtype, [id](room *r) -> bool
                {
                    r->users.erase(id);
                    return false;
                });
    }
    else
    {
        find_room(subtype, [this, &out, id](room *r) -> bool
                {
                    for (auto it = r->layout.begin(); it != r->layout.end(); it++)
                    {
                        furniture *f = *it;
                        if (!f->has_users)
                            continue;
                        if (f->ignore)
                            continue;
                        if (f->users.erase(id) && f->users.empty())
                        {
                            // delete the specific table/chair/bed/etc for the dwarf
                            if (f->bld_id != -1 && f->bld_id != r->bld_id)
                            {
                                if (df::building *bld = df::building::find(f->bld_id))
                                {
                                    Buildings::deconstruct(bld);
                                }
                                f->bld_id = -1;
                                f->ignore = true;
                            }

                            // clear the whole room if it is entirely unused
                            if (r->bld_id != -1 && std::find_if(r->layout.begin(), r->layout.end(), [](furniture *f) -> bool { return f->has_users && !f->users.empty(); }) == r->layout.end())
                            {
                                if (df::building *bld = r->dfbuilding())
                                {
                                    Buildings::deconstruct(bld);
                                }
                                r->bld_id = -1;

                                if (r->squad_id != -1)
                                {
                                    ai->debug(out, stl_sprintf("squad %d free %s", r->squad_id, describe_room(r).c_str()), r->pos());
                                    r->squad_id = -1;
                                }
                            }
                        }
                    }
                    return false;
                });
    }
}

void Plan::freesoldierbarrack(color_ostream & out, int32_t id)
{
    freecommonrooms(out, id, "barracks");
}

df::building *Plan::getpasture(color_ostream & out, int32_t pet_id)
{
    df::unit *pet = df::unit::find(pet_id);
    size_t limit = 1000 - (11*11*1000 / df::creature_raw::find(pet->race)->caste[pet->caste]->misc.grazer); // 1000 = arbitrary, based on dfwiki?pasture
    if (room *r = find_room("pasture", [limit](room *r_) -> bool
                {
                    size_t sum = 0;
                    for (auto it = r_->users.begin(); it != r_->users.end(); it++)
                    {
                        df::unit *u = df::unit::find(*it);
                        // 11*11 == pasture dimensions
                        sum += 11*11*1000 / df::creature_raw::find(u->race)->caste[u->caste]->misc.grazer;
                    }
                    return sum < limit;
                }))
    {
        r->users.insert(pet_id);
        if (r->bld_id == -1)
            construct_room(out, r);
        return r->dfbuilding();
    }
    return nullptr;
}

void Plan::freepasture(color_ostream & out, int32_t pet_id)
{
    if (room *r = find_room("pasture", [pet_id](room *r_) -> bool { return r_->users.count(pet_id); }))
    {
        r->users.erase(pet_id);
    }
}

void Plan::set_owner(color_ostream & out, room *r, int32_t uid)
{
    r->owner = uid;
    if (r->bld_id != -1)
    {
        df::unit *u = df::unit::find(uid);
        if (df::building *bld = r->dfbuilding())
        {
            Buildings::setOwner(bld, u);
        }
    }
}

void Plan::dig_tile(df::coord t, df::tile_dig_designation dig)
{
    if (ENUM_ATTR(tiletype, material, *Maps::getTileType(t)) == tiletype_material::TREE && dig != tile_dig_designation::No)
    {
        dig = tile_dig_designation::Default;
        t = find_tree_base(t);
    }

    df::tile_designation *des = Maps::getTileDesignation(t);
    if (dig != tile_dig_designation::No && des->bits.dig == tile_dig_designation::No && !des->bits.hidden)
    {
        for (auto job = world->job_list.next; job != nullptr; job = job->next)
        {
            if (ENUM_ATTR(job_type, type, job->item->job_type) == job_type_class::Digging && job->item->pos == t)
            {
                // someone already enroute to dig here, avoid 'Inappropriate
                // dig square' spam
                return;
            }
        }
    }

    des->bits.dig = dig;
    if (dig != tile_dig_designation::No)
        Maps::getTileBlock(t)->flags.bits.designated = 1;
}

// queue a room for digging when other dig jobs are finished
void Plan::wantdig(color_ostream & out, room *r)
{
    if (r->queue_dig || r->status != "plan")
        return;
    ai->debug(out, "wantdig " + describe_room(r));
    r->queue_dig = true;
    r->dig(true);
    tasks.push_back(new task("wantdig", r));
}

void Plan::digroom(color_ostream & out, room *r)
{
    if (r->status != "plan")
        return;
    ai->debug(out, "digroom " + describe_room(r));
    r->queue_dig = false;
    r->status = "dig";
    r->fixup_open();
    r->dig();

    tasks.push_back(new task("digroom", r));

    for (auto it = r->accesspath.begin(); it != r->accesspath.end(); it++)
    {
        digroom(out, *it);
    }

    for (auto it = r->layout.begin(); it != r->layout.end(); it++)
    {
        furniture *f = *it;
        if (f->item.empty() || f->item == "floodgate")
            continue;
        if (f->dig != tile_dig_designation::Default)
            continue;
        tasks.push_back(new task("furnish", r, f));
    }

    if (r->type == "workshop" && r->level == 0)
    {
        // add minimal stockpile in front of workshop
        const static struct sptypes
        {
            std::map<std::string, std::tuple<std::string, std::set<df::stockpile_list>, bool, bool>> map;
            sptypes()
            {
                std::set<df::stockpile_list> disable;
                disable.insert(stockpile_list::StoneOres);
                disable.insert(stockpile_list::StoneClay);
                map["Masons"] = std::make_tuple("stone", disable, false, false);
                disable.clear();
                map["Carpenters"] = std::make_tuple("wood", disable, false, false);
                disable.clear();
                disable.insert(stockpile_list::RefuseItems);
                disable.insert(stockpile_list::RefuseCorpses);
                disable.insert(stockpile_list::RefuseParts);
                map["Craftsdwarfs"] = std::make_tuple("refuse", disable, false, false);
                disable.clear();
                disable.insert(stockpile_list::FoodMeat);
                disable.insert(stockpile_list::FoodFish);
                disable.insert(stockpile_list::FoodUnpreparedFish);
                disable.insert(stockpile_list::FoodEgg);
                disable.insert(stockpile_list::FoodDrinkPlant);
                disable.insert(stockpile_list::FoodDrinkAnimal);
                disable.insert(stockpile_list::FoodCheesePlant);
                disable.insert(stockpile_list::FoodCheeseAnimal);
                disable.insert(stockpile_list::FoodSeeds);
                disable.insert(stockpile_list::FoodLeaves);
                disable.insert(stockpile_list::FoodMilledPlant);
                disable.insert(stockpile_list::FoodBoneMeal);
                disable.insert(stockpile_list::FoodFat);
                disable.insert(stockpile_list::FoodPaste);
                disable.insert(stockpile_list::FoodPressedMaterial);
                disable.insert(stockpile_list::FoodExtractPlant);
                disable.insert(stockpile_list::FoodMiscLiquid);
                map["Farmers"] = std::make_tuple("food", disable, true, false);
                disable.clear();
                disable.insert(stockpile_list::FoodMeat);
                disable.insert(stockpile_list::FoodFish);
                disable.insert(stockpile_list::FoodEgg);
                disable.insert(stockpile_list::FoodPlants);
                disable.insert(stockpile_list::FoodDrinkPlant);
                disable.insert(stockpile_list::FoodDrinkAnimal);
                disable.insert(stockpile_list::FoodCheesePlant);
                disable.insert(stockpile_list::FoodCheeseAnimal);
                disable.insert(stockpile_list::FoodSeeds);
                disable.insert(stockpile_list::FoodLeaves);
                disable.insert(stockpile_list::FoodMilledPlant);
                disable.insert(stockpile_list::FoodBoneMeal);
                disable.insert(stockpile_list::FoodFat);
                disable.insert(stockpile_list::FoodPaste);
                disable.insert(stockpile_list::FoodPressedMaterial);
                disable.insert(stockpile_list::FoodExtractPlant);
                disable.insert(stockpile_list::FoodExtractAnimal);
                disable.insert(stockpile_list::FoodMiscLiquid);
                map["Fishery"] = std::make_tuple("food", disable, true, false);
                disable.clear();
                disable.insert(stockpile_list::FoodFish);
                disable.insert(stockpile_list::FoodUnpreparedFish);
                disable.insert(stockpile_list::FoodEgg);
                disable.insert(stockpile_list::FoodPlants);
                disable.insert(stockpile_list::FoodDrinkPlant);
                disable.insert(stockpile_list::FoodDrinkAnimal);
                disable.insert(stockpile_list::FoodCheesePlant);
                disable.insert(stockpile_list::FoodCheeseAnimal);
                disable.insert(stockpile_list::FoodSeeds);
                disable.insert(stockpile_list::FoodLeaves);
                disable.insert(stockpile_list::FoodMilledPlant);
                disable.insert(stockpile_list::FoodBoneMeal);
                disable.insert(stockpile_list::FoodFat);
                disable.insert(stockpile_list::FoodPaste);
                disable.insert(stockpile_list::FoodPressedMaterial);
                disable.insert(stockpile_list::FoodExtractPlant);
                disable.insert(stockpile_list::FoodExtractAnimal);
                disable.insert(stockpile_list::FoodMiscLiquid);
                map["Butchers"] = std::make_tuple("food", disable, true, false);
                disable.clear();
                map["Jewelers"] = std::make_tuple("gems", disable, false, false);
                disable.clear();
                disable.insert(stockpile_list::ClothSilk);
                disable.insert(stockpile_list::ClothPlant);
                disable.insert(stockpile_list::ClothYarn);
                disable.insert(stockpile_list::ClothMetal);
                map["Loom"] = std::make_tuple("cloth", disable, false, false);
                disable.clear();
                disable.insert(stockpile_list::ThreadSilk);
                disable.insert(stockpile_list::ThreadPlant);
                disable.insert(stockpile_list::ThreadYarn);
                disable.insert(stockpile_list::ThreadMetal);
                map["Clothiers"] = std::make_tuple("cloth", disable, false, false);
                disable.clear();
                disable.insert(stockpile_list::FoodMeat);
                disable.insert(stockpile_list::FoodFish);
                disable.insert(stockpile_list::FoodUnpreparedFish);
                disable.insert(stockpile_list::FoodEgg);
                disable.insert(stockpile_list::FoodDrinkPlant);
                disable.insert(stockpile_list::FoodDrinkAnimal);
                disable.insert(stockpile_list::FoodCheesePlant);
                disable.insert(stockpile_list::FoodCheeseAnimal);
                disable.insert(stockpile_list::FoodSeeds);
                disable.insert(stockpile_list::FoodMilledPlant);
                disable.insert(stockpile_list::FoodBoneMeal);
                disable.insert(stockpile_list::FoodFat);
                disable.insert(stockpile_list::FoodPaste);
                disable.insert(stockpile_list::FoodPressedMaterial);
                disable.insert(stockpile_list::FoodExtractPlant);
                disable.insert(stockpile_list::FoodExtractAnimal);
                disable.insert(stockpile_list::FoodMiscLiquid);
                map["Still"] = std::make_tuple("food", disable, true, false);
                disable.clear();
                disable.insert(stockpile_list::FoodDrinkPlant);
                disable.insert(stockpile_list::FoodDrinkAnimal);
                disable.insert(stockpile_list::FoodSeeds);
                disable.insert(stockpile_list::FoodBoneMeal);
                disable.insert(stockpile_list::FoodMiscLiquid);
                map["Kitchen"] = std::make_tuple("food", disable, true, false);
                disable.clear();
                map["WoodFurnace"] = std::make_tuple("wood", disable, false, false);
                disable.clear();
                disable.insert(stockpile_list::StoneOther);
                disable.insert(stockpile_list::StoneClay);
                map["Smelter"] = std::make_tuple("stone", disable, false, false);
                disable.clear();
                disable.insert(stockpile_list::BlocksStone);
                disable.insert(stockpile_list::BlocksMetal);
                disable.insert(stockpile_list::BlocksOther);
                map["MetalsmithsForge"] = std::make_tuple("bars_blocks", disable, false, false);
            }
        } sptypes;
        if (sptypes.map.count(r->subtype))
        {
            // XXX hardcoded fort layout
            int16_t y = r->layout[0]->y > 0 ? r->max.y + 2 : r->min.y - 2; // check door position
            auto stock = sptypes.map.at(r->subtype);
            room *sp = new room("stockpile", std::get<0>(stock), df::coord(r->min.x, y, r->min.z), df::coord(r->max.x, y, r->min.z));
            sp->stock_disable = std::get<1>(stock);
            sp->stock_specific1 = std::get<2>(stock);
            sp->stock_specific2 = std::get<3>(stock);
            sp->workshop = r;
            sp->level = 0;
            rooms.push_back(sp);
            categorize_all();
            digroom(out, sp);
        }
    }

    df::coord size = r->size();
    if (r->type != "corridor" || size.z > 1)
    {
        nrdig++;
        if (size.x * size.y * size.z >= 10)
            nrdig++;
    }
}

bool Plan::construct_room(color_ostream & out, room *r)
{
    ai->debug(out, "construct " + describe_room(r));

    if (r->type == "corridor")
    {
        return furnish_room(out, r);
    }

    if (r->type == "stockpile")
    {
        furnish_room(out, r);
        tasks.push_back(new task("construct_stockpile", r));
        return true;
    }

    if (r->type == "workshop")
    {
        tasks.push_back(new task("construct_workshop", r));
        return true;
    }

    if (r->type == "farmplot")
    {
        return construct_farmplot(out, r);
    }

    if (r->type == "cistern")
    {
        return construct_cistern(out, r);
    }

    if (r->type == "cemetary")
    {
        return furnish_room(out, r);
    }

    if (r->type == "infirmary" || r->type == "pasture" || r->type == "pitcage" || r->type == "location")
    {
        furnish_room(out, r);
        if (try_construct_activityzone(out, r))
            return true;
        tasks.push_back(new task("construct_activityzone", r));
        return true;
    }

    if (r->type == "dininghall")
    {
        if (!r->temporary)
        {
            if (room *t = find_room("dininghall", [](room *r) -> bool { return r->temporary; }))
            {
                move_dininghall_fromtemp(out, r, t);
            }
        }
        return furnish_room(out, r);
    }

    return furnish_room(out, r);
}

bool Plan::furnish_room(color_ostream & out, room *r)
{
    for (auto it = r->layout.begin(); it != r->layout.end(); it++)
    {
        furniture *f = *it;
        tasks.push_back(new task("furnish", r, f));
    }
    r->status = "finished";
    return true;
}

static df::building_type FurnitureBuilding(std::string k)
{
    if (k == "chest")
        return building_type::Box;
    if (k == "gear_assembly")
        return building_type::GearAssembly;
    if (k == "vertical_axle")
        return building_type::AxleVertical;
    if (k == "traction_bench")
        return building_type::TractionBench;
    if (k == "nestbox")
        return building_type::NestBox;

    k[0] += 'A' - 'a';
    df::building_type bt = building_type::NONE;
    find_enum_item(&bt, k);
    return bt;
}

const static struct traptypes
{
    std::map<std::string, df::trap_type> map;
    traptypes()
    {
        map["cage"] = trap_type::CageTrap;
        map["lever"] = trap_type::Lever;
        map["trackstop"] = trap_type::TrackStop;
    }
} traptypes;

bool Plan::try_furnish(color_ostream & out, room *r, furniture *f)
{
    if (f->bld_id != -1)
        return true;
    if (f->ignore)
        return true;
    df::coord tgtile = r->min + df::coord(f->x, f->y, f->z);
    df::tiletype tt = *Maps::getTileType(tgtile);
    if (f->construction != construction_type::NONE)
    {
        if (try_furnish_construction(out, f->construction, tgtile))
        {
            if (f->item.empty())
                return true;
        }
        else
        {
            return false; // don't try to furnish item before construction is done
        }
    }

    df::item *itm = nullptr;

    if (ENUM_ATTR(tiletype_shape, basic_shape, ENUM_ATTR(tiletype, shape, tt)) == tiletype_shape_basic::Wall)
        return false;

    if (f->item.empty())
        return true;

    if (f->item == "well")
        return try_furnish_well(out, r, f, tgtile);

    if (f->item == "archerytarget")
        return try_furnish_archerytarget(out, r, f, tgtile);

    if (f->item == "gear_assembly" && !find_item(items_other_id::TRAPPARTS, itm))
        return false;

    if (f->item == "vertical_axle" && !find_item(items_other_id::WOOD, itm))
        return false;

    if (f->item == "windmill")
        return try_furnish_windmill(out, r, f, tgtile);

    if (f->item == "roller")
        return try_furnish_windmill(out, r, f, tgtile);

    if (cache_nofurnish.count(f->item))
        return false;

    if (Maps::getTileOccupancy(tgtile)->bits.building != tile_building_occ::None)
    {
        // TODO warn if this stays for too long?
        return false;
    }

    if (ENUM_ATTR(tiletype, shape, tt) == tiletype_shape::RAMP ||
            ENUM_ATTR(tiletype, material, tt) == tiletype_material::TREE ||
            ENUM_ATTR(tiletype, material, tt) == tiletype_material::ROOT)
    {
        dig_tile(tgtile, f->dig);
        return false;
    }

    if (f->item == "floodgate")
    {
        // require the floor to be smooth before we build a floodgate on it
        // because we can't smooth a floor under an open floodgate.
        if (!is_smooth(tgtile))
        {
            std::set<df::coord> tiles;
            tiles.insert(tgtile);
            smooth(tiles);
            return false;
        }
    }

    if (itm == nullptr)
    {
        itm = ai->stocks->find_furniture_item(f->item);
    }

    if (itm != nullptr)
    {
        if (f->subtype == "cage" && ai->stocks->count["cage"] < 1)
        {
            // avoid too much spam
            return false;
        }
        ai->debug(out, "furnish " + f->item + " in " + describe_room(r));
        df::building_type bldn = FurnitureBuilding(f->item);
        int subtype = f->subtype.empty() ? -1 : traptypes.map.at(f->subtype);
        df::building *bld = Buildings::allocInstance(tgtile, bldn, subtype);
        Buildings::setSize(bld, df::coord(1, 1, 1));
        std::vector<df::item *> item;
        item.push_back(itm);
        Buildings::constructWithItems(bld, item);
        if (f->makeroom)
        {
            r->bld_id = bld->id;
        }
        f->bld_id = bld->id;
        tasks.push_back(new task("checkfurnish", r, f));
        return true;
    }

    cache_nofurnish.insert(f->item);
    return false;
}

bool Plan::try_furnish_well(color_ostream & out, room *r, furniture *f, df::coord t)
{
    df::item *block, *mecha, *buckt, *chain;
    if (find_item(items_other_id::BLOCKS, block) &&
            find_item(items_other_id::TRAPPARTS, mecha) &&
            find_item(items_other_id::BUCKET, buckt) &&
            find_item(items_other_id::CHAIN, chain))
    {
        df::building *bld = Buildings::allocInstance(t, building_type::Well);
        Buildings::setSize(bld, df::coord(1, 1, 1));
        std::vector<df::item *> items;
        items.push_back(block);
        items.push_back(mecha);
        items.push_back(buckt);
        items.push_back(chain);
        Buildings::constructWithItems(bld, items);
        f->bld_id = bld->id;
        tasks.push_back(new task("checkfurnish", r, f));
        return true;
    }
    return false;
}

bool Plan::try_furnish_archerytarget(color_ostream & out, room *r, furniture *f, df::coord t)
{
    df::item *bould = nullptr;
    if (!find_item(items_other_id::BOULDER, bould, false, true))
        return false;

    df::building *bld = Buildings::allocInstance(t, building_type::ArcheryTarget);
    Buildings::setSize(bld, df::coord(1, 1, 1));
    virtual_cast<df::building_archerytargetst>(bld)->archery_direction = f->y > 2 ? df::building_archerytargetst::TopToBottom : df::building_archerytargetst::BottomToTop;
    std::vector<df::item *> item;
    item.push_back(bould);
    Buildings::constructWithItems(bld, item);
    f->bld_id = bld->id;
    tasks.push_back(new task("checkfurnish", r, f));
    return true;
}

bool Plan::try_furnish_construction(color_ostream & out, df::construction_type ctype, df::coord t)
{
    df::tiletype tt = *Maps::getTileType(t);
    if (ENUM_ATTR(tiletype, material, tt) == tiletype_material::TREE)
    {
        dig_tile(ai->plan->find_tree_base(t));
        return false;
    }

    df::tiletype_shape_basic sb = ENUM_ATTR(tiletype_shape, basic_shape, ENUM_ATTR(tiletype, shape, tt));
    if (ctype == construction_type::Wall)
    {
        if (sb == tiletype_shape_basic::Wall)
        {
            return true;
        }
    }

    if (ctype == construction_type::Ramp)
    {
        if (sb == tiletype_shape_basic::Ramp)
        {
            return true;
        }
    }

    if (ctype == construction_type::UpStair || ctype == construction_type::DownStair || ctype == construction_type::UpDownStair)
    {
        if (sb == tiletype_shape_basic::Stair)
        {
            return true;
        }
    }

    if (ctype == construction_type::Floor)
    {
        if (sb == tiletype_shape_basic::Floor)
        {
            return true;
        }
        if (sb == tiletype_shape_basic::Ramp)
        {
            dig_tile(t);
            return true;
        }
    }

    // fall through = must build actual construction

    if (ENUM_ATTR(tiletype, material, tt) == tiletype_material::CONSTRUCTION)
    {
        // remove existing invalid construction
        dig_tile(t);
        return false;
    }

    for (auto it = world->buildings.all.begin(); it != world->buildings.all.end(); it++)
    {
        df::building *b = *it;
        if (b->z == t.z && !b->room.extents &&
                b->x1 <= t.x && b->x2 >= t.x &&
                b->y1 <= t.y && b->y2 >= t.y)
        {
            return false;
        }
    }

    df::item *block = nullptr;
    if (!find_item(items_other_id::BLOCKS, block))
        return false;

    df::building *bld = Buildings::allocInstance(t, building_type::Construction, ctype);
    Buildings::setSize(bld, df::coord(1, 1, 1));
    std::vector<df::item *> item;
    item.push_back(block);
    Buildings::constructWithItems(bld, item);
    return true;
}

bool Plan::try_furnish_windmill(color_ostream & out, room *r, furniture *f, df::coord t)
{
    if (ENUM_ATTR(tiletype_shape, basic_shape, ENUM_ATTR(tiletype, shape, *Maps::getTileType(t))) != tiletype_shape_basic::Open)
        return false;

    std::vector<df::item *> mat;
    if (!find_items(items_other_id::WOOD, mat, 4))
        return false;

    df::building *bld = Buildings::allocInstance(t - df::coord(1, 1, 0), building_type::Windmill);
    Buildings::setSize(bld, df::coord(3, 3, 1));
    Buildings::constructWithItems(bld, mat);
    f->bld_id = bld->id;
    tasks.push_back(new task("checkfurnish", r, f));
    return true;
}

bool Plan::try_furnish_roller(color_ostream & out, room *r, furniture *f, df::coord t)
{
    df::item *mecha, *chain;
    if (find_item(items_other_id::TRAPPARTS, mecha) &&
            find_item(items_other_id::CHAIN, chain))
    {
        df::building *bld = Buildings::allocInstance(t, building_type::Rollers);
        Buildings::setSize(bld, df::coord(1, 1, 1));
        std::vector<df::item *> items;
        items.push_back(mecha);
        items.push_back(chain);
        Buildings::constructWithItems(bld, items);
        r->bld_id = bld->id;
        f->bld_id = bld->id;
        tasks.push_back(new task("checkfurnish", r, f));
        return true;
    }
    return false;
}

bool Plan::try_furnish_trap(color_ostream & out, room *r, furniture *f)
{
    df::coord t = r->min + df::coord(f->x, f->y, f->z);

    if (Maps::getTileOccupancy(t)->bits.building != tile_building_occ::None)
        return false;

    df::tiletype tt = *Maps::getTileType(t);
    if (ENUM_ATTR(tiletype, shape, tt) == tiletype_shape::RAMP ||
            ENUM_ATTR(tiletype, material, tt) == tiletype_material::TREE ||
            ENUM_ATTR(tiletype, material, tt) == tiletype_material::ROOT)
    {
        // XXX dont remove last access ramp ?
        dig_tile(t, tile_dig_designation::Default);
        return false;
    }

    df::item *mecha;
    if (find_item(items_other_id::TRAPPARTS, mecha))
        return false;

    df::trap_type subtype = traptypes.map.at(f->subtype);
    df::building *bld = Buildings::allocInstance(t, building_type::Trap, subtype);
    Buildings::setSize(bld, df::coord(1, 1, 1));
    std::vector<df::item *> item;
    item.push_back(mecha);
    Buildings::constructWithItems(bld, item);
    f->bld_id = bld->id;
    tasks.push_back(new task("checkfurnish", r, f));

    return true;
}

static int32_t find_custom_building(const std::string & code)
{
    for (auto it = world->raws.buildings.all.begin(); it != world->raws.buildings.all.end(); it++)
    {
        if ((*it)->code == code)
        {
            return (*it)->id;
        }
    }
    return -1;
}

bool Plan::try_construct_workshop(color_ostream & out, room *r)
{
    if (!r->constructions_done())
        return false;

    if (r->subtype == "Dyers")
    {
        df::item *barrel, *bucket;
        if (find_item(items_other_id::BARREL, barrel) &&
                find_item(items_other_id::BUCKET, bucket))
        {
            df::building *bld = Buildings::allocInstance(r->min, building_type::Workshop, workshop_type::Dyers);
            Buildings::setSize(bld, r->size());
            std::vector<df::item *> items;
            items.push_back(barrel);
            items.push_back(bucket);
            Buildings::constructWithItems(bld, items);
            r->bld_id = bld->id;
            tasks.push_back(new task("checkconstruct", r));
            return true;
        }
    }
    else if (r->subtype == "Ashery")
    {
        df::item *block, *barrel, *bucket;
        if (find_item(items_other_id::BLOCKS, block) &&
                find_item(items_other_id::BARREL, barrel) &&
                find_item(items_other_id::BUCKET, bucket))
        {
            df::building *bld = Buildings::allocInstance(r->min, building_type::Workshop, workshop_type::Ashery);
            Buildings::setSize(bld, r->size());
            std::vector<df::item *> items;
            items.push_back(block);
            items.push_back(barrel);
            items.push_back(bucket);
            Buildings::constructWithItems(bld, items);
            r->bld_id = bld->id;
            tasks.push_back(new task("checkconstruct", r));
            return true;
        }
    }
    else if (r->subtype == "SoapMaker")
    {
        df::item *buckt, *bould;
        if (find_item(items_other_id::BUCKET, buckt) &&
                find_item(items_other_id::BOULDER, bould, false, true))
        {
            df::building *bld = Buildings::allocInstance(r->min, building_type::Workshop, workshop_type::Custom, find_custom_building("SOAP_MAKER"));
            Buildings::setSize(bld, r->size());
            std::vector<df::item *> items;
            items.push_back(buckt);
            items.push_back(bould);
            Buildings::constructWithItems(bld, items);
            r->bld_id = bld->id;
            tasks.push_back(new task("checkconstruct", r));
            return true;
        }
    }
    else if (r->subtype == "ScrewPress")
    {
        std::vector<df::item *> mechas;
        if (find_items(items_other_id::TRAPPARTS, mechas, 2))
        {
            df::building *bld = Buildings::allocInstance(r->min, building_type::Workshop, workshop_type::Custom, find_custom_building("SCREW_PRESS"));
            Buildings::setSize(bld, r->size());
            Buildings::constructWithItems(bld, mechas);
            r->bld_id = bld->id;
            tasks.push_back(new task("checkconstruct", r));
            return true;
        }
    }
    else if (r->subtype == "MetalsmithsForge")
    {
        df::item *anvil, *bould;
        if (find_item(items_other_id::ANVIL, anvil, true) &&
                find_item(items_other_id::BOULDER, bould, true, true))
        {
            df::building *bld = Buildings::allocInstance(r->min, building_type::Workshop, workshop_type::MetalsmithsForge);
            Buildings::setSize(bld, r->size());
            std::vector<df::item *> items;
            items.push_back(anvil);
            items.push_back(bould);
            Buildings::constructWithItems(bld, items);
            r->bld_id = bld->id;
            tasks.push_back(new task("checkconstruct", r));
            return true;
        }
    }
    else if (r->subtype == "WoodFurnace" ||
            r->subtype == "Smelter" ||
            r->subtype == "Kiln" ||
            r->subtype == "GlassFurnace")
    {
        df::item *bould;
        if (find_item(items_other_id::BOULDER, bould, true, true))
        {
            df::furnace_type furnace_subtype;
            if (!find_enum_item(&furnace_subtype, r->subtype))
            {
                ai->debug(out, "[ERROR] could not find furnace subtype for " + r->subtype);
                return true;
            }
            df::building *bld = Buildings::allocInstance(r->min, building_type::Furnace, furnace_subtype);
            Buildings::setSize(bld, r->size());
            std::vector<df::item *> item;
            item.push_back(bould);
            Buildings::constructWithItems(bld, item);
            r->bld_id = bld->id;
            tasks.push_back(new task("checkconstruct", r));
            return true;
        }
    }
    else if (r->subtype == "Quern")
    {
        df::item *quern;
        if (find_item(items_other_id::QUERN, quern))
        {
            df::building *bld = Buildings::allocInstance(r->min, building_type::Workshop, workshop_type::Quern);
            Buildings::setSize(bld, r->size());
            std::vector<df::item *> item;
            item.push_back(quern);
            Buildings::constructWithItems(bld, item);
            r->bld_id = bld->id;
            tasks.push_back(new task("checkconstruct", r));
            return true;
        }
    }
    else if (r->subtype == "TradeDepot")
    {
        std::vector<df::item *> boulds;
        if (find_items(items_other_id::BOULDER, boulds, 3, false, true))
        {
            df::building *bld = Buildings::allocInstance(r->min, building_type::TradeDepot);
            Buildings::setSize(bld, r->size());
            Buildings::constructWithItems(bld, boulds);
            r->bld_id = bld->id;
            tasks.push_back(new task("checkconstruct", r));
            return true;
        }
    }
    else
    {
        df::workshop_type subtype;
        if (!find_enum_item(&subtype, r->subtype))
        {
            ai->debug(out, "[ERROR] could not find workshop subtype for " + r->subtype);
            return true;
        }
        df::item *bould;
        if (find_item(items_other_id::BOULDER, bould, false, true) ||
                // use wood if we can't find stone
                find_item(items_other_id::WOOD, bould))
        {
            df::building *bld = Buildings::allocInstance(r->min, building_type::Workshop, subtype);
            Buildings::setSize(bld, r->size());
            std::vector<df::item *> item;
            item.push_back(bould);
            Buildings::constructWithItems(bld, item);
            r->bld_id = bld->id;
            tasks.push_back(new task("checkconstruct", r));
            return true;
            // XXX else quarry?
        }
    }
    return false;
}

bool Plan::try_construct_stockpile(color_ostream & out, room *r)
{
    if (!r->constructions_done())
        return false;

    if (!AI::is_dwarfmode_viewscreen())
        return false;

    AI::feed_key(interface_key::D_STOCKPILES);
    cursor->x = r->min.x + 1;
    cursor->y = r->min.y;
    cursor->z = r->min.z;
    AI::feed_key(interface_key::CURSOR_LEFT);
    AI::feed_key(interface_key::STOCKPILE_CUSTOM);
    AI::feed_key(interface_key::STOCKPILE_CUSTOM_SETTINGS);
    const static struct stockpile_keys
    {
        std::map<std::string, df::stockpile_list> map;

        stockpile_keys()
        {
            map["animals"] = stockpile_list::Animals;
            map["food"] = stockpile_list::Food;
            map["weapons"] = stockpile_list::Weapons;
            map["armor"] = stockpile_list::Armor;
            map["furniture"] = stockpile_list::Furniture;
            map["corpses"] = stockpile_list::Corpses;
            map["refuse"] = stockpile_list::Refuse;
            map["wood"] = stockpile_list::Wood;
            map["stone"] = stockpile_list::Stone;
            map["gems"] = stockpile_list::Gems;
            map["bars_blocks"] = stockpile_list::BarsBlocks;
            map["cloth"] = stockpile_list::Cloth;
            map["leather"] = stockpile_list::Leather;
            map["ammo"] = stockpile_list::Ammo;
            map["coins"] = stockpile_list::Coins;
            map["finished_goods"] = stockpile_list::Goods;
            map["sheets"] = stockpile_list::Sheet;
        }
    } stockpile_keys;
    df::viewscreen_layer_stockpilest *view = strict_virtual_cast<df::viewscreen_layer_stockpilest>(Gui::getCurViewscreen(true));
    df::stockpile_list wanted_group = stockpile_keys.map.at(r->subtype);
    while (view->cur_group != stockpile_list::AdditionalOptions)
    {
        if (view->cur_group == wanted_group)
        {
            AI::feed_key(interface_key::STOCKPILE_SETTINGS_ENABLE);
            AI::feed_key(interface_key::STOCKPILE_SETTINGS_PERMIT_ALL);
        }
        else
        {
            AI::feed_key(interface_key::STOCKPILE_SETTINGS_DISABLE);
        }
        AI::feed_key(interface_key::STANDARDSCROLL_DOWN);
    }
    while (view->cur_group != wanted_group)
    {
        AI::feed_key(interface_key::STANDARDSCROLL_UP);
    }
    AI::feed_key(interface_key::STANDARDSCROLL_RIGHT);
    for (auto it = r->stock_disable.begin(); it != r->stock_disable.end(); it++)
    {
        while (view->cur_list != *it)
        {
            AI::feed_key(interface_key::STANDARDSCROLL_DOWN);
        }
        AI::feed_key(interface_key::STOCKPILE_SETTINGS_FORBID_SUB);
    }
    if (r->stock_specific1)
    {
        AI::feed_key(interface_key::STOCKPILE_SETTINGS_SPECIFIC1);
    }
    if (r->stock_specific2)
    {
        AI::feed_key(interface_key::STOCKPILE_SETTINGS_SPECIFIC2);
    }
    AI::feed_key(interface_key::LEAVESCREEN);
    AI::feed_key(interface_key::SELECT);
    for (int16_t x = r->min.x; x < r->max.x; x++)
    {
        AI::feed_key(interface_key::CURSOR_RIGHT);
    }
    for (int16_t y = r->min.y; y < r->max.y; y++)
    {
        AI::feed_key(interface_key::CURSOR_DOWN);
    }
    AI::feed_key(interface_key::SELECT);
    AI::feed_key(interface_key::LEAVESCREEN);
    ai->camera->ignore_pause();
    df::building_stockpilest *bld = virtual_cast<df::building_stockpilest>(world->buildings.all.back());
    r->bld_id = bld->id;
    furnish_room(out, r);

    if (r->workshop && r->subtype == "stone")
    {
        room_items(out, r, [](df::item *i) { i->flags.bits.dump = 1; });
    }

    if (r->level == 0 &&
            r->subtype != "stone" && r->subtype != "wood")
    {
        if (room *rr = find_room("stockpile", [r](room *o) -> bool { return o->subtype == r->subtype && o->level == 1; }))
        {
            wantdig(out, rr);
        }
    }

    // setup stockpile links with adjacent level
    find_room("stockpile", [r, bld](room *o) -> bool
            {
                int32_t diff = o->level - r->level;
                if (o->subtype == r->subtype && (diff == -1 || diff == 1))
                {
                    if (df::building_stockpilest *obld = virtual_cast<df::building_stockpilest>(o->dfbuilding()))
                    {
                        df::building_stockpilest *b_from, *b_to;
                        if (diff > 0)
                        {
                            b_from = obld;
                            b_to = bld;
                        }
                        else
                        {
                            b_from = bld;
                            b_to = obld;
                        }
                        for (auto btf = b_to->links.take_from_pile.begin(); btf != b_to->links.take_from_pile.end(); btf++)
                        {
                            if ((*btf)->id == b_from->id)
                            {
                                return false;
                            }
                        }
                        b_to->links.take_from_pile.push_back(b_from);
                        b_from->links.give_to_pile.push_back(b_to);
                    }
                }
                return false; // loop on all stockpiles
            });

    return true;
}

bool Plan::try_construct_activityzone(color_ostream & out, room *r)
{
    if (!r->constructions_done())
        return false;

    if (!AI::is_dwarfmode_viewscreen())
        return false;

    df::coord size = r->size();

    df::building_civzonest *bld = virtual_cast<df::building_civzonest>(Buildings::allocInstance(r->min, building_type::Civzone, civzone_type::ActivityZone));
    Buildings::setSize(bld, size);
    delete[] bld->room.extents;
    bld->room.extents = new uint8_t[size.x * size.y]();
    bld->room.x = r->min.x;
    bld->room.y = r->min.y;
    bld->room.width = size.x;
    bld->room.height = size.y;
    memset(bld->room.extents, 1, size.x * size.y);
    Buildings::constructAbstract(bld);
    r->bld_id = bld->id;
    bld->is_room = true;

    bld->zone_flags.bits.active = 1;
    if (r->type == "infirmary")
    {
        bld->zone_flags.bits.hospital = 1;
        bld->hospital.max_splints = 5;
        bld->hospital.max_thread = 75000;
        bld->hospital.max_cloth = 50000;
        bld->hospital.max_crutches = 5;
        bld->hospital.max_plaster = 750;
        bld->hospital.max_buckets = 2;
        bld->hospital.max_soap = 750;
    }
    else if (r->type == "garbagedump")
    {
        bld->zone_flags.bits.garbage_dump = 1;
    }
    else if (r->type == "pasture")
    {
        bld->zone_flags.bits.pen_pasture = 1;
        bld->pit_flags.whole |= 2;
    }
    else if (r->type == "pitcage")
    {
        bld->zone_flags.bits.pit_pond = 1;
    }
    else if (r->type == "location")
    {
        AI::feed_key(interface_key::D_CIVZONE);

        df::coord pos = r->pos();
        Gui::revealInDwarfmodeMap(pos, true);
        Gui::setCursorCoords(pos.x, pos.y, pos.z);

        AI::feed_key(interface_key::CURSOR_LEFT);
        AI::feed_key(interface_key::CIVZONE_MEETING);
        AI::feed_key(interface_key::ASSIGN_LOCATION);
        AI::feed_key(interface_key::LOCATION_NEW);
        if (r->subtype == "tavern")
        {
            AI::feed_key(interface_key::LOCATION_INN_TAVERN);
        }
        else if (r->subtype == "library")
        {
            AI::feed_key(interface_key::LOCATION_LIBRARY);
        }
        else if (r->subtype == "temple")
        {
            AI::feed_key(interface_key::LOCATION_TEMPLE);
            AI::feed_key(interface_key::SELECT); // no specific deity
        }
        else
        {
            AI::feed_key(interface_key::LEAVESCREEN);
            AI::feed_key(interface_key::LEAVESCREEN);
            ai->debug(out, "[ERROR] unknown location type: " + r->subtype);
        }
        AI::feed_key(interface_key::LEAVESCREEN);

        ai->camera->ignore_pause();
    }

    return true;
}

const static struct farm_allowed_materials
{
    std::set<df::tiletype_material> set;
    farm_allowed_materials()
    {
        set.insert(tiletype_material::GRASS_DARK);
        set.insert(tiletype_material::GRASS_LIGHT);
        set.insert(tiletype_material::GRASS_DRY);
        set.insert(tiletype_material::GRASS_DEAD);
        set.insert(tiletype_material::SOIL);
    }
} farm_allowed_materials;

bool Plan::construct_farmplot(color_ostream & out, room *r)
{
    for (int16_t x = r->min.x; x <= r->max.x; x++)
    {
        for (int16_t y = r->min.y; y <= r->max.y; y++)
        {
            for (int16_t z = r->min.z; z <= r->max.z; z++)
            {
                if (!farm_allowed_materials.set.count(ENUM_ATTR(tiletype, material, *Maps::getTileType(x, y, z))))
                {
                    df::map_block *block = Maps::getTileBlock(x, y, z);
                    auto e = std::find_if(block->block_events.begin(), block->block_events.end(), [](df::block_square_event *e) -> bool
                            {
                                df::block_square_event_material_spatterst *spatter = virtual_cast<df::block_square_event_material_spatterst>(e);
                                return spatter &&
                                    spatter->mat_type == builtin_mats::MUD &&
                                    spatter->mat_index == -1;
                            });
                    if (e == block->block_events.end())
                    {
                        df::block_square_event_material_spatterst *spatter = df::allocate<df::block_square_event_material_spatterst>();
                        spatter->mat_type = builtin_mats::MUD;
                        spatter->mat_index = -1;
                        spatter->min_temperature = 60001;
                        spatter->max_temperature = 60001;
                        e = block->block_events.insert(e, spatter);
                    }
                    df::block_square_event_material_spatterst *spatter = virtual_cast<df::block_square_event_material_spatterst>(*e);
                    if (spatter->amount[x & 0xf][y & 0xf] < 50)
                    {
                        ai->debug(out, stl_sprintf("cheat: mud invocation (%d, %d, %d)", x, y, z));
                        spatter->amount[x & 0xf][y & 0xf] = 50; // small pile of mud
                    }
                }
            }
        }
    }

    df::building *bld = Buildings::allocInstance(r->min, building_type::FarmPlot);
    Buildings::setSize(bld, r->size());
    Buildings::constructWithItems(bld, std::vector<df::item *>());
    r->bld_id = bld->id;
    furnish_room(out, r);
    if (room *st = find_room("stockpile", [r](room *o) -> bool { return o->workshop == r; }))
    {
        digroom(out, st);
    }
    tasks.push_back(new task("setup_farmplot", r));
    return true;
}

void Plan::move_dininghall_fromtemp(color_ostream & out, room *r, room *t)
{
    // if we dug a real hall, get rid of the temporary one
    for (auto f = t->layout.begin(); f != t->layout.end(); f++)
    {
        if ((*f)->item.empty() || !(*f)->has_users)
        {
            continue;
        }
        for (auto of = r->layout.begin(); of != r->layout.end(); of++)
        {
            if ((*of)->item == (*f)->item && (*of)->has_users && (*of)->users.empty())
            {
                (*of)->users = (*f)->users;
                if (!(*f)->ignore)
                {
                    (*of)->ignore = false;
                }
                if (df::building *bld = df::building::find((*f)->bld_id))
                {
                    Buildings::deconstruct(bld);
                }
                break;
            }
        }
    }
    rooms.erase(std::remove(rooms.begin(), rooms.end(), t), rooms.end());
    for (auto it = tasks.begin(); it != tasks.end(); )
    {
        if ((*it)->r == t)
        {
            delete *it;
            tasks.erase(it++);
        }
        else
        {
            it++;
        }
    }
    delete t;
    categorize_all();
}

void Plan::smooth_room(color_ostream & out, room *r)
{
    smooth_xyz(r->min - df::coord(1, 1, 0), r->max + df::coord(1, 1, 0));
}

// smooth a room and its accesspath corridors (recursive)
void Plan::smooth_room_access(color_ostream & out, room *r)
{
    smooth_room(out, r);
    for (auto it = r->accesspath.begin(); it != r->accesspath.end(); it++)
    {
        smooth_room_access(out, *it);
    }
}

void Plan::smooth_cistern(color_ostream & out, room *r)
{
    for (auto it = r->accesspath.begin(); it != r->accesspath.end(); it++)
    {
        smooth_cistern_access(out, *it);
    }

    std::set<df::coord> tiles;
    for (int16_t z = r->min.z; z <= r->max.z; z++)
    {
        for (int16_t x = r->min.x - 1; x <= r->max.x + 1; x++)
        {
            for (int16_t y = r->min.y - 1; y <= r->max.y + 1; y++)
            {
                if (z != r->min.z &&
                        r->min.x <= x && x <= r->max.x &&
                        r->min.y <= y && y <= r->max.y)
                    continue;

                tiles.insert(df::coord(x, y, z));
            }
        }
    }
    smooth(tiles);
}

// smooth only the inside of the room and any walls, but not adjacent floors
void Plan::smooth_cistern_access(color_ostream & out, room *r)
{
    std::set<df::coord> tiles;
    for (int16_t x = r->min.x - 1; x <= r->max.x + 1; x++)
    {
        for (int16_t y = r->min.y - 1; y <= r->max.y + 1; y++)
        {
            for (int16_t z = r->min.z; z <= r->max.z; z++)
            {
                if (x < r->min.x || r->max.x < x || y < r->min.y || r->max.y < y)
                {
                    if (ENUM_ATTR(tiletype_shape, basic_shape, ENUM_ATTR(tiletype, shape, *Maps::getTileType(x, y, z))) != tiletype_shape_basic::Wall)
                    {
                        continue;
                    }
                }
                tiles.insert(df::coord(x, y, z));
            }
        }
    }
    smooth(tiles);
    for (auto it = r->accesspath.begin(); it != r->accesspath.end(); it++)
    {
        smooth_cistern_access(out, *it);
    }
}

bool Plan::construct_cistern(color_ostream & out, room *r)
{
    furnish_room(out, r);
    smooth_cistern(out, r);

    // remove boulders
    dump_items_access(out, r);

    // build levers for floodgates
    wantdig(out, find_room("location", [](room *r) -> bool { return r->subtype == "tavern"; }));

    // check smoothing progress, channel intermediate levels
    if (r->subtype == "well")
    {
        tasks.push_back(new task("dig_cistern", r));
    }

    return true;
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
    for (auto it = r->accesspath.begin(); it != r->accesspath.end(); it++)
    {
        found = dump_items_access(out, *it) || found;
    }
    return found;
}

// yield every on_ground items in the room
void Plan::room_items(color_ostream & out, room *r, std::function<void(df::item *)> f)
{
    for (int16_t z = r->min.z; z <= r->max.z; z++)
    {
        for (int16_t x = r->min.x & -16; x <= r->max.x; x += 16)
        {
            for (int16_t y = r->min.y & -16; y <= r->max.y; y += 16)
            {
                auto & items = Maps::getTileBlock(x, y, z)->items;
                for (auto it = items.begin(); it != items.end(); it++)
                {
                    df::item *i = df::item::find(*it);
                    if (i->flags.bits.on_ground &&
                            r->min.x <= i->pos.x && i->pos.x <= r->max.x &&
                            r->min.y <= i->pos.y && i->pos.y <= r->max.y &&
                            z == i->pos.z)
                    {
                        f(i);
                    }
                }
            }
        }
    }
}

void Plan::smooth_xyz(df::coord min, df::coord max)
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
    smooth(tiles);
}

void Plan::smooth(std::set<df::coord> tiles)
{
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
            tiles.erase(it++);
            continue;
        }

        // already smooth
        if (is_smooth(*it))
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
    for (auto j = world->job_list.next; j != nullptr; j = j->next)
    {
        if (j->item->job_type == job_type::DetailWall ||
                j->item->job_type == job_type::DetailFloor)
        {
            tiles.erase(j->item->pos);
        }
    }

    // mark the tiles to be smoothed!
    for (auto it = tiles.begin(); it != tiles.end(); it++)
    {
        Maps::getTileDesignation(*it)->bits.smooth = 1;
        Maps::getTileBlock(*it)->flags.bits.designated = 1;
    }
}

bool Plan::is_smooth(df::coord t)
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
        sp == tiletype_special::TRACK ||
        sp == tiletype_special::SMOOTH ||
        s == tiletype_shape::FORTIFICATION ||
        sb == tiletype_shape_basic::Open ||
        sb == tiletype_shape_basic::Stair ||
        (bld != tile_building_occ::None &&
         bld != tile_building_occ::Dynamic);
}

// check smoothing progress, channel intermediate floors when done
bool Plan::try_digcistern(color_ostream & out, room *r)
{
    // XXX hardcoded layout..
    int32_t cnt = 0;
    int16_t acc_y = r->accesspath[0]->min.y;
    for (int16_t z = r->min.z + 1; z <= r->max.z; z++)
    {
        for (int16_t x = r->max.x; x >= r->min.x; x--)
        {
            bool stop = false;
            for (int16_t y = r->min.y; y <= r->max.y; y++)
            {
                switch (ENUM_ATTR(tiletype_shape, basic_shape,
                            ENUM_ATTR(tiletype, shape,
                                *Maps::getTileType(x, y, z))))
                {
                    case tiletype_shape_basic::Floor:
                        stop = true;
                        if (!is_smooth(df::coord(x + 1, y - 1, z)) ||
                                !is_smooth(df::coord(x + 1, y, z)) ||
                                !is_smooth(df::coord(x + 1, y + 1, z)))
                        {
                            continue;
                        }
                        if (ENUM_ATTR(tiletype_shape, basic_shape,
                                ENUM_ATTR(tiletype, shape,
                                    *Maps::getTileType(x - 1, y, z))) ==
                                tiletype_shape_basic::Floor)
                        {
                            dig_tile(df::coord(x, y, z), tile_dig_designation::Channel);
                        }
                        else
                        {
                            // last column before stairs
                            df::coord nt10(x, y - 1, z);
                            df::coord nt12(x, y + 1, z);
                            df::coord nt00(x - 1, y - 1, z);
                            df::coord nt02(x - 1, y + 1, z);
                            if (y > acc_y ?
                                    is_smooth(nt12) && is_smooth(nt02) :
                                    is_smooth(nt10) && is_smooth(nt00))
                            {
                                dig_tile(df::coord(x, y, z), tile_dig_designation::Channel);
                            }
                        }
                        break;
                    case tiletype_shape_basic::Open:
                        if (r->min.x <= x && x <= r->max.x &&
                                r->min.y <= y && y <= r->max.y)
                        {
                            cnt++;
                        }
                        break;
                    default:
                        break;
                }
            }
            if (stop)
                break;
        }
    }

    trycistern_count++;
    if (trycistern_count % 12 == 0)
    {
        smooth_cistern(out, r);
    }

    df::coord size = r->size();
    if (cnt == size.x * size.y * (size.z - 1))
    {
        r->channeled = true;
        return true;
    }
    return false;
}

void Plan::dig_garbagedump(color_ostream & out)
{
    find_room("garbagepit", [this](room *r) -> bool
            {
                if (r->status == "plan")
                {
                    r->status = "dig";
                    r->dig(false, true);
                    tasks.push_back(new task("dig_garbage", r));
                }
                return false;
            });
}

bool Plan::try_diggarbage(color_ostream & out, room *r)
{
    if (r->is_dug(tiletype_shape_basic::Open))
    {
        r->status = "dug";
        // XXX ugly as usual
        df::coord t(r->min.x, r->min.y, r->min.z - 1);
        if (ENUM_ATTR(tiletype_shape, basic_shape, ENUM_ATTR(tiletype, shape,
                        *Maps::getTileType(t))) == tiletype_shape_basic::Ramp)
        {
            dig_tile(t, tile_dig_designation::Default);
        }
        find_room("garbagedump", [this, &out](room *r) -> bool
                {
                    if (r->status == "plan")
                    {
                        try_construct_activityzone(out, r);
                    }
                    return false;
                });
        return true;
    }
    // tree ?
    r->dig(false, true);
    return false;
}

bool Plan::try_setup_farmplot(color_ostream & out, room *r)
{
    df::building *bld = r->dfbuilding();
    if (!bld)
        return true;
    if (bld->getBuildStage() < bld->getMaxBuildStage())
        return false;

    ai->stocks->farmplot(out, r);

    return true;
}

bool Plan::try_endfurnish(color_ostream & out, room *r, furniture *f)
{
    if (!AI::is_dwarfmode_viewscreen())
    {
        // some of these things need to use the UI.
        return false;
    }

    df::building *bld = df::building::find(f->bld_id);
    if (!bld)
    {
        // destroyed building?
        return true;
    }
    if (bld->getBuildStage() < bld->getMaxBuildStage())
        return false;

    if (f->item == "coffin")
    {
        df::building_coffinst *coffin = virtual_cast<df::building_coffinst>(bld);
        coffin->burial_mode.bits.allow_burial = 1;
        coffin->burial_mode.bits.no_citizens = 0;
        coffin->burial_mode.bits.no_pets = 1;
    }
    else if (f->item == "door")
    {
        df::building_doorst *door = virtual_cast<df::building_doorst>(bld);
        door->door_flags.bits.pet_passable = 1;
        door->door_flags.bits.internal = f->internal ? 1 : 0;
    }
    else if (f->item == "trap")
    {
        if (f->subtype == "lever")
        {
            return setup_lever(out, r, f);
        }
    }
    else if (f->item == "floodgate")
    {
        if (!f->way.empty())
        {
            for (auto rr = rooms.begin(); rr != rooms.end(); rr++)
            {
                if ((*rr)->status == "plan")
                    continue;
                for (auto ff = (*rr)->layout.begin(); ff != (*rr)->layout.end(); ff++)
                {
                    if ((*ff)->item == "trap" &&
                            (*ff)->subtype == "lever" &&
                            (*ff)->target == f)
                    {
                        link_lever(out, *ff, f);
                    }
                }
            }
        }
    }
    else if (f->item == "archerytarget")
    {
        f->makeroom = true;
    }

    if (r->type == "infirmary")
    {
        // Toggle hospital off and on because it's easier than figuring out
        // what Dwarf Fortress does. Shouldn't cancel any jobs, but might
        // create jobs if we just built a box.

        AI::feed_key(interface_key::D_CIVZONE);

        df::coord pos = r->pos();
        Gui::revealInDwarfmodeMap(pos, true);
        Gui::setCursorCoords(pos.x, pos.y, pos.z);

        AI::feed_key(interface_key::CURSOR_LEFT);
        AI::feed_key(interface_key::CIVZONE_HOSPITAL);
        AI::feed_key(interface_key::CIVZONE_HOSPITAL);
        AI::feed_key(interface_key::LEAVESCREEN);

        ai->camera->ignore_pause();
    }

    if (!f->makeroom)
        return true;
    if (!r->is_dug())
        return false;

    ai->debug(out, "makeroom " + describe_room(r));

    df::coord size = r->size() + df::coord(2, 2, 0);

    delete[] bld->room.extents;
    bld->room.extents = new uint8_t[size.x * size.y]();
    bld->room.x = r->min.x - 1;
    bld->room.y = r->min.y - 1;
    bld->room.width = size.x;
    bld->room.height = size.y;
    auto set_ext = [&bld](int16_t x, int16_t y, uint8_t v)
    {
        bld->room.extents[bld->room.width * (y - bld->room.y) + (x - bld->room.x)] = v;
    };
    for (int16_t rx = r->min.x - 1; rx <= r->max.x + 1; rx++)
    {
        for (int16_t ry = r->min.y - 1; ry <= r->max.y + 1; ry++)
        {
            if (ENUM_ATTR(tiletype, shape, *Maps::getTileType(rx, ry, r->min.z)) == tiletype_shape::WALL)
            {
                set_ext(rx, ry, 2);
            }
            else
            {
                set_ext(rx, ry, r->include(df::coord(rx, ry, r->min.z)) ? 3 : 4);
            }
        }
    }
    for (auto f_ = r->layout.begin(); f_ != r->layout.end(); f_++)
    {
        if ((*f_)->item != "door")
            continue;
        int16_t x = r->min.x + (*f_)->x;
        int16_t y = r->min.y + (*f_)->y;
        set_ext(x, y, 0);
        // tile in front of the door tile is 4 (TODO door in corner...)
        if (x < r->min.x)
            set_ext(x + 1, y, 4);
        if (x > r->max.x)
            set_ext(x - 1, y, 4);
        if (y < r->min.y)
            set_ext(x, y + 1, 4);
        if (y > r->max.y)
            set_ext(x, y - 1, 4);
    }
    bld->is_room = true;

    set_owner(out, r, r->owner);
    furnish_room(out, r);

    if (r->type == "dininghall")
    {
        virtual_cast<df::building_tablest>(bld)->table_flags.bits.meeting_hall = 1;
    }
    else if (r->type == "barracks")
    {
        df::building *bld = r->dfbuilding();
        if (f->item == "archerytarget")
        {
            bld = df::building::find(f->bld_id);
        }
        if (r->squad_id != -1 && bld)
        {
            assign_barrack_squad(out, bld, r->squad_id);
        }
    }

    return true;
}

bool Plan::setup_lever(color_ostream & out, room *r, furniture *f)
{
    if (r->type == "location" && r->subtype == "tavern")
    {
        std::string way = f->way;
        if (!f->target)
        {
            room *cistern = find_room("cistern", [](room *r) -> bool { return r->subtype == "reserve"; });
            for (auto gate = cistern->layout.begin(); gate != cistern->layout.end(); gate++)
            {
                if ((*gate)->item == "floodgate" && (*gate)->way == way)
                {
                    f->target = *gate;
                    break;
                }
            }
        }

        if (link_lever(out, f, f->target))
        {
            pull_lever(out, f);
            if (way == "in")
            {
                tasks.push_back(new task("monitor_cistern"));
            }

            return true;
        }
    }
    return false;
}

bool Plan::link_lever(color_ostream & out, furniture *src, furniture *dst)
{
    if (src->bld_id == -1 || dst->bld_id == -1)
        return false;
    df::building *bld = df::building::find(src->bld_id);
    if (!bld || bld->getBuildStage() < bld->getMaxBuildStage())
        return false;
    df::building *tbld = df::building::find(dst->bld_id);
    if (!tbld || tbld->getBuildStage() < tbld->getMaxBuildStage())
        return false;

    for (auto ref = bld->general_refs.begin(); ref != bld->general_refs.end(); ref++)
    {
        df::general_ref_building_triggertargetst *tt = virtual_cast<df::general_ref_building_triggertargetst>(*ref);
        if (tt && tt->building_id == tbld->id)
            return false;
    }
    for (auto j = bld->jobs.begin(); j != bld->jobs.end(); j++)
    {
        if ((*j)->job_type == job_type::LinkBuildingToTrigger)
        {
            for (auto ref = (*j)->general_refs.begin(); ref != (*j)->general_refs.end(); ref++)
            {
                df::general_ref_building_triggertargetst *tt = virtual_cast<df::general_ref_building_triggertargetst>(*ref);
                if (tt && tt->building_id == tbld->id)
                    return false;
            }
        }
    }

    std::vector<df::item *> mechas;
    if (!find_items(items_other_id::TRAPPARTS, mechas, 2))
        return false;

    df::general_ref_building_triggertargetst *reflink = df::allocate<df::general_ref_building_triggertargetst>();
    reflink->building_id = tbld->id;
    df::general_ref_building_holderst *refhold = df::allocate<df::general_ref_building_holderst>();
    refhold->building_id = bld->id;

    df::job *job = df::allocate<df::job>();
    job->job_type = job_type::LinkBuildingToTrigger;
    job->general_refs.push_back(reflink);
    job->general_refs.push_back(refhold);
    bld->jobs.push_back(job);
    Job::linkIntoWorld(job);

    Job::attachJobItem(job, mechas[0], df::job_item_ref::LinkToTarget);
    Job::attachJobItem(job, mechas[1], df::job_item_ref::LinkToTrigger);

    return true;
}

bool Plan::pull_lever(color_ostream & out, furniture *f)
{
    df::building *bld = df::building::find(f->bld_id);
    if (!bld)
    {
        ai->debug(out, "cistern: missing lever " + f->way);
        return false;
    }
    if (!bld->jobs.empty())
    {
        ai->debug(out, "cistern: lever has job " + f->way);
        return false;
    }
    ai->debug(out, "cistern: pull lever " + f->way);

    df::general_ref_building_holderst *ref = df::allocate<df::general_ref_building_holderst>();
    ref->building_id = bld->id;

    df::job *job = df::allocate<df::job>();
    job->job_type = job_type::PullLever;
    job->pos = df::coord(bld->x1, bld->y1, bld->z);
    job->general_refs.push_back(ref);
    bld->jobs.push_back(job);
    Job::linkIntoWorld(job);
    return true;
}

void Plan::monitor_cistern(color_ostream & out)
{
    if (!m_c_lever_in)
    {
        room *well = find_room("location", [](room *r) -> bool { return r->subtype == "tavern"; });
        for (auto f = well->layout.begin(); f != well->layout.end(); f++)
        {
            if ((*f)->item == "trap" && (*f)->subtype == "lever")
            {
                if ((*f)->way == "in")
                    m_c_lever_in = *f;
                else if ((*f)->way == "out")
                    m_c_lever_out = *f;
            }
        }
        m_c_cistern = find_room("cistern", [](room *r) -> bool { return r->subtype == "well"; });
        m_c_reserve = find_room("cistern", [](room *r) -> bool { return r->subtype == "reserve"; });
        if (m_c_reserve->channel_enable.isValid())
        {
            m_c_testgate_delay = 2;
        }
    }

    df::building_trapst *l_in = nullptr, *l_out = nullptr;
    df::building_floodgatest *f_in = nullptr, *f_out = nullptr;
    if (m_c_lever_in->bld_id != -1)
        l_in = virtual_cast<df::building_trapst>(df::building::find(m_c_lever_in->bld_id));
    if (m_c_lever_in->target && m_c_lever_in->target->bld_id != -1)
        f_in = virtual_cast<df::building_floodgatest>(df::building::find(m_c_lever_in->target->bld_id));
    if (m_c_lever_out->bld_id != -1)
        l_out = virtual_cast<df::building_trapst>(df::building::find(m_c_lever_out->bld_id));
    if (m_c_lever_out->target && m_c_lever_out->target->bld_id != -1)
        f_out = virtual_cast<df::building_floodgatest>(df::building::find(m_c_lever_out->target->bld_id));

    if (l_in && !f_out && !l_in->linked_mechanisms.empty())
    {
        // f_in is linked, can furnish f_out now without risking walling
        // workers in the reserve
        for (auto l = m_c_reserve->layout.begin(); l != m_c_reserve->layout.end(); l++)
        {
            (*l)->ignore = false;
        }
        furnish_room(out, m_c_reserve);
    }

    if (!l_in || !f_in || !l_out || !f_out)
        return;
    if (l_in->linked_mechanisms.empty() || l_out->linked_mechanisms.empty())
        return;
    if (!l_in->jobs.empty() || !l_out->jobs.empty())
        return;

    // may use mechanisms for entrance cage traps now
    bool furnish_entrance = false;
    for (auto f = fort_entrance->layout.begin(); f != fort_entrance->layout.end(); f++)
    {
        if ((*f)->ignore)
        {
            (*f)->ignore = false;
            furnish_entrance = true;
        }
    }
    if (furnish_entrance)
    {
        furnish_room(out, fort_entrance);
    }

    bool f_in_closed = (f_in->gate_flags.bits.closed || f_in->gate_flags.bits.closing) && !f_in->gate_flags.bits.opening;
    bool f_out_closed = (f_out->gate_flags.bits.closed || f_out->gate_flags.bits.closing) && !f_out->gate_flags.bits.opening;

    if (m_c_testgate_delay != -1)
    {
        // expensive, dont check too often
        m_c_testgate_delay--;
        if (m_c_testgate_delay > 0)
            return;

        // re-dump garbage + smooth reserve accesspath
        construct_cistern(out, m_c_reserve);

        df::coord gate = m_c_reserve->channel_enable;
        if (ENUM_ATTR(tiletype_shape, basic_shape,
                    ENUM_ATTR(tiletype, shape,
                        *Maps::getTileType(gate))) ==
                tiletype_shape_basic::Wall)
        {
            ai->debug(out, "cistern: test channel");
            bool empty = true;
            std::list<room *> todo;
            todo.push_back(m_c_reserve);
            while (empty && !todo.empty())
            {
                room *r = todo.front();
                todo.pop_front();
                todo.insert(todo.end(), r->accesspath.begin(), r->accesspath.end());
                for (int16_t x = r->min.x - 1; empty && x <= r->max.x + 1; x++)
                {
                    for (int16_t y = r->min.y - 1; empty && y <= r->max.y + 1; y++)
                    {
                        for (int16_t z = r->min.z; z <= r->max.z; z++)
                        {
                            df::coord t(x, y, z);
                            if (!is_smooth(t) && (r->type != "corridor" || (r->min.x <= x && x <= r->max.x && r->min.y <= y && y <= r->max.y) || ENUM_ATTR(tiletype_shape, basic_shape, ENUM_ATTR(tiletype, shape, *Maps::getTileType(t))) == tiletype_shape_basic::Wall))
                            {
                                ai->debug(out, stl_sprintf("cistern: unsmoothed (%d, %d, %d) %s", x, y, z, describe_room(r).c_str()));
                                empty = false;
                                break;
                            }
                            df::tile_occupancy occ = *Maps::getTileOccupancy(t);
                            if (occ.bits.unit || occ.bits.unit_grounded)
                            {
                                for (auto u = world->units.active.begin(); u != world->units.active.end(); u++)
                                {
                                    if (Units::getPosition(*u) == t)
                                    {
                                        ai->debug(out, stl_sprintf("cistern: unit (%d, %d, %d) (%s) %s", x, y, z, AI::describe_unit(*u).c_str(), describe_room(r).c_str()));
                                        break;
                                    }
                                }
                                empty = false;
                                break;
                            }
                            if (occ.bits.item)
                            {
                                auto & items = Maps::getTileBlock(t)->items;
                                for (auto it = items.begin(); it != items.end(); it++)
                                {
                                    df::item *i = df::item::find(*it);
                                    if (Items::getPosition(i) == t)
                                    {
                                        ai->debug(out, stl_sprintf("cistern: item (%d, %d, %d) (%s) %s", x, y, z, AI::describe_item(i).c_str(), describe_room(r).c_str()));
                                    }
                                }
                                empty = false;
                                break;
                            }
                        }
                    }
                }
            }

            if (empty && !dump_items_access(out, m_c_reserve))
            {
                if (!f_out_closed)
                {
                    // avoid floods. wait for the output to be closed.
                    pull_lever(out, m_c_lever_out);
                    m_c_testgate_delay = 4;
                }
                else
                {
                    if (f_in_closed)
                    {
                        pull_lever(out, m_c_lever_in);
                    }
                    ai->debug(out, "cistern: do channel");
                    cistern_channel_requested = true;
                    dig_tile(gate + df::coord(0, 0, 1), tile_dig_designation::Channel);
                    m_c_testgate_delay = 16;
                }
            }
            else
            {
                room *well = find_room("location", [](room *r) -> bool { return r->subtype == "tavern"; });
                if (Maps::getTileDesignation(well->min + df::coord(-2, well->size().y / 2, 0))->bits.flow_size == 7)
                {
                    // something went not as planned, but we have a water source
                    m_c_testgate_delay = -1;
                }
                else
                {
                    if (!cistern_channel_requested)
                    {
                        // make sure we can actually access the cistern
                        if (f_in_closed)
                            pull_lever(out, m_c_lever_in);
                        if (f_out_closed)
                            pull_lever(out, m_c_lever_out);
                    }
                    m_c_testgate_delay = 16;
                }
            }
        }
        else
        {
            m_c_testgate_delay = -1;
            // channel surrounding tiles
            for (int16_t x = -1; x <= 1; x++)
            {
                for (int16_t y = -1; y <= 1; y++)
                {
                    df::tiletype tt = *Maps::getTileType(gate + df::coord(x, y, 0));
                    if (ENUM_ATTR(tiletype_shape, basic_shape, ENUM_ATTR(tiletype, shape, tt)) != tiletype_shape_basic::Wall)
                        continue;
                    df::tiletype_material tm = ENUM_ATTR(tiletype, material, tt);
                    if (tm != tiletype_material::STONE && tm != tiletype_material::MINERAL && tm != tiletype_material::SOIL && tm != tiletype_material::ROOT)
                        continue;
                    if (spiral_search(gate + df::coord(x, y, 0), 1, 1, [](df::coord ttt) -> bool { df::tile_designation *td = Maps::getTileDesignation(ttt); return td && td->bits.feature_local; }).isValid())
                    {
                        dig_tile(gate + df::coord(x, y, 1), tile_dig_designation::Channel);
                    }
                }
            }
        }
        return;
    }

    if (!m_c_cistern->channeled)
        return;

    // cistlvl = water level for the top floor
    // if it is flooded, reserve output tile is probably > 4 and prevents
    // buildingdestroyers from messing with the floodgates
    uint32_t cistlvl = Maps::getTileDesignation(m_c_cistern->pos() + df::coord(0, 0, 1))->bits.flow_size;
    uint32_t resvlvl = Maps::getTileDesignation(m_c_reserve->pos())->bits.flow_size;

    if (resvlvl <= 1)
    {
        // reserve empty, fill it
        if (!f_out_closed)
        {
            pull_lever(out, m_c_lever_out);
        }
        else if (f_in_closed)
        {
            pull_lever(out, m_c_lever_in);
        }
    }
    else if (resvlvl == 7)
    {
        // reserve full, empty into cistern if needed
        if (!f_in_closed)
        {
            pull_lever(out, m_c_lever_in);
        }
        else
        {
            if (cistlvl < 7)
            {
                if (f_out_closed)
                {
                    pull_lever(out, m_c_lever_out);
                }
            }
            else
            {
                if (!f_out_closed)
                {
                    pull_lever(out, m_c_lever_out);
                }
            }
        }
    }
    else
    {
        // ensure it's either filling up or emptying
        if (f_in_closed && f_out_closed)
        {
            if (resvlvl >= 6 && cistlvl < 7)
            {
                pull_lever(out, m_c_lever_out);
            }
            else
            {
                pull_lever(out, m_c_lever_in);
            }
        }
    }
}

bool Plan::try_endconstruct(color_ostream & out, room *r)
{
    df::building *bld = r->dfbuilding();
    if (bld && bld->getBuildStage() < bld->getMaxBuildStage())
        return false;
    furnish_room(out, r);
    return true;
}

// returns one tile of an outdoor river (if one exists)
df::coord Plan::scan_river(color_ostream & out)
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

command_result Plan::setup_blueprint(color_ostream & out)
{
    command_result res;
    // TODO use existing fort facilities (so we can relay the user or continue from a save)
    ai->debug(out, "setting up fort blueprint...");
    // TODO place fort body first, have main stair stop before surface, and place trade depot on path to surface
    res = scan_fort_entrance(out);
    if (res != CR_OK)
        return res;
    ai->debug(out, "blueprint found entrance");
    // TODO if no room for fort body, make surface fort
    res = scan_fort_body(out);
    if (res != CR_OK)
        return res;
    ai->debug(out, "blueprint found body");
    res = setup_blueprint_rooms(out);
    if (res != CR_OK)
        return res;
    ai->debug(out, "blueprint found rooms");
    // ensure traps are on the surface
    for (auto i = fort_entrance->layout.begin(); i != fort_entrance->layout.end(); i++)
    {
        (*i)->z = surface_tile_at(fort_entrance->min.x + (*i)->x, fort_entrance->min.y + (*i)->y, true).z - fort_entrance->min.z;
    }
    fort_entrance->layout.erase(std::remove_if(fort_entrance->layout.begin(), fort_entrance->layout.end(), [this](furniture *i) -> bool
                {
                    df::coord t = fort_entrance->min + df::coord(i->x, i->y, i->z - 1);
                    df::tiletype *tt = Maps::getTileType(t);
                    if (!tt)
                    {
                        delete i;
                        return true;
                    }
                    df::tiletype_material tm = ENUM_ATTR(tiletype, material, *tt);
                    if (ENUM_ATTR(tiletype_shape, basic_shape, ENUM_ATTR(tiletype, shape, *tt)) != tiletype_shape_basic::Wall || (tm != tiletype_material::STONE && tm != tiletype_material::MINERAL && tm != tiletype_material::SOIL && tm != tiletype_material::ROOT && (!allow_ice || tm != tiletype_material::FROZEN_LIQUID)))
                    {
                        delete i;
                        return true;
                    }
                    return false;
                }), fort_entrance->layout.end());
    res = list_map_veins(out);
    if (res != CR_OK)
        return res;
    ai->debug(out, "blueprint found veins");
    res = setup_outdoor_gathering_zones(out);
    if (res != CR_OK)
        return res;
    ai->debug(out, "blueprint outdoor gathering zones");
    res = setup_blueprint_caverns(out);
    if (res == CR_OK)
        ai->debug(out, "blueprint found caverns");
    res = make_map_walkable(out);
    if (res != CR_OK)
        return res;
    ai->debug(out, "LET THE GAME BEGIN!");
    return CR_OK;
}

command_result Plan::make_map_walkable(color_ostream & out)
{
    events.onupdate_register_once("df-ai plan make_map_walkable", [this](color_ostream & out) -> bool
            {
                // if we don't have a river, we're fine
                df::coord river = scan_river(out);
                if (!river.isValid())
                    return true;
                df::coord surface = surface_tile_at(river.x, river.y);
                if (surface.isValid() && ENUM_ATTR(tiletype, material, *Maps::getTileType(surface)) == tiletype_material::BROOK)
                    return true;

                Plan *plan = this;

                river = spiral_search(river, [plan](df::coord t) -> bool
                        {
                            // TODO rooms outline
                            int16_t y = plan->fort_entrance->pos().y;
                            if (t.y >= y + Plan::MinY && t.y <= y + Plan::MaxY)
                                return false;
                            df::tile_designation *td = Maps::getTileDesignation(t);
                            return td && td->bits.feature_local;
                        });
                if (!river.isValid())
                    return true;

                // find a safe place for the first tile
                df::coord t1 = spiral_search(river, [plan](df::coord t) -> bool
                        {
                            if (!plan->map_tile_in_rock(t))
                                return false;
                            df::coord st = plan->surface_tile_at(t.x, t.y);
                            return plan->map_tile_in_rock(st + df::coord(0, 0, -1));
                        });
                if (!t1.isValid())
                    return true;
                t1 = surface_tile_at(t1.x, t1.y);

                // if the game hasn't done any pathfinding yet, wait until the next frame and try again
                uint16_t t1w = getTileWalkable(t1);
                if (t1w == 0)
                    return false;

                // find the second tile
                df::coord t2 = spiral_search(t1, [t1w](df::coord t) -> bool
                        {
                            uint16_t tw = Plan::getTileWalkable(t);
                            return tw != 0 && tw != t1w;
                        });
                if (!t2.isValid())
                    return true;
                uint16_t t2w = getTileWalkable(t2);

                // make sure the second tile is in a safe place
                t2 = spiral_search(t2, [plan, t2w](df::coord t) -> bool
                        {
                            return Plan::getTileWalkable(t) == t2w && plan->map_tile_in_rock(t - df::coord(0, 0, 1));
                        });
                if (!t2.isValid())
                    return true;

                // find the bottom of the staircases
                int16_t z;
                for (auto f = world->features.map_features.begin(); f != world->features.map_features.end(); f++)
                {
                    df::feature_init_outdoor_riverst *r = virtual_cast<df::feature_init_outdoor_riverst>(*f);
                    if (r)
                    {
                        z = *std::min_element(r->feature->min_map_z.begin(), r->feature->min_map_z.end()) - 1;
                        break;
                    }
                }

                // make the corridors
                std::vector<room *> cor;
                cor.push_back(new room(t1, df::coord(t1, z)));
                cor.push_back(new room(t2, df::coord(t2, z)));

                int16_t dx = t1.x - t2.x;
                if (-1 > dx)
                    cor.push_back(new room(df::coord(t1.x + 1, t1.y, z), df::coord(t2.x - 1, t1.y, z)));
                else if (dx > 1)
                    cor.push_back(new room(df::coord(t1.x - 1, t1.y, z), df::coord(t2.x + 1, t1.y, z)));

                int16_t dy = t1.y - t2.y;
                if (-1 > dy)
                    cor.push_back(new room(df::coord(t2.x, t1.y + 1, z), df::coord(t2.x, t2.y - 1, z)));
                else if (dy > 1)
                    cor.push_back(new room(df::coord(t2.x, t1.y - 1, z), df::coord(t2.x, t2.y + 1, z)));

                if ((-1 > dx || dx > 1) && (-1 > dy || dy > 1))
                    cor.push_back(new room(df::coord(t2.x, t1.y, z), df::coord(t2.x, t1.y, z)));

                for (auto c = cor.begin(); c != cor.end(); c++)
                {
                    corridors.push_back(*c);
                    wantdig(out, *c);
                }
                return true;
            });
    return CR_OK;
}

// scan the map, list all map veins in @map_veins (mat_index => [block coords],
// sorted by z)
command_result Plan::list_map_veins(color_ostream & out)
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
                for (auto event = block->block_events.begin(); event != block->block_events.end(); event++)
                {
                    df::block_square_event_mineralst *vein = virtual_cast<df::block_square_event_mineralst>(*event);
                    if (vein)
                    {
                        map_veins[vein->inorganic_mat].insert(block->map_pos);
                    }
                }
            }
        }
    }
    return CR_OK;
}

static int32_t mat_index_vein(df::coord t)
{
    auto & events = Maps::getTileBlock(t)->block_events;
    for (auto it = events.rbegin(); it != events.rend(); it++)
    {
        df::block_square_event_mineralst *mineral = virtual_cast<df::block_square_event_mineralst>(*it);
        if (mineral && mineral->getassignment(t.x & 0xf, t.y & 0xf))
        {
            return mineral->inorganic_mat;
        }
    }
    return -1;
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
                        df::tiletype tt = *Maps::getTileType(d.first);
                        df::tiletype_shape_basic sb = ENUM_ATTR(tiletype_shape, basic_shape, ENUM_ATTR(tiletype, shape, tt));
                        if (sb == tiletype_shape_basic::Open)
                        {
                            df::construction_type ctype;
                            if (d.second == tile_dig_designation::Default)
                            {
                                ctype = construction_type::Floor;
                            }
                            else if (!find_enum_item(&ctype, ENUM_KEY_STR(tile_dig_designation, d.second)))
                            {
                                ai->debug(out, "[ERROR] could not find construction_type::" + ENUM_KEY_STR(tile_dig_designation, d.second));
                                return false;
                            }
                            return try_furnish_construction(out, ctype, d.first);
                        }
                        if (sb != tiletype_shape_basic::Wall)
                        {
                            return true;
                        }
                        if (Maps::getTileDesignation(d.first)->bits.dig == tile_dig_designation::No) // warm/wet tile
                            dig_tile(d.first, d.second);
                        if (ENUM_ATTR(tiletype, material, tt) == tiletype_material::MINERAL && mat_index_vein(d.first) == mat)
                            count++;
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
            break;
        auto & veins = map_veins.at(mat);
        auto it = std::find_if(veins.begin(), veins.end(), [this](df::coord c) -> bool
                {
                    for (int16_t vx = -1; vx <= 1; vx++)
                    {
                        for (int16_t vy = -1; vy <= 1; vy++)
                        {
                            if (dug_veins.count(c + df::coord(16 * vx, 16 * vy, 0)))
                            {
                                return true;
                            }
                        }
                    }
                    return false;
                });
        if (it == veins.end())
            it--;
        df::coord v = *it;
        map_veins.at(mat).erase(it);
        int32_t cnt = do_dig_vein(out, mat, v);
        if (cnt > 0)
        {
            dug_veins.insert(v);
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
    ai->debug(out, "dig_vein " + world->raws.inorganics[mat]->id);
    int32_t count = 0;
    int16_t fort_minz = 0x7fff;
    for (auto c = corridors.begin(); c != corridors.end(); c++)
    {
        if ((*c)->subtype != "veinshaft")
        {
            fort_minz = std::min(fort_minz, (*c)->min.z);
        }
    }
    if (b.z >= fort_minz)
    {
        int16_t y = fort_entrance->pos().y;
        // TODO rooms outline
        if (b.y > y + MinY && b.y < y + MaxY)
        {
            return count;
        }
    }

    auto & q = map_vein_queue[mat];

    // dig whole block
    // TODO have the dwarves search for the vein
    // TODO mine in (visible?) chunks
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
            if (ENUM_ATTR(tiletype, material, *Maps::getTileType(t)) == tiletype_material::MINERAL && mat_index_vein(t) == mat)
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
                                ok = false;
                            else
                                ns = false;
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
                    if (ENUM_ATTR(tiletype, material, *Maps::getTileType(t)) == tiletype_material::MINERAL && mat_index_vein(t) == mat)
                        count++;
                    need_shaft = ns;
                }
            }
        }
    }
    for (auto d = todo.begin(); d != todo.end(); d++)
    {
        q.push_back(*d);
        dig_tile(d->first, d->second);
    }

    if (need_shaft)
    {
        // TODO minecarts?

        // avoid giant vertical runs: slalom x+-1 every z%16

        df::coord vert = fort_entrance->pos();
        if (b.y > vert.y)
        {
            vert = df::coord(vert.x, vert.y + 30, b.z); // XXX 30...
        }
        else
        {
            vert = df::coord(vert.x, vert.y - 30, b.z);
        }
        if (vert.z % 32 > 16)
            vert.x++; // XXX check this

        df::coord t0 = b + df::coord((minx + maxx) / 2, (miny + maxy) / 2, 0);
        while (t0.y != vert.y)
        {
            dig_tile(t0, tile_dig_designation::Default);
            q.push_back(std::make_pair(t0, tile_dig_designation::Default));
            if (t0.y > vert.y)
                t0.y--;
            else
                t0.y++;
        }
        while (t0.x != vert.x)
        {
            dig_tile(t0, tile_dig_designation::Default);
            q.push_back(std::make_pair(t0, tile_dig_designation::Default));
            if (t0.x > vert.x)
                t0.x--;
            else
                t0.x++;
        }
        while (Maps::getTileDesignation(t0)->bits.hidden)
        {
            if (t0.z % 16 == 0)
            {
                dig_tile(t0, tile_dig_designation::DownStair);
                q.push_back(std::make_pair(t0, tile_dig_designation::DownStair));
                if (t0.z % 32 >= 16)
                    t0.x++;
                else
                    t0.x--;
                dig_tile(t0, tile_dig_designation::UpStair);
                q.push_back(std::make_pair(t0, tile_dig_designation::UpStair));
            }
            else
            {
                dig_tile(t0, tile_dig_designation::UpDownStair);
                q.push_back(std::make_pair(t0, tile_dig_designation::UpDownStair));
            }
            t0.z++;
        }
        if (!ENUM_ATTR(tiletype_shape, passable_low, ENUM_ATTR(tiletype, shape, *Maps::getTileType(t0))) && Maps::getTileDesignation(t0)->bits.dig == tile_dig_designation::No)
        {
            dig_tile(t0, tile_dig_designation::DownStair);
            q.push_back(std::make_pair(t0, tile_dig_designation::DownStair));
        }
    }

    return count;
}

// same as ruby spiral_search, but search starting with center of each side
df::coord Plan::spiral_search(df::coord t, int16_t max, int16_t min, int16_t step, std::function<bool(df::coord)> b)
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
            if (b(tt))
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
                if (b(tt))
                    return tt;
            }
        }
    }

    df::coord invalid;
    invalid.clear();
    return invalid;
}

static furniture *new_cage_trap(int16_t x, int16_t y)
{
    furniture *f = new furniture();
    f->item = "trap";
    f->subtype = "cage";
    f->x = x;
    f->y = y;
    f->ignore = true;
    return f;
}

// search a valid tile for fortress entrance
command_result Plan::scan_fort_entrance(color_ostream & out)
{
    // map center
    int16_t cx = world->map.x_count / 2;
    int16_t cy = world->map.y_count / 2;
    df::coord center = surface_tile_at(cx, cy, true);

    df::coord ent0 = spiral_search(center, [this](df::coord t0) -> bool
            {
                // test the whole map for 3x5 clear spots
                df::coord t = surface_tile_at(t0.x, t0.y);
                if (!t.isValid())
                    return false;

                // make sure we're not too close to the edge of the map.
                if (t.x + MinX < 0 || t.x + MaxX >= world->map.x_count ||
                        t.y + MinY < 0 || t.y + MaxY >= world->map.y_count ||
                        t.z + MinZ < 0 || t.z + MaxZ >= world->map.z_count)
                {
                    return false;
                }

                for (int16_t _x = -1; _x <= 1; _x++)
                {
                    for (int16_t _y = -2; _y <= 2; _y++)
                    {
                        df::tiletype tt = *Maps::getTileType(t + df::coord(_x, _y, -1));
                        if (ENUM_ATTR(tiletype, shape, tt) != tiletype_shape::WALL)
                            return false;
                        df::tiletype_material tm = ENUM_ATTR(tiletype, material, tt);
                        if (!allow_ice &&
                                tm != tiletype_material::STONE &&
                                tm != tiletype_material::MINERAL &&
                                tm != tiletype_material::SOIL &&
                                tm != tiletype_material::ROOT)
                            return false;
                        df::coord ttt = t + df::coord(_x, _y, 0);
                        if (ENUM_ATTR(tiletype, shape, *Maps::getTileType(ttt)) != tiletype_shape::FLOOR)
                            return false;
                        df::tile_designation td = *Maps::getTileDesignation(ttt);
                        if (td.bits.flow_size != 0 || td.bits.hidden)
                            return false;
                        if (Buildings::findAtTile(ttt))
                            return false;
                    }
                }
                for (int16_t _x = -3; _x <= 3; _x++)
                {
                    for (int16_t _y = -4; _y <= 4; _y++)
                    {
                        if (!surface_tile_at(t.x + _x, t.y + _y, true).isValid())
                            return false;
                    }
                }
                return true;
            });

    if (!ent0.isValid())
    {
        if (!allow_ice)
        {
            allow_ice = true;

            return scan_fort_entrance(out);
        }

        ai->debug(out, "[ERROR] Can't find a fortress entrance spot. We need a 3x5 flat area with solid ground for at least 2 tiles on each side.");
        return CR_FAILURE;
    }

    df::coord ent = surface_tile_at(ent0.x, ent0.y);

    fort_entrance = new room(ent - df::coord(0, 1, 0), ent + df::coord(0, 1, 0));
    for (int i = 0; i < 3; i++)
    {
        fort_entrance->layout.push_back(new_cage_trap(i - 1, -1));
        fort_entrance->layout.push_back(new_cage_trap(1, i));
        fort_entrance->layout.push_back(new_cage_trap(1 - i, 3));
        fort_entrance->layout.push_back(new_cage_trap(-1, 2 - i));
    }
    for (int i = 0; i < 5; i++)
    {
        fort_entrance->layout.push_back(new_cage_trap(i - 2, -2));
        fort_entrance->layout.push_back(new_cage_trap(2, i - 1));
        fort_entrance->layout.push_back(new_cage_trap(2 - i, 4));
        fort_entrance->layout.push_back(new_cage_trap(-2, 3 - i));
    }

    return CR_OK;
}

// search how much we need to dig to find a spot for the full fortress body
// here we cheat and work as if the map was fully reveal()ed
command_result Plan::scan_fort_body(color_ostream & out)
{
    // use a hardcoded fort layout
    df::coord c = fort_entrance->pos();

    for (int16_t cz1 = c.z; cz1 >= 0; cz1--)
    {
        bool stop = false;
        // stop searching if we hit a cavern or an aquifer inside our main
        // staircase
        for (int16_t x = fort_entrance->min.x; !stop && x <= fort_entrance->max.x; x++)
        {
            for (int16_t y = fort_entrance->min.y; !stop && y <= fort_entrance->max.y; y++)
            {
                df::coord t(x, y, cz1 + MaxZ);
                if (!map_tile_nocavern(t) || Maps::getTileDesignation(t)->bits.water_table)
                    stop = true;
            }
        }
        if (stop)
        {
            break;
        }

        auto check = [this, c, cz1, &stop](int16_t dx, int16_t dy, int16_t dz)
        {
            df::coord t = df::coord(c, cz1) + df::coord(dx, dy, dz);
            df::tiletype tt = *Maps::getTileType(t);
            df::tiletype_material tm = ENUM_ATTR(tiletype, material, tt);
            if (ENUM_ATTR(tiletype, shape, tt) != tiletype_shape::WALL ||
                    Maps::getTileDesignation(t)->bits.water_table ||
                    (tm != tiletype_material::STONE &&
                     tm != tiletype_material::MINERAL &&
                     (!allow_ice || tm != tiletype_material::FROZEN_LIQUID) &&
                     (dz < 0 || (tm != tiletype_material::SOIL &&
                                 tm != tiletype_material::ROOT))))
                stop = true;
        };

        for (int16_t dz = MinZ; !stop && dz <= MaxZ; dz++)
        {
            // scan perimeter first to quickly eliminate caverns / bad rock
            // layers
            for (int16_t dx = MinX; !stop && dx <= MaxX; dx++)
            {
                for (int16_t dy = MinY; !stop && dy <= MaxY; dy += MaxY - MinY)
                {
                    check(dx, dy, dz);
                }
            }
            for (int16_t dx = MinX; !stop && dx <= MaxX; dx += MaxX - MinX)
            {
                for (int16_t dy = MinY + 1; !stop && dy <= MaxY - 1; dy++)
                {
                    check(dx, dy, dz);
                }
            }
            // perimeter ok, full scan
            for (int16_t dx = MinX + 1; !stop && dx <= MaxX - 1; dx++)
            {
                for (int16_t dy = MinY + 1; !stop && dy <= MaxY - 1; dy++)
                {
                    check(dx, dy, dz);
                }
            }
        }

        if (!stop)
        {
            fort_entrance->min.z = cz1;
            return CR_OK;
        }
    }

    ai->debug(out, "[ERROR] Too many caverns, cant find room for fort. We need more minerals!");
    return CR_FAILURE;
}

// assign rooms in the space found by scan_fort_*
command_result Plan::setup_blueprint_rooms(color_ostream & out)
{
    // hardcoded layout
    corridors.push_back(fort_entrance);

    df::coord f = fort_entrance->pos();

    command_result res;

    std::vector<room *> fe;
    fe.push_back(fort_entrance);

    f.z = fort_entrance->min.z;
    res = setup_blueprint_workshops(out, f, fe);
    if (res != CR_OK)
        return res;
    ai->debug(out, "workshop floor ready");

    fort_entrance->min.z--;
    f.z--;
    res = setup_blueprint_utilities(out, f, fe);
    if (res != CR_OK)
        return res;
    ai->debug(out, "utility floor ready");

    fort_entrance->min.z--;
    f.z--;
    res = setup_blueprint_stockpiles(out, f, fe);
    if (res != CR_OK)
        return res;
    ai->debug(out, "stockpile floor ready");

    for (size_t i = 0; i < 2; i++)
    {
        fort_entrance->min.z--;
        f.z--;
        res = setup_blueprint_bedrooms(out, f, fe);
        if (res != CR_OK)
            return res;
        ai->debug(out, stl_sprintf("bedroom floor ready %d/2", i + 1));
    }

    return CR_OK;
}

static furniture *new_furniture(const std::string & item, int16_t x, int16_t y)
{
    furniture *f = new furniture();
    f->item = item;
    f->x = x;
    f->y = y;
    return f;
}

static furniture *new_furniture_with_users(const std::string & item, int16_t x, int16_t y, bool ignore = false)
{
    furniture *f = new_furniture(item, x, y);
    f->has_users = true;
    f->ignore = ignore;
    return f;
}

static furniture *new_door(int16_t x, int16_t y, bool internal = false)
{
    furniture *f = new_furniture("door", x, y);
    f->internal = internal;
    return f;
}

static furniture *new_dig(df::tile_dig_designation d, int16_t x, int16_t y, int16_t z = 0)
{
    furniture *f = new furniture();
    f->dig = d;
    f->x = x;
    f->y = y;
    f->z = z;
    return f;
}

static furniture *new_construction(df::construction_type c, int16_t x, int16_t y, int16_t z = 0)
{
    furniture *f = new furniture();
    f->construction = c;
    f->x = x;
    f->y = y;
    f->z = z;
    return f;
}

command_result Plan::setup_blueprint_workshops(color_ostream & out, df::coord f, std::vector<room *> entr)
{
    room *corridor_center0 = new room(f + df::coord(-1, -1, 0), f + df::coord(-1, 1, 0));
    corridor_center0->layout.push_back(new_door(-1, 0));
    corridor_center0->layout.push_back(new_door(-1, 2));
    corridor_center0->accesspath = entr;
    corridors.push_back(corridor_center0);

    room *corridor_center2 = new room(f + df::coord(1, -1, 0), f + df::coord(1, 1, 0));
    corridor_center2->layout.push_back(new_door(1, 0));
    corridor_center2->layout.push_back(new_door(1, 2));
    corridor_center2->accesspath = entr;
    corridors.push_back(corridor_center2);

    // Millstone, Siege, magma workshops/furnaces
    std::vector<std::string> types;
    types.push_back("Still");
    types.push_back("Kitchen");
    types.push_back("Fishery");
    types.push_back("Butchers");
    types.push_back("Leatherworks");
    types.push_back("Tanners");
    types.push_back("Loom");
    types.push_back("Clothiers");
    types.push_back("Dyers");
    types.push_back("Bowyers");
    types.push_back("");
    types.push_back("Kiln");
    types.push_back("Masons");
    types.push_back("Carpenters");
    types.push_back("Mechanics");
    types.push_back("Farmers");
    types.push_back("Craftsdwarfs");
    types.push_back("Jewelers");
    types.push_back("Smelter");
    types.push_back("MetalsmithsForge");
    types.push_back("Ashery");
    types.push_back("WoodFurnace");
    types.push_back("SoapMaker");
    types.push_back("GlassFurnace");
    auto type_it = types.begin();

    for (int16_t dirx = -1; dirx <= 1; dirx += 2)
    {
        room *prev_corx = dirx < 0 ? corridor_center0 : corridor_center2;
        int16_t ocx = f.x + dirx * 3;
        for (int16_t dx = 1; dx <= 6; dx++)
        {
            // segments of the big central horizontal corridor
            int16_t cx = f.x + dirx * 4 * dx;
            room *cor_x = new room(df::coord(ocx, f.y - 1, f.z), df::coord(cx + (dx <= 5 ? 0 : dirx), f.y + 1, f.z));
            cor_x->accesspath.push_back(prev_corx);
            corridors.push_back(cor_x);
            prev_corx = cor_x;
            ocx = cx + dirx;

            if (dirx == 1 && dx == 3)
            {
                // stuff a quern&screwpress near the farmers'
                df::coord c(cx - 2, f.y + 1, f.z);
                room *r = new room("workshop", "Quern", c, c);
                r->accesspath.push_back(cor_x);
                r->level = 0;
                rooms.push_back(r);

                c.x = cx - 6;
                r = new room("workshop", "Quern", c, c);
                r->accesspath.push_back(cor_x);
                r->level = 1;
                rooms.push_back(r);

                c.x = cx + 2;
                r = new room("workshop", "Quern", c, c);
                r->accesspath.push_back(cor_x);
                r->level = 2;
                rooms.push_back(r);

                c.x = cx - 2;
                c.y = f.y - 1;
                r = new room("workshop", "ScrewPress", c, c);
                r->accesspath.push_back(cor_x);
                r->level = 0;
                rooms.push_back(r);
            }
            else if (dirx == -1 && dx == 6)
            {
                room *r = new room("location", "library", df::coord(cx - 12, f.y - 5, f.z), df::coord(cx - 3, f.y - 1, f.z));
                r->layout.push_back(new_door(10, 4));
                r->layout.push_back(new_furniture("chest", 9, 3));
                r->layout.push_back(new_furniture("chest", 9, 2));
                r->layout.push_back(new_furniture("table", 9, 1));
                r->layout.push_back(new_furniture("chair", 8, 1));
                r->layout.push_back(new_furniture("table", 9, 0));
                r->layout.push_back(new_furniture("chair", 8, 0));
                for (int16_t i = 0; i < 6; i++)
                {
                    r->layout.push_back(new_furniture("bookcase", i + 1, 1));
                    r->layout.push_back(new_furniture("bookcase", i + 1, 3));
                }
                r->accesspath.push_back(cor_x);
                rooms.push_back(r);

                r = new room("location", "temple", df::coord(cx - 12, f.y + 1, f.z), df::coord(cx - 3, f.y + 5, f.z));
                r->layout.push_back(new_door(10, 0));
                r->accesspath.push_back(cor_x);
                rooms.push_back(r);
            }

            std::string t = *type_it++;

            room *r = new room("workshop", t, df::coord(cx - 1, f.y - 5, f.z), df::coord(cx + 1, f.y - 3, f.z));
            r->accesspath.push_back(cor_x);
            r->layout.push_back(new_door(1, 3));
            r->level = 0;
            if (dirx == -1 && dx == 1)
            {
                r->layout.push_back(new_furniture("nestbox", -1, 4));
            }
            rooms.push_back(r);

            r = new room("workshop", t, df::coord(cx - 1, f.y - 8, f.z), df::coord(cx + 1, f.y - 6, f.z));
            r->accesspath.push_back(rooms.back());
            r->level = 1;
            rooms.push_back(r);

            r = new room("workshop", t, df::coord(cx - 1, f.y - 11, f.z), df::coord(cx + 1, f.y - 9, f.z));
            r->accesspath.push_back(rooms.back());
            r->level = 2;
            rooms.push_back(r);

            t = *type_it++;
            r = new room("workshop", t, df::coord(cx - 1, f.y + 3, f.z), df::coord(cx + 1, f.y + 5, f.z));
            r->accesspath.push_back(cor_x);
            r->layout.push_back(new_door(1, -1));
            r->level = 0;
            if (dirx == -1 && dx == 1)
            {
                r->layout.push_back(new_furniture("nestbox", -1, -2));
            }
            rooms.push_back(r);

            r = new room("workshop", t, df::coord(cx - 1, f.y + 6, f.z), df::coord(cx + 1, f.y + 8, f.z));
            r->accesspath.push_back(rooms.back());
            r->level = 1;
            rooms.push_back(r);

            r = new room("workshop", t, df::coord(cx - 1, f.y + 9, f.z), df::coord(cx + 1, f.y + 11, f.z));
            r->accesspath.push_back(rooms.back());
            r->level = 2;
            rooms.push_back(r);
        }
    }

    df::coord depot_center = spiral_search(df::coord(f.x - 4, f.y, fort_entrance->max.z - 1), [this](df::coord t) -> bool
            {
                for (int16_t dx = -2; dx <= 2; dx++)
                {
                    for (int16_t dy = -2; dy <= 2; dy++)
                    {
                        df::coord tt = t + df::coord(dx, dy, 0);
                        if (!map_tile_in_rock(tt))
                            return false;
                        if (map_tile_intersects_room(tt))
                            return false;
                    }
                }
                for (int16_t dy = -1; dy <= 1; dy++)
                {
                    df::coord tt = t + df::coord(-3, dy, 0);
                    if (!map_tile_in_rock(tt))
                        return false;
                    df::coord ttt = tt + df::coord(0, 0, 1);
                    if (ENUM_ATTR(tiletype_shape, basic_shape, ENUM_ATTR(tiletype, shape, *Maps::getTileType(ttt))) != tiletype_shape_basic::Floor)
                        return false;
                    if (map_tile_intersects_room(tt))
                        return false;
                    if (map_tile_intersects_room(ttt))
                        return false;
                }
                return true;
            });

    if (depot_center.isValid())
    {
        room *r = new room("workshop", "TradeDepot", depot_center - df::coord(2,2, 0), depot_center + df::coord(2, 2, 0));
        r->level = 0;
        r->layout.push_back(new_dig(tile_dig_designation::Ramp, -1, 1));
        r->layout.push_back(new_dig(tile_dig_designation::Ramp, -1, 2));
        r->layout.push_back(new_dig(tile_dig_designation::Ramp, -1, 3));
        rooms.push_back(r);
    }
    else
    {
        room *r = new room("workshop", "TradeDepot", df::coord(f.x - 7, f.y - 2, fort_entrance->max.z), df::coord(f.x - 3, f.y + 2, fort_entrance->max.z));
        r->level = 0;
        for (int16_t x = 0; x < 5; x++)
        {
            for (int16_t y = 0; y < 5; y++)
            {
                r->layout.push_back(new_construction(construction_type::Floor, x, y));
            }
        }
        rooms.push_back(r);
    }
    return CR_OK;
}

command_result Plan::setup_blueprint_stockpiles(color_ostream & out, df::coord f, std::vector<room *> entr)
{
    room *corridor_center0 = new room(f + df::coord(-1, -1, 0), f + df::coord(-1, 1, 0));
    corridor_center0->layout.push_back(new_door(-1, 0));
    corridor_center0->layout.push_back(new_door(-1, 2));
    corridor_center0->accesspath = entr;
    corridors.push_back(corridor_center0);

    room *corridor_center2 = new room(f + df::coord(1, -1, 0), f + df::coord(1, 1, 0));
    corridor_center2->layout.push_back(new_door(1, 0));
    corridor_center2->layout.push_back(new_door(1, 2));

    std::vector<std::string> types;
    types.push_back("food");
    types.push_back("furniture");
    types.push_back("wood");
    types.push_back("stone");
    types.push_back("refuse");
    types.push_back("animals");
    types.push_back("corpses");
    types.push_back("gems");
    types.push_back("finished_goods");
    types.push_back("cloth");
    types.push_back("bars_blocks");
    types.push_back("leather");
    types.push_back("ammo");
    types.push_back("armor");
    types.push_back("weapons");
    types.push_back("coins");
    auto type_it = types.begin();

    // TODO side stairs to workshop level ?
    for (int16_t dirx = -1; dirx <= 1; dirx += 2)
    {
        room *prev_corx = dirx < 0 ? corridor_center0 : corridor_center2;
        int16_t ocx = f.x + dirx * 3;
        for (int16_t dx = 1; dx <= 4; dx++)
        {
            // segments of the big central horizontal corridor
            int16_t cx = f.x + dirx * (8 * dx - 4);
            room *cor_x = new room(df::coord(ocx, f.y - 1, f.z), df::coord(cx + dirx, f.y + 1, f.z));
            cor_x->accesspath.push_back(prev_corx);
            corridors.push_back(cor_x);
            prev_corx = cor_x;
            ocx = cx + 2 * dirx;

            std::string t0 = *type_it++;
            std::string t1 = *type_it++;
            room *r0 = new room("stockpile", t0, df::coord(cx - 3, f.y - 4, f.z), df::coord(cx + 3, f.y - 3, f.z));
            room *r1 = new room("stockpile", t1, df::coord(cx - 3, f.y + 3, f.z), df::coord(cx + 3, f.y + 4, f.z));
            r0->level = 1;
            r1->level = 1;
            r0->accesspath.push_back(cor_x);
            r1->accesspath.push_back(cor_x);
            r0->layout.push_back(new_door(2, 2));
            r0->layout.push_back(new_door(4, 2));
            r1->layout.push_back(new_door(2, -1));
            r1->layout.push_back(new_door(4, -1));
            rooms.push_back(r0);
            rooms.push_back(r1);

            r0 = new room("stockpile", t0, df::coord(cx - 3, f.y - 11, f.z), df::coord(cx + 3, f.y - 5, f.z));
            r1 = new room("stockpile", t1, df::coord(cx - 3, f.y + 5, f.z), df::coord(cx + 3, f.y + 11, f.z));
            r0->level = 2;
            r1->level = 2;
            rooms.push_back(r0);
            rooms.push_back(r1);

            r0 = new room("stockpile", t0, df::coord(cx - 3, f.y - 20, f.z), df::coord(cx + 3, f.y - 12, f.z));
            r1 = new room("stockpile", t1, df::coord(cx - 3, f.y + 12, f.z), df::coord(cx + 3, f.y + 20, f.z));
            r0->level = 3;
            r1->level = 3;
            rooms.push_back(r0);
            rooms.push_back(r1);
        }
    }
    for (auto it = rooms.begin(); it != rooms.end(); it++)
    {
        room *r = *it;
        if (r->type == "stockpile" && r->subtype == "coins" &&
                r->level > 1)
        {
            r->subtype = "furniture";
            r->level += 2;
        }
    }

    return setup_blueprint_pitcage(out);
}

static furniture *new_hive_floor(int16_t x, int16_t y)
{
    furniture *f = new furniture();
    f->construction = construction_type::Floor;
    f->x = 3;
    f->y = 1;
    f->item = "hive";
    return f;
}

command_result Plan::setup_blueprint_pitcage(color_ostream & out)
{
    room *gpit = find_room("garbagepit");
    if (!gpit)
        return CR_OK;
    auto layout = [](room *r)
    {
        r->layout.push_back(new_construction(construction_type::UpStair, -1, 1, -10));
        for (int16_t z = -9; z <= -1; z++)
        {
            r->layout.push_back(new_construction(construction_type::UpDownStair, -1, 1, z));
        }
        r->layout.push_back(new_construction(construction_type::DownStair, -1, 1, 0));
        std::vector<int16_t> dxs;
        dxs.push_back(0);
        dxs.push_back(1);
        dxs.push_back(2);
        std::vector<int16_t> dys;
        dys.push_back(1);
        dys.push_back(0);
        dys.push_back(2);
        for (auto dx = dxs.begin(); dx != dxs.end(); dx++)
        {
            for (auto dy = dys.begin(); dy != dys.end(); dy++)
            {
                if (*dx == 1 && *dy == 1)
                {
                    r->layout.push_back(new_dig(tile_dig_designation::Channel, *dx, *dy));
                }
                else
                {
                    r->layout.push_back(new_construction(construction_type::Floor, *dx, *dy));
                }
            }
        }
    };

    room *r = new room("pitcage", "", gpit->min + df::coord(-1, -1, 10), gpit->min + df::coord(1, 1, 10));
    layout(r);
    r->layout.push_back(new_hive_floor(3, 1));
    rooms.push_back(r);

    room *stockpile = new room("stockpile", "animals", r->min, r->max);
    stockpile->level = 0;
    stockpile->stock_specific1 = true; // no empty cages
    stockpile->stock_specific2 = true; // no empty animal traps
    layout(stockpile);
    rooms.push_back(stockpile);

    return CR_OK;
}

static furniture *new_wall(int16_t x, int16_t y)
{
    furniture *f = new furniture();
    f->dig = tile_dig_designation::No;
    f->construction = construction_type::Wall;
    f->x = x;
    f->y = y;
    return f;
}

command_result Plan::setup_blueprint_utilities(color_ostream & out, df::coord f, std::vector<room *> entr)
{
    room *corridor_center0 = new room(f + df::coord(-1, -1, 0), f + df::coord(-1, 1, 0));
    corridor_center0->layout.push_back(new_door(-1, 0));
    corridor_center0->layout.push_back(new_door(-1, 2));
    corridor_center0->accesspath = entr;
    corridors.push_back(corridor_center0);

    room *corridor_center2 = new room(f + df::coord(1, -1, 0), f + df::coord(1, 1, 0));
    corridor_center2->layout.push_back(new_door(1, 0));
    corridor_center2->layout.push_back(new_door(1, 2));
    corridor_center2->accesspath = entr;
    corridors.push_back(corridor_center2);

    // dining halls
    int16_t ocx = f.x - 2;
    room *old_cor = corridor_center0;

    // temporary dininghall, for fort start
    room *tmp = new room("dininghall", "", f + df::coord(-4, -1, 0), f + df::coord(-3, 1, 0));
    tmp->temporary = true;
    for (int16_t dy = 0; dy <= 2; dy++)
    {
        tmp->layout.push_back(new_furniture_with_users("table", 0, dy));
        tmp->layout.push_back(new_furniture_with_users("chair", 1, dy));
    }
    tmp->layout[0]->makeroom = true;
    tmp->accesspath.push_back(old_cor);
    rooms.push_back(tmp);

    // dininghalls x 4 (54 users each)
    for (int16_t ax = 0; ax <= 1; ax++)
    {
        room *cor = new room(df::coord(ocx - 1, f.y - 1, f.z), df::coord(f.x - ax * 12 - 5, f.y + 1, f.z));
        cor->accesspath.push_back(old_cor);
        corridors.push_back(cor);
        ocx = f.x - ax * 12 - 4;
        old_cor = cor;

        std::vector<int16_t> dxs;
        dxs.push_back(5);
        dxs.push_back(4);
        dxs.push_back(6);
        dxs.push_back(3);
        dxs.push_back(7);
        dxs.push_back(2);
        dxs.push_back(8);
        dxs.push_back(1);
        dxs.push_back(9);
        for (int16_t dy = -1; dy <= 1; dy += 2)
        {
            room *dinner = new room("dininghall", "", df::coord(f.x - ax * 12 - 2 - 10, f.y + dy * 9, f.z), df::coord(f.x - ax * 12 - 2, f.y + dy * 3, f.z));
            dinner->layout.push_back(new_door(7, dy > 0 ? -1 : 7));
            dinner->layout.push_back(new_wall(2, 3));
            dinner->layout.push_back(new_wall(8, 3));
            for (auto dx = dxs.begin(); dx != dxs.end(); dx++)
            {
                for (int16_t sy = -1; sy <= 1; sy += 2)
                {
                    dinner->layout.push_back(new_furniture_with_users("table", *dx, 3 + dy * sy * 1, true));
                    dinner->layout.push_back(new_furniture_with_users("chair", *dx, 3 + dy * sy * 2, true));
                }
            }
            for (auto f = dinner->layout.begin(); f != dinner->layout.end(); f++)
            {
                if ((*f)->item == "table")
                {
                    (*f)->makeroom = true;
                    break;
                }
            }
            dinner->accesspath.push_back(cor);
            rooms.push_back(dinner);
        }
    }

    // tavern
    room *cor = new room(f + df::coord(-18, -1, 0), f + df::coord(-26, 1, 0));
    cor->accesspath.push_back(corridors.back());
    corridors.push_back(cor);

    df::coord tavern_center = f - df::coord(32, 0, 0);
    room *tavern = new room("location", "tavern", tavern_center - df::coord(4, 4, 0), tavern_center + df::coord(4, 4, 0));
    tavern->layout.push_back(new_door(9, 3));
    tavern->layout.push_back(new_door(9, 5));
    tavern->layout.push_back(new_furniture("chest", 8, 0));
    tavern->accesspath.push_back(cor);
    rooms.push_back(tavern);

    room *booze = new room("stockpile", "food", tavern_center + df::coord(-2, -4, 0), tavern_center + df::coord(3, -4, 0));
    booze->stock_disable.insert(stockpile_list::FoodMeat);
    booze->stock_disable.insert(stockpile_list::FoodFish);
    booze->stock_disable.insert(stockpile_list::FoodUnpreparedFish);
    booze->stock_disable.insert(stockpile_list::FoodEgg);
    booze->stock_disable.insert(stockpile_list::FoodPlants);
    booze->stock_disable.insert(stockpile_list::FoodCheesePlant);
    booze->stock_disable.insert(stockpile_list::FoodCheeseAnimal);
    booze->stock_disable.insert(stockpile_list::FoodSeeds);
    booze->stock_disable.insert(stockpile_list::FoodLeaves);
    booze->stock_disable.insert(stockpile_list::FoodMilledPlant);
    booze->stock_disable.insert(stockpile_list::FoodBoneMeal);
    booze->stock_disable.insert(stockpile_list::FoodFat);
    booze->stock_disable.insert(stockpile_list::FoodPaste);
    booze->stock_disable.insert(stockpile_list::FoodPressedMaterial);
    booze->stock_disable.insert(stockpile_list::FoodExtractPlant);
    booze->stock_disable.insert(stockpile_list::FoodExtractAnimal);
    booze->stock_disable.insert(stockpile_list::FoodMiscLiquid);
    booze->stock_specific1 = true; // no prepared food
    booze->workshop = tavern;
    booze->level = 0;
    tavern->accesspath.push_back(booze);
    rooms.push_back(booze);

    if (allow_ice)
    {
        ai->debug(out, "icy embark, no well");
        booze->min.x -= 2;
    }
    else
    {
        df::coord river = scan_river(out);
        if (river.isValid())
        {
            command_result res = setup_blueprint_cistern_fromsource(out, river, f, tavern);
            if (res != CR_OK)
                return res;
        }
        else
        {
            // TODO pool, pumps, etc
            ai->debug(out, "no river, no well");
            booze->min.x -= 2;
        }
    }

    // farm plots
    int16_t cx = f.x + 4 * 6; // end of workshop corridor (last ws door)
    int16_t cy = f.y;;
    int16_t cz = find_room("workshop", [](room *r) -> bool { return r->subtype == "Farmers"; })->min.z;
    room *ws_cor = new room(df::coord(f.x + 3, cy, cz), df::coord(cx + 1, cy, cz)); // ws_corr->accesspath ...
    corridors.push_back(ws_cor);
    room *farm_stairs = new room(df::coord(cx + 2, cy, cz), df::coord(cx + 2, cy, cz));
    farm_stairs->accesspath.push_back(ws_cor);
    corridors.push_back(farm_stairs);
    cx += 3;
    int16_t cz2 = cz;
    int32_t soilcnt = 0;
    for (int16_t z = cz; z < world->map.z_count; z++)
    {
        bool ok = true;
        int32_t scnt = 0;
        for (int16_t dx = -1; dx <= nrfarms * farm_w / 3; dx++)
        {
            for (int16_t dy = -3 * farm_h - farm_h + 1; dy <= 3 * farm_h + farm_h - 1; dy++)
            {
                df::tiletype *t = Maps::getTileType(cx + dx, cy + dy, z);
                if (!t || ENUM_ATTR(tiletype, shape, *t) != tiletype_shape::WALL)
                {
                    ok = false;
                    continue;
                }

                if (ENUM_ATTR(tiletype, material, *t) == tiletype_material::SOIL)
                {
                    scnt++;
                }
            }
        }

        if (ok && soilcnt < scnt)
        {
            soilcnt = scnt;
            cz2 = z;
        }
    }

    farm_stairs->max.z = cz2;
    cor = new room(df::coord(cx, cy, cz2), df::coord(cx + 1, cy, cz2));
    cor->accesspath.push_back(farm_stairs);
    corridors.push_back(cor);
    room *first_farm = nullptr;
    auto make_farms = [this, cor, cx, cy, cz2, &first_farm](int16_t dy, std::string st)
    {
        for (int16_t dx = 0; dx < nrfarms / 3; dx++)
        {
            for (int16_t ddy = 0; ddy < 3; ddy++)
            {
                room *r = new room("farmplot", st, df::coord(cx + farm_w * dx, cy + dy * 2 + dy * ddy * farm_h, cz2), df::coord(cx + farm_w * dx + farm_w - 1, cy + dy * (2 + farm_h - 1) + dy * ddy * farm_h, cz2));
                r->has_users = true;
                if (dx == 0 && ddy == 0)
                {
                    r->layout.push_back(new_door(1, dy > 0 ? -1 : farm_h));
                    r->accesspath.push_back(cor);
                }
                else
                {
                    r->accesspath.push_back(rooms.back());
                }
                rooms.push_back(r);
                if (first_farm == nullptr)
                {
                    first_farm = r;
                }
            }
        }
    };
    make_farms(-1, "food");
    make_farms(1, "cloth");

    // seeds stockpile
    room *r = new room("stockpile", "food", df::coord(cx + 2, cy, cz2), df::coord(cx + 4, cy, cz2));
    r->level = 0;
    r->stock_disable.insert(stockpile_list::FoodMeat);
    r->stock_disable.insert(stockpile_list::FoodFish);
    r->stock_disable.insert(stockpile_list::FoodUnpreparedFish);
    r->stock_disable.insert(stockpile_list::FoodEgg);
    r->stock_disable.insert(stockpile_list::FoodPlants);
    r->stock_disable.insert(stockpile_list::FoodDrinkPlant);
    r->stock_disable.insert(stockpile_list::FoodDrinkAnimal);
    r->stock_disable.insert(stockpile_list::FoodCheesePlant);
    r->stock_disable.insert(stockpile_list::FoodCheeseAnimal);
    r->stock_disable.insert(stockpile_list::FoodLeaves);
    r->stock_disable.insert(stockpile_list::FoodMilledPlant);
    r->stock_disable.insert(stockpile_list::FoodBoneMeal);
    r->stock_disable.insert(stockpile_list::FoodFat);
    r->stock_disable.insert(stockpile_list::FoodPaste);
    r->stock_disable.insert(stockpile_list::FoodPressedMaterial);
    r->stock_disable.insert(stockpile_list::FoodExtractPlant);
    r->stock_disable.insert(stockpile_list::FoodExtractAnimal);
    r->stock_disable.insert(stockpile_list::FoodMiscLiquid);
    r->stock_specific1 = true; // no prepared meals
    r->workshop = first_farm;
    r->accesspath.push_back(cor);
    rooms.push_back(r);

    // garbage dump
    // TODO ensure flat space, no pools/tree, ...
    df::coord tile(cx + 5, cy, cz);
    r = new room("garbagedump", "", tile, tile);
    tile = spiral_search(tile, [this, f, cx, cz](df::coord t) -> bool
            {
                t = surface_tile_at(t.x, t.y);
                if (!t.isValid())
                    return false;
                if (t.x < cx + 5 && (t.z <= cz + 2 || t.x <= f.x + 5))
                    return false;
                if (!map_tile_in_rock(t + df::coord(0, 0, -1)))
                    return false;
                if (!map_tile_in_rock(t + df::coord(2, 0, -1)))
                    return false;
                if (ENUM_ATTR(tiletype_shape, basic_shape, ENUM_ATTR(tiletype, shape, *Maps::getTileType(t))) != tiletype_shape_basic::Floor)
                    return false;
                if (ENUM_ATTR(tiletype_shape, basic_shape, ENUM_ATTR(tiletype, shape, *Maps::getTileType(t + df::coord(1, 0, 0)))) != tiletype_shape_basic::Floor)
                    return false;
                if (ENUM_ATTR(tiletype_shape, basic_shape, ENUM_ATTR(tiletype, shape, *Maps::getTileType(t + df::coord(2, 0, 0)))) != tiletype_shape_basic::Floor)
                    return false;
                if (ENUM_ATTR(tiletype_shape, basic_shape, ENUM_ATTR(tiletype, shape, *Maps::getTileType(t + df::coord(1, 1, 0)))) == tiletype_shape_basic::Floor)
                    return true;
                if (ENUM_ATTR(tiletype_shape, basic_shape, ENUM_ATTR(tiletype, shape, *Maps::getTileType(t + df::coord(1, -1, 0)))) == tiletype_shape_basic::Floor)
                    return true;
                return false;
            });
    tile = surface_tile_at(tile.x, tile.y);
    r->min = r->max = tile;
    rooms.push_back(r);
    r = new room("garbagepit", "", tile + df::coord(1, 0, 0), tile + df::coord(2, 0, 0));
    rooms.push_back(r);

    // infirmary
    old_cor = corridor_center2;
    cor = new room(f + df::coord(3, -1, 0), f + df::coord(5, 1, 0));
    cor->accesspath.push_back(old_cor);
    corridors.push_back(cor);
    old_cor = cor;

    room *infirmary = new room("infirmary", "", f + df::coord(2, -3, 0), f + df::coord(6, -7, 0));
    infirmary->layout.push_back(new_door(3, 5));
    infirmary->layout.push_back(new_furniture("bed", 0, 1));
    infirmary->layout.push_back(new_furniture("table", 1, 1));
    infirmary->layout.push_back(new_furniture("bed", 2, 1));
    infirmary->layout.push_back(new_furniture("traction_bench", 0, 2));
    infirmary->layout.push_back(new_furniture("traction_bench", 2, 2));
    infirmary->layout.push_back(new_furniture("bed", 0, 3));
    infirmary->layout.push_back(new_furniture("table", 1, 3));
    infirmary->layout.push_back(new_furniture("bed", 2, 3));
    infirmary->layout.push_back(new_furniture("chest", 4, 1));
    infirmary->layout.push_back(new_furniture("chest", 4, 2));
    infirmary->layout.push_back(new_furniture("chest", 4, 3));
    infirmary->accesspath.push_back(cor);
    rooms.push_back(infirmary);

    // cemetary lots (160 spots)
    cor = new room(f + df::coord(6, -1, 0), f + df::coord(14, 1, 0));
    cor->accesspath.push_back(old_cor);
    corridors.push_back(cor);
    old_cor = cor;

    for (int16_t ry = 0; ry < 500; ry++)
    {
        bool stop = false;
        for (int16_t tx = -1; !stop && tx <= 19; tx++)
        {
            for (int16_t ty = -1; !stop && ty <= 4; ty++)
            {
                df::tiletype *t = Maps::getTileType(f + df::coord(10 + tx, -3 - 3 * ry - ty, 0));
                if (!t || ENUM_ATTR(tiletype_shape, basic_shape,
                            ENUM_ATTR(tiletype, shape, *t)) !=
                        tiletype_shape_basic::Wall)
                {
                    stop = true;
                }
            }
        }
        if (stop)
            break;

        for (int16_t rrx = 0; rrx < 2; rrx++)
        {
            for (int16_t rx = 0; rx < 2; rx++)
            {
                df::coord o = f + df::coord(10 + 5 * rx + 9 * rrx, -3 - 3 * ry, 0);
                room *cemetary = new room("cemetary", "", o, o + df::coord(4, -3, 0));
                for (int16_t dx = 0; dx < 4; dx++)
                {
                    for (int16_t dy = 0; dy < 2; dy++)
                    {
                        cemetary->layout.push_back(new_furniture_with_users("coffin", dx + 1 - rx, dy + 1, true));
                    }
                }
                if (rx == 0 && ry == 0 && rrx == 0)
                {
                    cemetary->layout.push_back(new_door(4, 4));
                    cemetary->accesspath.push_back(cor);
                }
                rooms.push_back(cemetary);
            }
        }
    }

    // barracks
    // 8 dwarf per squad, 20% pop => 40 soldiers for 200 dwarves => 5 barracks
    old_cor = corridor_center2;
    int16_t oldcx = old_cor->max.x + 2; // door
    for (int16_t rx = 0; rx < 4; rx++)
    {
        cor = new room(df::coord(oldcx, f.y - 1, f.z), df::coord(f.x + 5 + 10 * rx, f.y + 1, f.z));
        cor->accesspath.push_back(old_cor);
        corridors.push_back(cor);
        old_cor = cor;
        oldcx = cor->max.x + 1;

        for (int16_t ry = -1; ry <= 1; ry += 2)
        {
            if (ry == -1 && rx < 3) // infirmary/cemetary
                continue;

            room *barracks = new room("barracks", "", df::coord(f.x + 2 + 10 * rx, f.y + 3 * ry, f.z), df::coord(f.x + 2 + 10 * rx + 6, f.y + 10 * ry, f.z));
            barracks->layout.push_back(new_door(3, ry > 0 ? -1 : 8));
            for (int16_t dy_ = 0; dy_ < 8; dy_++)
            {
                int16_t dy = ry < 0 ? 7 - dy_ : dy_;
                barracks->layout.push_back(new_furniture_with_users("armorstand", 5, dy, true));
                barracks->layout.push_back(new_furniture_with_users("bed", 6, dy, true));
                barracks->layout.push_back(new_furniture_with_users("cabinet", 0, dy, true));
                barracks->layout.push_back(new_furniture_with_users("chest", 1, dy, true));
            }
            barracks->layout.push_back(new_furniture_with_users("weaponrack", 4, ry > 0 ? 7 : 0, false));
            barracks->layout.back()->makeroom = true;
            barracks->layout.push_back(new_furniture_with_users("weaponrack", 2, ry > 0 ? 7 : 0, true));
            barracks->layout.push_back(new_furniture_with_users("archerytarget", 3, ry > 0 ? 7 : 0, true));
            barracks->accesspath.push_back(cor);
            rooms.push_back(barracks);
        }
    }

    ai->debug(out, "finished interior utilities");
    command_result res;
    res = setup_blueprint_pastures(out);
    if (res != CR_OK)
        return res;
    ai->debug(out, "finished pastures");
    res = setup_blueprint_outdoor_farms(out, nrfarms * 2);
    if (res != CR_OK)
        return res;
    return CR_OK;
    ai->debug(out, "finished outdoor farms");
}

static furniture *new_well(int16_t x, int16_t y)
{
    furniture *f = new_furniture("well", x, y);
    f->dig = tile_dig_designation::Channel;
    return f;
}

static furniture *new_cistern_lever(int16_t x, int16_t y, const std::string & way)
{
    furniture *f = new_furniture("trap", x, y);
    f->subtype = "lever";
    f->way = way;
    return f;
}

static furniture *new_cistern_floodgate(int16_t x, int16_t y, const std::string & way, bool ignore = false)
{
    furniture *f = new_furniture("floodgate", x, y);
    f->way = way;
    f->ignore = ignore;
    return f;
}

command_result Plan::setup_blueprint_cistern_fromsource(color_ostream & out, df::coord src, df::coord f, room *tavern)
{
    // TODO dynamic layout, at least move the well/cistern on the side of the river
    // TODO scan for solid ground for all this

    // add a well to the tavern
    tavern->layout.push_back(new_well(4, 4));
    tavern->layout.push_back(new_cistern_lever(1, 0, "out"));
    tavern->layout.push_back(new_cistern_lever(0, 0, "in"));

    df::coord c = tavern->pos();

    // water cistern under the well (in the hole of bedroom blueprint)
    std::vector<room *> cist_cors = find_corridor_tosurface(out, c - df::coord(8, 0, 0));
    cist_cors.at(0)->min.z -= 3;

    room *cistern = new room("cistern", "well", c + df::coord(-7, -1, -3), c + df::coord(1, 1, -1));
    cistern->accesspath.push_back(cist_cors.at(0));

    // handle low rivers / high mountain forts
    if (f.z > src.z)
        f.z = c.z = src.z;
    // should be fine with cistern auto-fill checks
    if (cistern->min.z > f.z)
        cistern->min.z = f.z;

    // staging reservoir to fill the cistern, one z-level at a time
    // should have capacity = 1 cistern level @7/7 + itself @1/7 (rounded up)
    //  cistern is 9x3 + 1 (stairs)
    //  reserve is 5x7 (can fill cistern 7/7 + itself 1/7 + 14 spare
    room *reserve = new room("cistern", "reserve", c + df::coord(-10, -3, 0), c + df::coord(-14, 3, 0));
    reserve->layout.push_back(new_cistern_floodgate(-1, 3, "in", false));
    reserve->layout.push_back(new_cistern_floodgate(5, 3, "out", true));
    reserve->accesspath.push_back(cist_cors.at(0));

    // cisterns are dug in order
    // try to dig reserve first, so we have liquid water even if river freezes
    rooms.push_back(reserve);
    rooms.push_back(cistern);

    // link the cistern reserve to the water source

    // trivial walk of the river tiles to find a spot closer to dst
    auto move_river = [this, &src](df::coord dst)
    {
        auto distance = [](df::coord a, df::coord b) -> int16_t
        {
            df::coord d = a - b;
            return d.x * d.x + d.y * d.y + d.z * d.z;
        };

        df::coord nsrc = src;
        for (size_t i = 0; i < 500; i++)
        {
            if (!nsrc.isValid())
                break;
            src = nsrc;
            int16_t dist = distance(src, dst);
            nsrc = spiral_search(src, 1, 1, [distance, dist, dst](df::coord t) -> bool
                    {
                        if (distance(t, dst) > dist)
                            return false;
                        return Maps::getTileDesignation(t)->bits.feature_local;
                    });
        }
    };

    // 1st end: reservoir input
    df::coord p1 = c - df::coord(16, 0, 0);
    move_river(p1);
    ai->debug(out, stl_sprintf("cistern: reserve/in (%d, %d, %d), river (%d, %d, %d)", p1.x, p1.y, p1.z, src.x, src.y, src.z));

    df::coord p = p1;
    room *r = reserve;
    // XXX hardcoded layout again
    if (src.x > p1.x)
    {
        // the tunnel should walk around other blueprint rooms
        df::coord p2 = p1 + df::coord(0, src.y >= p1.y ? 26 : -26, 0);
        room *cor = new room(p1, p2);
        corridors.push_back(cor);
        reserve->accesspath.push_back(cor);
        move_river(p2);
        p = p2;
        r = cor;
    }

    std::vector<room *> up = find_corridor_tosurface(out, p);
    r->accesspath.push_back(up.at(0));

    df::coord dst = up.back()->max - df::coord(0, 0, 2);
    if (src.z <= dst.z)
        dst.z = src.z - 1;
    move_river(dst);

    if (std::abs(dst.x - src.x) > 1)
    {
        df::coord p3(src.x, dst.y, dst.z);
        move_river(p3);
    }

    // find safe tile near the river and a tile to channel
    df::coord channel;
    channel.clear();
    df::coord output = spiral_search(src, [this, &channel](df::coord t) -> bool
            {
                if (!map_tile_in_rock(t))
                {
                    return false;
                }
                channel = Plan::spiral_search(t, 1, 1, [](df::coord t) -> bool
                        {
                            return Plan::spiral_search(t, 1, 1, [](df::coord t) -> bool
                                    {
                                        return Maps::getTileDesignation(t)->bits.feature_local;
                                    }).isValid();
                        });
                if (!channel.isValid())
                {
                    channel = Plan::spiral_search(t, 1, 1, [](df::coord t) -> bool
                            {
                                return Plan::spiral_search(t, 1, 1, [](df::coord t) -> bool
                                        {
                                            return Maps::getTileDesignation(t)->bits.flow_size != 0 ||
                                                    ENUM_ATTR(tiletype, material, *Maps::getTileType(t)) == tiletype_material::FROZEN_LIQUID;
                                        }).isValid();
                            });
                }
                return channel.isValid();
            });

    if (channel.isValid())
    {
        ai->debug(out, stl_sprintf("cistern: out(%d, %d, %d), channel_enable (%d, %d, %d)", output.x, output.y, output.z, channel.x, channel.y, channel.z));
    }

    // TODO check that 'channel' is easily channelable (eg river in a hole)

    int16_t y_x = 0;
    if (dst.x - 1 > output.x)
    {
        room *cor = new room(df::coord(dst.x - 1, dst.y, dst.z), df::coord(output.x + 1, dst.y, dst.z));
        corridors.push_back(cor);
        r->accesspath.push_back(cor);
        r = cor;
        y_x = 1;
    }
    else if (output.x - 1 > dst.x)
    {
        room *cor = new room(df::coord(dst.x + 1, dst.y, dst.z), df::coord(output.x - 1, dst.y, dst.z));
        corridors.push_back(cor);
        r->accesspath.push_back(cor);
        r = cor;
        y_x = -1;
    }

    if (dst.y - 1 > output.y)
    {
        room *cor = new room(df::coord(output.x + y_x, dst.y + 1, dst.z), df::coord(output.x + y_x, output.y, dst.z));
        corridors.push_back(cor);
        r->accesspath.push_back(cor);
    }
    else if (output.y - 1 > dst.y)
    {
        room *cor = new room(df::coord(output.x + y_x, dst.y - 1, dst.z), df::coord(output.x + y_x, output.y, dst.z));
        corridors.push_back(cor);
        r->accesspath.push_back(cor);
    }

    up = find_corridor_tosurface(out, df::coord(output, dst.z));
    r->accesspath.push_back(up.at(0));

    reserve->channel_enable = channel;
    return CR_OK;
}

// scan for 11x11 flat areas with grass
command_result Plan::setup_blueprint_pastures(color_ostream & out)
{
    size_t want = 36;
    spiral_search(fort_entrance->pos(), std::max(world->map.x_count, world->map.y_count), 10, 5, [this, &out, &want](df::coord _t) -> bool
            {
                df::coord sf = surface_tile_at(_t.x, _t.y);
                if (!sf.isValid())
                    return false;
                size_t floortile = 0;
                size_t grasstile = 0;
                bool ok = true;
                for (int16_t dx = -5; ok && dx <= 5; dx++)
                {
                    for (int16_t dy = -5; ok && dy <= 5; dy++)
                    {
                        df::coord t = sf + df::coord(dx, dy, 0);
                        df::tiletype *tt = Maps::getTileType(t);
                        if (!tt)
                        {
                            ok = false;
                            continue;
                        }
                        if (ENUM_ATTR(tiletype_shape, basic_shape, ENUM_ATTR(tiletype, shape, *tt)) != tiletype_shape_basic::Floor && ENUM_ATTR(tiletype, material, *tt) != tiletype_material::TREE)
                        {
                            ok = false;
                            continue;
                        }
                        if (Maps::getTileDesignation(t)->bits.flow_size != 0)
                        {
                            continue;
                        }
                        if (ENUM_ATTR(tiletype, material, *tt) == tiletype_material::FROZEN_LIQUID)
                        {
                            continue;
                        }
                        floortile++;
                        auto & events = Maps::getTileBlock(t)->block_events;
                        for (auto be = events.begin(); be != events.end(); be++)
                        {
                            df::block_square_event_grassst *grass = virtual_cast<df::block_square_event_grassst>(*be);
                            if (grass && grass->amount[t.x & 0xf][t.y & 0xf] > 0)
                            {
                                grasstile++;
                                break;
                            }
                        }
                    }
                }
                if (ok && floortile >= 9 * 9 && grasstile >= 8 * 8)
                {
                    room *r = new room("pasture", "", sf - df::coord(5, 5, 0), sf + df::coord(5, 5, 0));
                    r->has_users = true;
                    rooms.push_back(r);
                    want--;
                }
                return want == 0;
            });
    return CR_OK;
}

// scan for 3x3 flat areas with soil
command_result Plan::setup_blueprint_outdoor_farms(color_ostream & out, size_t want)
{
    spiral_search(fort_entrance->pos(), std::max(world->map.x_count, world->map.y_count), 9, 3, [this, &out, &want](df::coord _t) -> bool
            {
                df::coord sf = surface_tile_at(_t.x, _t.y);
                if (!sf.isValid())
                    return false;
                df::tile_designation sd = *Maps::getTileDesignation(sf);
                for (int16_t dx = -1; dx <= 1; dx++)
                {
                    for (int16_t dy = -1; dy <= 1; dy++)
                    {
                        df::coord t = sf + df::coord(dx, dy, 0);
                        df::tile_designation *td = Maps::getTileDesignation(t);
                        if (!td)
                        {
                            return false;
                        }
                        if (sd.bits.subterranean != td->bits.subterranean)
                        {
                            return false;
                        }
                        if (!sd.bits.subterranean &&
                                sd.bits.biome != td->bits.biome)
                        {
                            return false;
                        }
                        df::tiletype tt = *Maps::getTileType(t);
                        if (ENUM_ATTR(tiletype_shape, basic_shape, ENUM_ATTR(tiletype, shape, tt)) != tiletype_shape_basic::Floor)
                        {
                            return false;
                        }
                        if (td->bits.flow_size != 0)
                        {
                            return false;
                        }
                        if (!farm_allowed_materials.set.count(ENUM_ATTR(tiletype, material, tt)))
                        {
                            return false;
                        }
                    }
                }
                room *r = new room("farmplot", want % 2 == 0 ? "food" : "cloth", sf - df::coord(1, 1, 0), sf + df::coord(1, 1, 0));
                r->has_users = true;
                r->outdoor = true;
                want--;
                return want == 0;
            });
    return CR_OK;
}

command_result Plan::setup_blueprint_bedrooms(color_ostream & out, df::coord f, std::vector<room *> entr)
{
    room *corridor_center0 = new room(f + df::coord(-1, -1, 0), f + df::coord(-1, 1, 0));
    corridor_center0->layout.push_back(new_door(-1, 0));
    corridor_center0->layout.push_back(new_door(-1, 2));
    corridor_center0->accesspath = entr;
    corridors.push_back(corridor_center0);

    room *corridor_center2 = new room(f + df::coord(1, -1, 0), f + df::coord(1, 1, 0));
    corridor_center2->layout.push_back(new_door(1, 0));
    corridor_center2->layout.push_back(new_door(1, 2));
    corridor_center2->accesspath = entr;
    corridors.push_back(corridor_center2);

    for (int16_t dirx = -1; dirx <= 1; dirx += 2)
    {
        room *prev_corx = dirx < 0 ? corridor_center0 : corridor_center2;
        int16_t ocx = f.x + dirx * 3;
        for (int16_t dx = 1; dx <= 3; dx++)
        {
            // segments of the big central horizontal corridor
            int16_t cx = f.x + dirx * (9 * dx - 4);
            room *cor_x = new room(df::coord(ocx, f.y - 1, f.z), df::coord(cx, f.y + 1, f.z));
            cor_x->accesspath.push_back(prev_corx);
            corridors.push_back(cor_x);
            prev_corx = cor_x;
            ocx = cx + dirx;

            for (int16_t diry = -1; diry <= 1; diry += 2)
            {
                room *prev_cory = cor_x;
                int16_t ocy = f.y + diry * 2;
                for (int16_t dy = 1; dy <= 6; dy++)
                {
                    int16_t cy = f.y + diry * 3 * dy;
                    room *cor_y = new room(df::coord(cx, ocy, f.z), df::coord(cx - dirx, cy, f.z));
                    cor_y->accesspath.push_back(prev_cory);
                    corridors.push_back(cor_y);
                    prev_cory = cor_y;
                    ocy = cy + diry;

                    auto bedroom = [this, cx, diry, cor_y](room *r)
                    {
                        r->accesspath.push_back(cor_y);
                        r->layout.push_back(new_furniture("bed", r->min.x < cx ? 0 : 1, diry < 0 ? 1 : 0));
                        r->layout.back()->makeroom = true;
                        r->layout.push_back(new_furniture("cabinet", r->min.x < cx ? 0 : 1, diry < 0 ? 0 : 1));
                        r->layout.back()->ignore = true;
                        r->layout.push_back(new_furniture("chest", r->min.x < cx ? 1 : 0, diry < 0 ? 0 : 1));
                        r->layout.back()->ignore = true;
                        r->layout.push_back(new_door(r->min.x < cx ? 2 : -1, diry < 0 ? 1 : 0));
                        rooms.push_back(r);
                    };
                    bedroom(new room("bedroom", "", df::coord(cx - dirx * 4, cy, f.z), df::coord(cx - dirx * 3, cy + diry, f.z)));
                    bedroom(new room("bedroom", "", df::coord(cx + dirx * 2, cy, f.z), df::coord(cx + dirx * 3, cy + diry, f.z)));
                }
            }
        }

        // noble suites
        int16_t cx = f.x + dirx * (9 * 3 - 4 + 6);
        room *cor_x = new room(df::coord(ocx, f.y - 1, f.z), df::coord(cx, f.y + 1, f.z));
        room *cor_x2 = new room(df::coord(ocx - dirx, f.y, f.z), df::coord(f.x + dirx * 3, f.y, f.z));
        cor_x->accesspath.push_back(cor_x2);
        cor_x2->accesspath.push_back(dirx < 0 ? corridor_center0 : corridor_center2);
        corridors.push_back(cor_x);
        corridors.push_back(cor_x2);

        for (int16_t diry = -1; diry <= 1; diry += 2)
        {
            noblesuite++;

            room *r = new room("nobleroom", "office", df::coord(cx - 1, f.y + diry * 3, f.z), df::coord(cx + 1, f.y + diry * 5, f.z));
            r->noblesuite = noblesuite;
            r->accesspath.push_back(cor_x);
            r->layout.push_back(new_furniture("chair", 1, 1));
            r->layout.back()->makeroom = true;
            r->layout.push_back(new_furniture("chair", 1 - dirx, 1));
            r->layout.push_back(new_furniture("chest", 1 + dirx, 0));
            r->layout.back()->ignore = true;
            r->layout.push_back(new_furniture("cabinet", 1 + dirx, 2));
            r->layout.back()->ignore = true;
            r->layout.push_back(new_door(1, 1 - 2 * diry));
            rooms.push_back(r);

            r = new room("nobleroom", "bedroom", df::coord(cx - 1, f.y + diry * 7, f.z), df::coord(cx + 1, f.y + diry * 9, f.z));
            r->noblesuite = noblesuite;
            r->layout.push_back(new_furniture("bed", 1, 1));
            r->layout.back()->makeroom = true;
            r->layout.push_back(new_furniture("armorstand", 1 - dirx, 0));
            r->layout.back()->ignore = true;
            r->layout.push_back(new_furniture("weaponrack", 1 - dirx, 2));
            r->layout.back()->ignore = true;
            r->layout.push_back(new_door(1, 1 - 2 * diry, true));
            r->accesspath.push_back(rooms.back());
            rooms.push_back(r);

            r = new room("nobleroom", "diningroom", df::coord(cx - 1, f.y + diry * 11, f.z), df::coord(cx + 1, f.y + diry * 13, f.z));
            r->noblesuite = noblesuite;
            r->layout.push_back(new_furniture("table", 1, 1));
            r->layout.back()->makeroom = true;
            r->layout.push_back(new_furniture("chair", 1 + dirx, 1));
            r->layout.push_back(new_furniture("cabinet", 1 - dirx, 0));
            r->layout.back()->ignore = true;
            r->layout.push_back(new_furniture("chest", 1 - dirx, 2));
            r->layout.back()->ignore = true;
            r->layout.push_back(new_door(1, 1 - 2 * diry, true));
            r->accesspath.push_back(rooms.back());
            rooms.push_back(r);

            r = new room("nobleroom", "tomb", df::coord(cx - 1, f.y + diry * 15, f.z), df::coord(cx + 1, f.y + diry * 17, f.z));
            r->noblesuite = noblesuite;
            r->layout.push_back(new_furniture("coffin", 1, 1));
            r->layout.back()->makeroom = true;
            r->layout.push_back(new_door(1, 1 - 2 * diry, true));
            r->accesspath.push_back(rooms.back());
            rooms.push_back(r);
        }
    }
    return CR_OK;
}

static int16_t setup_outdoor_gathering_zones_counters[3];
static std::map<int16_t, std::set<df::coord2d>> setup_outdoor_gathering_zones_ground;

command_result Plan::setup_outdoor_gathering_zones(color_ostream & out)
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
                        bld->room.extents = new uint8_t[w * h]();
                        bld->room.x = x;
                        bld->room.y = y;
                        bld->room.width = w;
                        bld->room.height = h;
                        for (int16_t dx = 0; dx < w; dx++)
                        {
                            for (int16_t dy = 0; dy < h; dy++)
                            {
                                bld->room.extents[dx + w * dy] = g->second.count(df::coord2d(dx, dy)) ? 1 : 0;
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
                            ai->debug(out, "plan setup_outdoor_gathering_zones finished");
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
    df::coord target;
    for (; !wall.isValid() && z > 0; z--)
    {
        ai->debug(out, stl_sprintf("outpost: searching z-level %d", z));
        for (int16_t x = 0; !wall.isValid() && x < world->map.x_count; x++)
        {
            for (int16_t y = 0; !wall.isValid() && y < world->map.y_count; y++)
            {
                df::coord t(x, y, z);
                if (!map_tile_in_rock(t))
                    continue;
                // find a floor next to the wall
                target = spiral_search(t, 2, 2, [this](df::coord _t) -> bool
                        {
                            return map_tile_cavernfloor(_t);
                        });
                if (target.isValid())
                    wall = t;
            }
        }
    }
    if (!wall.isValid())
    {
        ai->debug(out, "outpost: could not find a cavern wall tile");
        return CR_FAILURE;
    }

    room *r = new room("outpost", "cavern", target, target);

    int16_t y_x = 0;
    if (wall.x - 1 > target.x)
    {
        room *cor = new room(df::coord(wall.x + 1, wall.y, wall.z), df::coord(target.x - 1, wall.y, wall.z));
        corridors.push_back(cor);
        r->accesspath.push_back(cor);
        y_x = 1;
    }
    else if (target.x - 1 > wall.x)
    {
        room *cor = new room(df::coord(wall.x - 1, wall.y, wall.z), df::coord(target.x + 1, wall.y, wall.z));
        corridors.push_back(cor);
        r->accesspath.push_back(cor);
        y_x = -1;
    }

    if (wall.y - 1 > target.y)
    {
        room *cor = new room(df::coord(target.x + y_x, wall.y + 1, wall.z), df::coord(target.x + y_x, target.y, wall.z));
        corridors.push_back(cor);
        r->accesspath.push_back(cor);
    }
    else if (target.y - 1 > wall.y)
    {
        room *cor = new room(df::coord(target.x + y_x, wall.y - 1, wall.z), df::coord(target.x + y_x, target.y, wall.z));
        corridors.push_back(cor);
        r->accesspath.push_back(cor);
    }

    std::vector<room *> up = find_corridor_tosurface(out, wall);
    r->accesspath.push_back(up.at(0));

    ai->debug(out, stl_sprintf("outpost: wall (%d, %d, %d)", wall.x, wall.y, wall.z));
    ai->debug(out, stl_sprintf("outpost: target (%d, %d, %d)", target.x, target.y, target.z));
    ai->debug(out, stl_sprintf("outpost: up (%d, %d, %d)", up.back()->max.x, up.back()->max.y, up.back()->max.z));

    rooms.push_back(r);

    return CR_OK;
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
            if (tm != tiletype_material::STONE && tm != tiletype_material::MINERAL && tm != tiletype_material::SOIL && tm != tiletype_material::ROOT && (allow_ice || tm != tiletype_material::FROZEN_LIQUID))
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

// create a new Corridor from origin to surface, through rock
// may create multiple chunks to avoid obstacles, all parts are added to corridors
// returns an array of Corridors, 1st = origin, last = surface
std::vector<room *> Plan::find_corridor_tosurface(color_ostream & out, df::coord origin)
{
    std::vector<room *> cors;
    for (;;)
    {
        room *cor = new room(origin, origin);
        if (!cors.empty())
        {
            cors.back()->accesspath.push_back(cor);
        }
        cors.push_back(cor);

        while (map_tile_in_rock(cor->max) && !map_tile_intersects_room(cor->max + df::coord(0, 0, 1)))
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

        df::coord out2 = spiral_search(cor->max, [this](df::coord t) -> bool
                {
                    while (map_tile_in_rock(t))
                    {
                        t.z++;
                    }

                    df::tiletype tt = *Maps::getTileType(t);
                    df::tiletype_shape_basic sb = ENUM_ATTR(tiletype_shape, basic_shape, ENUM_ATTR(tiletype, shape, tt));
                    df::tile_designation td = *Maps::getTileDesignation(t);

                    return (sb == tiletype_shape_basic::Ramp || sb == tiletype_shape_basic::Floor) &&
                            ENUM_ATTR(tiletype, material, tt) != tiletype_material::TREE &&
                            td.bits.flow_size == 0 &&
                            !td.bits.hidden &&
                            !map_tile_intersects_room(t);
                });

        if (Maps::getTileDesignation(cor->max)->bits.flow_size > 0)
        {
            // damp stone located
            cor->max.z--;
            out2.z--;
        }
        cor->max.z--;
        out2.z--;

        int16_t y_x = cor->max.x;
        if (cor->max.x - 1 > out2.x)
        {
            cor = new room(df::coord(out2.x - 1, out2.y, out2.z), df::coord(cor->max.x + 1, out2.y, out2.z));
            cors.back()->accesspath.push_back(cor);
            cors.push_back(cor);
            y_x++;
        }
        else if (out2.x - 1 > cor->max.x)
        {
            cor = new room(df::coord(out2.x + 1, out2.y, out2.z), df::coord(cor->max.x - 1, out2.y, out2.z));
            cors.back()->accesspath.push_back(cor);
            cors.push_back(cor);
            y_x--;
        }

        if (cor->max.y - 1 > out2.y)
        {
            cor = new room(df::coord(y_x, out2.y - 1, out2.z), df::coord(y_x, cor->max.y, out2.z));
            cors.back()->accesspath.push_back(cor);
            cors.push_back(cor);
        }
        else if (out2.y - 1 > cor->max.y)
        {
            cor = new room(df::coord(y_x, out2.y + 1, out2.z), df::coord(y_x, cor->max.y, out2.z));
            cors.back()->accesspath.push_back(cor);
            cors.push_back(cor);
        }

        if (origin == out2)
        {
            ai->debug(out, stl_sprintf("[ERROR] find_corridor_tosurface: loop: %d, %d, %d", origin.x, origin.y, origin.z));
            break;
        }
        ai->debug(out, stl_sprintf("find_corridor_tosurface: %d, %d, %d -> %d, %d, %d", origin.x, origin.y, origin.z, out2.x, out2.y, out2.z));

        origin = out2;
    }
    corridors.insert(corridors.end(), cors.begin(), cors.end());
    return cors;
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

std::string Plan::status()
{
    std::map<std::string, size_t> task_count;
    std::map<std::string, size_t> furnishing;
    for (auto t = tasks.begin(); t != tasks.end(); t++)
    {
        task_count[(*t)->type]++;
        if ((*t)->type == "furnish" && !(*t)->f->item.empty())
        {
            furnishing[(*t)->f->item]++;
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
        s << ", digging: " << describe_room(t->r);
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
        s << f->first << ": " << f->second;
    }
    return s.str();
}

void Plan::categorize_all()
{
    room_category.clear();
    for (auto r = rooms.begin(); r != rooms.end(); r++)
    {
        room_category[(*r)->type].push_back(*r);
    }

    if (room_category.count("stockpile"))
    {
        auto & stockpiles = room_category.at("stockpile");
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

std::string Plan::describe_room(room *r)
{
    std::ostringstream s;
    s << r->type;
    if (!r->subtype.empty())
    {
        s << " (" << r->subtype << ")";
    }

    if (!r->comment.empty())
    {
        s << " (" << r->comment << ")";
    }

    if (df::unit *u = df::unit::find(r->owner))
    {
        s << " (owned by " << AI::describe_unit(u) << ")";
    }

    if (r->noblesuite != -1)
    {
        s << " (noble suite " << r->noblesuite << ")";
    }

    if (df::squad *squad = df::squad::find(r->squad_id))
    {
        s << " (used by " << AI::describe_name(squad->name, true) << ")";
    }

    if (r->level != -1)
    {
        s << " (level " << r->level << ")";
    }

    if (r->workshop)
    {
        s << " (" << describe_room(r->workshop) << ")";
    }

    if (r->has_users)
    {
        s << " (" << r->users.size() << " users)";
    }

    s << " (" << r->status << ")";

    return s.str();
}

room *Plan::find_room(std::string type)
{
    if (room_category.empty())
    {
        for (auto r = rooms.begin(); r != rooms.end(); r++)
        {
            if ((*r)->type == type)
            {
                return *r;
            }
        }
        return nullptr;
    }

    if (room_category.count(type))
    {
        return room_category.at(type).front();
    }

    return nullptr;
}

room *Plan::find_room(std::string type, std::function<bool(room *)> b)
{
    if (room_category.empty())
    {
        for (auto r = rooms.begin(); r != rooms.end(); r++)
        {
            if ((*r)->type == type && b(*r))
            {
                return *r;
            }
        }
        return nullptr;
    }

    if (room_category.count(type))
    {
        auto & rs = room_category.at(type);
        for (auto r = rs.begin(); r != rs.end(); r++)
        {
            if (b(*r))
            {
                return *r;
            }
        }
    }

    return nullptr;
}

bool Plan::map_tile_intersects_room(df::coord t)
{
    for (auto r = rooms.begin(); r != rooms.end(); r++)
    {
        if ((*r)->safe_include(t))
            return true;
    }
    for (auto r = corridors.begin(); r != corridors.end(); r++)
    {
        if ((*r)->safe_include(t))
            return true;
    }
    return false;
}

df::coord Plan::find_tree_base(df::coord t)
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
            return (*tree)->pos;
        }
    }

    for (auto tree = world->plants.tree_wet.begin(); tree != world->plants.tree_wet.end(); tree++)
    {
        if (find(*tree))
        {
            return (*tree)->pos;
        }
    }

    df::coord invalid;
    invalid.clear();
    return invalid;
}

// XXX
bool Plan::corridor_include_hack(const room *r, df::coord t)
{
    for (auto c = corridors.begin(); c != corridors.end(); c++)
    {
        if (!(*c)->include(t))
        {
            continue;
        }

        if ((*c)->min.z != (*c)->max.z)
        {
            return true;
        }
        for (auto a = (*c)->accesspath.begin(); a != (*c)->accesspath.end(); a++)
        {
            if (*a == r)
            {
                return true;
            }
        }
    }
    return false;
}

// vim: et:sw=4:ts=4

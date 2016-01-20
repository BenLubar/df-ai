#include "ai.h"
#include "plan.h"
#include "stocks.h"

#include "modules/Buildings.h"
#include "modules/Items.h"
#include "modules/Job.h"
#include "modules/Maps.h"
#include "modules/Materials.h"
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
#include "df/hauling_route.h"
#include "df/hauling_stop.h"
#include "df/item_boulderst.h"
#include "df/item_toolst.h"
#include "df/itemdef_toolst.h"
#include "df/items_other_id.h"
#include "df/job.h"
#include "df/organic_mat_category.h"
#include "df/plant.h"
#include "df/plant_tree_info.h"
#include "df/plant_tree_tile.h"
#include "df/route_stockpile_link.h"
#include "df/squad.h"
#include "df/stop_depart_condition.h"
#include "df/trap_type.h"
#include "df/ui.h"
#include "df/unit.h"
#include "df/vehicle.h"
#include "df/world.h"

REQUIRE_GLOBAL(ui);
REQUIRE_GLOBAL(world);

const static size_t manager_taskmax = 4; // when stacking manager jobs, do not stack more than this
const static size_t manager_maxbacklog = 10; // add new masonly if more that this much mason manager orders
const static size_t dwarves_per_table = 3; // number of dwarves per dininghall table/chair
const static size_t dwarves_per_farmtile_num = 3; // number of dwarves per farmplot tile
const static size_t dwarves_per_farmtile_den = 2;
const static size_t wantdig_max = 2; // dig at most this much wantdig rooms at a time
const static size_t spare_bedroom = 3; // dig this much free bedroom in advance when idle

const int16_t Plan::MinX = -48, Plan::MinY = -22, Plan::MinZ = -5;
const int16_t Plan::MaxX = 35, Plan::MaxY = 22, Plan::MaxZ = 1;

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
    allow_ice(false),
    past_initial_phase(false),
    cistern_channel_requested(false)
{
    tasks.push_back(new task{horrible_t("checkrooms")});

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
    for (auto t : tasks)
    {
        delete t;
    }
    for (auto r : rooms)
    {
        delete r;
    }
    for (auto r : corridors)
    {
        delete r;
    }
}

static bool find_item(df::items_other_id idx, df::item *&item, bool fire_safe = false, bool non_economic = false)
{
    for (df::item *i : world->items.other[idx])
    {
        if (Stocks::is_item_free(i) &&
                (!fire_safe || i->isTemperatureSafe(1)) &&
                (!non_economic || ui->economic_stone[virtual_cast<df::item_boulderst>(i)->mat_index]))
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
    for (df::item *i : world->items.other[idx])
    {
        if (Stocks::is_item_free(i) &&
                (!fire_safe || i->isTemperatureSafe(1)) &&
                (!non_economic || ui->economic_stone[virtual_cast<df::item_boulderst>(i)->mat_index]))
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

    digroom(out, find_room("workshop", [](room *r) -> bool { return r->subtype == "Masons" && r->misc.at("workshop_level") == 0; }));
    digroom(out, find_room("workshop", [](room *r) -> bool { return r->subtype == "Carpenters" && r->misc.at("workshop_level") == 0; }));
    find_room("workshop", [](room *r) -> bool { return r->subtype.empty() && r->misc.at("workshop_level") == 0; })->subtype = "Masons";
    find_room("workshop", [](room *r) -> bool { return r->subtype.empty() && r->misc.at("workshop_level") == 1; })->subtype = "Masons";
    find_room("workshop", [](room *r) -> bool { return r->subtype.empty() && r->misc.at("workshop_level") == 2; })->subtype = "Masons";

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
    for (auto t : tasks)
    {
        if ((*t)[0] != "digroom")
            continue;
        df::coord size = (*t)[1].r->size();
        if ((*t)[1].r->type != "corridor" || size.z > 1)
            nrdig++;
        if ((*t)[1].r->type != "corridor" && size.x * size.y * size.z >= 10)
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
                if (t[0] == "wantdig")
                {
                    if (t[1].r->is_dug() || nrdig < wantdig_max)
                    {
                        digroom(out, t[1]);
                        del = true;
                    }
                }
                else if (t[0] == "digroom")
                {
                    if (t[1].r->is_dug())
                    {
                        t[1].r->status = "dug";
                        construct_room(out, t[1]);
                        want_reupdate = true; // wantdig asap
                        del = true;
                    }
                }
                else if (t[0] == "construct_workshop")
                {
                    del = try_construct_workshop(out, t[1]);
                }
                else if (t[0] == "construct_stockpile")
                {
                    del = try_construct_stockpile(out, t[1]);
                }
                else if (t[0] == "construct_activityzone")
                {
                    del = try_construct_activityzone(out, t[1]);
                }
                else if (t[0] == "setup_farmplot")
                {
                    del = try_setup_farmplot(out, t[1]);
                }
                else if (t[0] == "furnish")
                {
                    del = try_furnish(out, t[1], t[2]);
                }
                else if (t[0] == "checkfurnish")
                {
                    del = try_endfurnish(out, t[1], t[2]);
                }
                else if (t[0] == "checkconstruct")
                {
                    del = try_endconstruct(out, t[1]);
                }
                else if (t[0] == "dig_cistern")
                {
                    del = try_digcistern(out, t[1]);
                }
                else if (t[0] == "dig_garbage")
                {
                    del = try_diggarbage(out, t[1]);
                }
                else if (t[0] == "checkidle")
                {
                    del = checkidle(out);
                }
                else if (t[0] == "checkrooms")
                {
                    checkrooms(out);
                }
                else if (t[0] == "monitor_cistern")
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

task *Plan::is_digging()
{
    for (auto t : tasks)
    {
        if (((*t)[0] == "wantdig" || (*t)[0] == "digroom") && (*t)[1].r->type != "corridor")
            return t;
    }
    return nullptr;
}

bool Plan::is_idle()
{
    for (auto t : tasks)
    {
        if ((*t)[0] != "monitor_cistern" && (*t)[0] != "checkrooms" && (*t)[0] != "checkidle")
            return false;
    }
    return true;
}

void Plan::new_citizen(color_ostream & out, int32_t uid)
{
    if (std::find_if(tasks.begin(), tasks.end(), [](task *t) -> bool { return (*t)[0] == "checkidle"; }) == tasks.end())
    {
        tasks.push_back(new task{horrible_t("checkidle")});
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

    for (auto b : world->buildings.other[buildings_other_id::WAGON])
    {
        if (df::building_wagonst *w = virtual_cast<df::building_wagonst>(b))
        {
            bool any = false;
            for (auto i : w->contained_items)
            {
                if (i->use_mode == 0)
                {
                    any = true;
                    break;
                }
            }
            if (!any)
            {
                Buildings::deconstruct(w);
            }
        }
    }

    // if nothing better to do, order the miners to dig remaining
    // stockpiles, workshops, and a few bedrooms
    auto ifplan = [](room *r) -> bool
    {
        return r->status == "plan";
    };
    size_t freebed = spare_bedroom;
    room *r = nullptr;
#define FIND_ROOM(cond, type, lambda) \
    if (r == nullptr && (cond)) \
        r = find_room(type, lambda)

    FIND_ROOM(!important_workshops.empty(), "workshop", [this](room *r) -> bool
            {
                if (r->subtype == important_workshops.back() &&
                        r->status == "plan" &&
                        r->misc.at("workshop_level") == 0)
                {
                    important_workshops.pop_back();
                    return true;
                }
                return false;
            });
    FIND_ROOM(true, "cistern", ifplan);
    FIND_ROOM(true, "well", ifplan);
    FIND_ROOM(true, "infirmary", ifplan);
    FIND_ROOM(!find_room("cemetary", [](room *r) -> bool { return r->status != "plan"; }), "cemetary", ifplan);
    FIND_ROOM(!important_workshops2.empty(), "workshop", [this](room *r) -> bool
            {
                if (r->subtype == important_workshops2.back() &&
                        r->status == "plan" &&
                        r->misc.at("workshop_level") == 0)
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
                        r->misc.at("stockpile_level") <= 1;
            });
    FIND_ROOM(true, "workshop", [](room *r) -> bool
            {
                return !r->subtype.empty() &&
                        r->status == "plan" &&
                        r->misc.at("workshop_level") == 0;
            });
    if (r == nullptr && !fort_entrance->misc.count("furnished"))
        r = fort_entrance;
    if (r == nullptr)
        past_initial_phase = true;
    FIND_ROOM(true, "outpost", [](room *r) -> bool
            {
                return r->status == "plan" && r->subtype == "cavern";
            });
    FIND_ROOM(true, "workshop", [](room *r) -> bool
            {
                return !r->subtype.empty() &&
                        r->status == "plan" &&
                        r->misc.at("workshop_level") == 1;
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
    auto finished_nofurnished = [](room *r) -> bool
    {
        return r->status == "finished" && !r->misc.count("furnished");
    };
    FIND_ROOM(true, "nobleroom", finished_nofurnished);
    FIND_ROOM(true, "bederoom", finished_nofurnished);
    auto nousers_noplan = [](room *r) -> bool
    {
        return r->status != "plan" && std::find_if(r->layout.begin(), r->layout.end(), [](furniture *f) -> bool
                {
                    return f->count("users") &&
                            f->at("users").ids.empty();
                }) != r->layout.end();
    };
    auto nousers_plan = [](room *r) -> bool
    {
        return r->status == "plan" && std::find_if(r->layout.begin(), r->layout.end(), [](furniture *f) -> bool
                {
                    return f->count("users") &&
                            f->at("users").ids.empty();
                }) != r->layout.end();
    };
    FIND_ROOM(!find_room("dininghall", nousers_noplan), "dininghall", nousers_plan);
    FIND_ROOM(!find_room("barracks", nousers_noplan), "barracks", nousers_plan);
    FIND_ROOM(true, "stockpile", [](room *r) -> bool
            {
                return r->status == "plan" &&
                        r->misc.at("stockpile_level") <= 3;
            });
    FIND_ROOM(true, "workshop", [](room *r) -> bool
            {
                return !r->subtype.empty() &&
                        r->status == "plan";
            });
    FIND_ROOM(true, "cartway", ifplan);
    FIND_ROOM(true, "stockpile", ifplan);
#undef FIND_ROOM

    if (r)
    {
        ai->debug(out, "checkidle " + describe_room(r));
        wantdig(out, r);
        if (r->status == "finished")
        {
            r->misc["furnished"] = horrible_t();
            for (furniture *f : r->layout)
            {
                f->erase("ignore");
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
    return false;
}

static std::vector<room *> idleidle_tab;

void Plan::idleidle(color_ostream & out)
{
    ai->debug(out, "smooth fort");
    idleidle_tab.clear();
    for (auto r : rooms)
    {
        if (r->status != "plan" && r->status != "dig" &&
                (r->type == "nobleroom" ||
                 r->type == "bedroom" ||
                 r->type == "well" ||
                 r->type == "dininghall" ||
                 r->type == "cemetary" ||
                 r->type == "infirmary" ||
                 r->type == "barracks"))
            idleidle_tab.push_back(r);
    }
    for (auto r : corridors)
    {
        if (r->status != "plan" && r->status != "dig")
            idleidle_tab.push_back(r);
    }

    events.onupdate_register_once("df-ai plan idleidle", 4, [this](color_ostream & out)
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
        for (auto f : r->layout)
        {
            if (f->count("ignore"))
                continue;
            df::coord t = r->min + df::coord(f->count("x") ? f->at("x").id : 0, f->count("y") ? f->at("y").id : 0, f->count("z") ? f->at("z").id : 0);
            if (f->count("bld_id") && !df::building::find(f->at("bld_id")))
            {
                ai->debug(out, "fix furniture " + f->at("item").str + " in " + describe_room(r), t);
                f->erase("bld_id");

                tasks.push_back(new task{horrible_t("furnish"), horrible_t(r), horrible_t(f)});
            }
            if (f->count("construction"))
            {
                try_furnish_construction(out, r, f, t);
            }
        }
        // tantrumed building
        if (r->misc.count("bld_id") && !r->dfbuilding())
        {
            ai->debug(out, "rebuild " + describe_room(r), r->pos());
            r->misc.erase("bld_id");
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
        r = find_room("bedroom", [](room *r) -> bool { return r->status == "plan" && !r->misc.count("queue_dig"); });
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
        ai->debug(out, stl_sprintf("AI can't getbedroom(%d)", id));
    }
}

void Plan::getdiningroom(color_ostream & out, int32_t id)
{
    if (room *r = find_room("farmplot", [](room *r) -> bool
                {
                    df::coord size = r->size();
                    return r->subtype == "food" &&
                        !r->misc.count("outdoor") &&
                        r->misc["users"].ids.size() < size.x * size.y *
                        dwarves_per_farmtile_num / dwarves_per_farmtile_den;
                }))
    {
        wantdig(out, r);
        r->misc["users"].ids.insert(id);
    }

    if (room *r = find_room("farmplot", [](room *r) -> bool
                {
                    df::coord size = r->size();
                    return r->subtype == "cloth" &&
                        !r->misc.count("outdoor") &&
                        r->misc["users"].ids.size() < size.x * size.y *
                        dwarves_per_farmtile_num / dwarves_per_farmtile_den;
                }))
    {
        wantdig(out, r);
        r->misc["users"].ids.insert(id);
    }

    if (room *r = find_room("farmplot", [](room *r) -> bool
                {
                    df::coord size = r->size();
                    return r->subtype == "food" &&
                        r->misc.count("outdoor") &&
                        r->misc["users"].ids.size() < size.x * size.y *
                        dwarves_per_farmtile_num / dwarves_per_farmtile_den;
                }))
    {
        wantdig(out, r);
        r->misc["users"].ids.insert(id);
    }

    if (room *r = find_room("farmplot", [](room *r) -> bool
                {
                    df::coord size = r->size();
                    return r->subtype == "cloth" &&
                        r->misc.count("outdoor") &&
                        r->misc["users"].ids.size() < size.x * size.y *
                        dwarves_per_farmtile_num / dwarves_per_farmtile_den;
                }))
    {
        wantdig(out, r);
        r->misc["users"].ids.insert(id);
    }

    if (room *r = find_room("dininghall", [](room *r) -> bool
                {
                    for (furniture *f : r->layout)
                    {
                        if (f->count("users") &&
                                f->at("users").ids.size() < dwarves_per_table)
                            return true;
                    }
                    return false;
                }))
    {
        wantdig(out, r);
        for (furniture *f : r->layout)
        {
            if (f->count("item") && f->at("item") == "table" &&
                    f->at("users").ids.size() < dwarves_per_table)
            {
                f->erase("ignore");
                (*f)["users"].ids.insert(id);
                break;
            }
        }
        for (furniture *f : r->layout)
        {
            if (f->count("item") && f->at("item") == "chair" &&
                    f->at("users").ids.size() < dwarves_per_table)
            {
                f->erase("ignore");
                (*f)["users"].ids.insert(id);
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

    for (int32_t id : id_list)
    {
        std::vector<Units::NoblePosition> entpos;
        Units::getNoblePositions(&entpos, df::unit::find(id));
        room *base = find_room("nobleroom", [id](room *r) -> bool { return r->owner == id; });
        if (!base)
            base = find_room("nobleroom", [](room *r) -> bool { return r->owner == -1; });
        std::set<std::string> seen;
        while (room *r = find_room("nobleroom", [base, seen](room *r) -> bool { return r->misc.at("noblesuite").id == base->misc.at("noblesuite").id && !seen.count(r->subtype); }))
        {
            seen.insert(r->subtype);
            set_owner(out, r, id);
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


    room *r = find_room("barracks", [squad_id](room *r) -> bool { return r->misc.count("squad_id") && r->misc.at("squad_id") == squad_id; });
    if (!r)
    {
        r = find_room("barracks", [](room *r) -> bool { return !r->misc.count("squad_id") || r->misc.at("squad_id") == -1; });
        if (!r)
        {
            ai->debug(out, "no free barracks");
            return;
        }
        r->misc["squad_id"] = squad_id;
        ai->debug(out, stl_sprintf("squad %d assign %s", squad_id, describe_room(r).c_str()));
        wantdig(out, r);
        if (r->misc.count("bld_id"))
        {
            if (df::building *bld = r->dfbuilding())
            {
                assign_barrack_squad(out, bld, squad_id);
            }
        }
    }

    auto find_furniture = [id, r](const std::string & type)
    {
        for (furniture *f : r->layout)
        {
            if (f->at("item") == type && f->at("users").ids.count(id))
            {
                f->erase("ignore");
                return;
            }
        }
        for (furniture *f : r->layout)
        {
            if (f->at("item") == type && (type == "archerytarget" || f->at("users").ids.size() < (type == "weaponrack" ? 4 : 1)))
            {
                (*f)["users"].ids.insert(id);
                f->erase("ignore");
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
    if (room *r = find_room("cemetary", [](room *r) -> bool { return std::find_if(r->layout.begin(), r->layout.end(), [](furniture *f) -> bool { return f->count("users") && f->at("users").ids.size() < 1; }) != r->layout.end(); }))
    {
        wantdig(out, r);
        for (furniture *f : r->layout)
        {
            if (f->at("item") == "coffin" && f->at("users").ids.size() < 1)
            {
                f->at("users").ids.insert(0);
                f->erase("ignore");
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
    if (room *r = find_room("bedroom", [id](room *r) -> bool { return r->owner == id; }))
    {
        ai->debug(out, "free " + describe_room(r), r->pos());
        set_owner(out, r, -1);
        for (furniture *f : r->layout)
        {
            if (f->count("ignore"))
                continue;
            if (f->at("item") == "door")
                continue;
            if (f->count("bld_id"))
            {
                if (df::building *bld = df::building::find(f->at("bld_id")))
                {
                    Buildings::deconstruct(bld);
                    f->erase("bld_id");
                }
            }
        }
        r->misc.erase("bld_id");
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
                    r->misc.at("users").ids.erase(id);
                    if (r->misc.count("bld_id") && r->misc.at("users").ids.empty())
                    {
                        if (df::building *bld = r->dfbuilding())
                        {
                            Buildings::deconstruct(bld);
                        }
                        r->misc.erase("bld_id");
                    }
                    return false;
                });
    }
    else
    {
        find_room(subtype, [this, &out, id](room *r) -> bool
                {
                    for (furniture *f : r->layout)
                    {
                        if (!f->count("users"))
                            continue;
                        if (f->count("ignore"))
                            continue;
                        if (f->at("users").ids.erase(id) && f->at("users").ids.empty())
                        {
                            // delete the specific table/chair/bed/etc for the dwarf
                            if (f->count("bld_id") && (!r->misc.count("bld_id") || (int32_t) f->at("bld_id") != r->misc.at("bld_id")))
                            {
                                if (df::building *bld = df::building::find(f->at("bld_id")))
                                {
                                    Buildings::deconstruct(bld);
                                }
                                f->erase("bld_id");
                                (*f)["ignore"] = horrible_t();
                            }

                            // clear the whole room if it is entirely unused
                            if (r->misc.count("bld_id") && std::find_if(r->layout.begin(), r->layout.end(), [](furniture *f) -> bool { return f->count("users") && !f->at("users").ids.empty(); }) == r->layout.end())
                            {
                                if (df::building *bld = r->dfbuilding())
                                {
                                    Buildings::deconstruct(bld);
                                }
                                r->misc.erase("bld_id");

                                if (r->misc.count("squad_id"))
                                {
                                    ai->debug(out, stl_sprintf("squad %d free %s", r->misc.at("squad_id").id, describe_room(r).c_str()), r->pos());
                                    r->misc.erase("squad_id");
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
    if (room *r = find_room("pasture", [limit](room *r) -> bool
                {
                    size_t sum = 0;
                    for (int32_t id : r->misc.at("users").ids)
                    {
                        df::unit *u = df::unit::find(id);
                        // 11*11 == pasture dimensions
                        sum += 11*11*1000 / df::creature_raw::find(u->race)->caste[u->caste]->misc.grazer;
                    }
                    return sum < limit;
                }))
    {
        r->misc["users"].ids.insert(pet_id);
        if (!r->misc.count("bld_id"))
            construct_room(out, r);
        return r->dfbuilding();
    }
    return nullptr;
}

void Plan::freepasture(color_ostream & out, int32_t pet_id)
{
    if (room *r = find_room("pasture", [pet_id](room *r) -> bool { return r->misc.at("users").ids.count(pet_id); }))
    {
        r->misc.at("users").ids.erase(pet_id);
    }
}

void Plan::set_owner(color_ostream & out, room *r, int32_t uid)
{
    r->owner = uid;
    if (r->misc.count("bld_id"))
    {
        df::unit *u = df::unit::find(uid);
        if (df::building *bld = r->dfbuilding())
        {
            Buildings::setOwner(bld, u);
        }
    }
}

void Plan::dig_tile(df::coord t, std::string mode)
{
    df::tile_dig_designation dig;
    if (!find_enum_item(&dig, mode))
        return;

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
    if (r->misc.count("queue_dig") || r->status != "plan")
        return;
    ai->debug(out, "wantdig " + describe_room(r));
    r->misc["queue_dig"] = horrible_t();
    r->dig("plan");
    tasks.push_back(new task{horrible_t("wantdig"), horrible_t(r)});
}

void Plan::digroom(color_ostream & out, room *r)
{
    if (r->status != "plan")
        return;
    ai->debug(out, "digroom " + describe_room(r));
    r->misc.erase("queue_dig");
    r->status = "dig";
    r->fixup_open();
    r->dig();

    tasks.push_back(new task{horrible_t("digroom"), horrible_t(r)});

    for (room *ap : r->accesspath)
    {
        digroom(out, ap);
    }

    for (furniture *f : r->layout)
    {
        if (!f->count("item") || f->at("item") == "floodgate")
            continue;
        if (f->count("dig"))
            continue;
        tasks.push_back(new task{horrible_t("furnish"), horrible_t(r), horrible_t(f)});
    }

    if (r->type == "workshop" && r->misc.at("workshop_level") == 0)
    {
        // add minimal stockpile in front of workshop
        const static std::map<std::string, std::string> sptypes =
        {
            {"Masons", "stone"}, {"Carpenters", "wood"}, {"Craftsdwarfs", "refuse"},
            {"Farmers", "food"}, {"Fishery", "food"}, {"Jewelers", "gems"}, {"Loom", "cloth"},
            {"Clothiers", "cloth"}, {"Still", "food"}, {"Kitchen", "food"}, {"WoodFurnace", "wood"},
            {"Smelter", "stone"}, {"MetalsmithsForge", "bars_blocks"},
        };
        if (sptypes.count(r->subtype))
        {
            // XXX hardcoded fort layout
            int16_t y = r->layout[0]->at("y") > 0 ? r->max.y + 2 : r->min.y - 2; // check door position
            room *sp = new room("stockpile", sptypes.at(r->subtype), df::coord(r->min.x, y, r->min.z), df::coord(r->max.x, y, r->min.z));
            sp->misc["workshop"] = r;
            sp->misc["stockpile_level"] = 0;
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
        tasks.push_back(new task{horrible_t("construct_stockpile"), horrible_t(r)});
        return true;
    }

    if (r->type == "workshop")
    {
        tasks.push_back(new task{horrible_t("construct_workshop"), horrible_t(r)});
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

    if (r->type == "infirmary" || r->type == "pasture" || r->type == "pitcage")
    {
        furnish_room(out, r);
        if (try_construct_activityzone(out, r))
            return true;
        tasks.push_back(new task{horrible_t("construct_activityzone"), horrible_t(r)});
        return true;
    }

    if (r->type == "dininghall")
    {
        if (!r->misc.count("temporary"))
        {
            if (room *t = find_room("dininghall", [](room *r) -> bool { return r->misc.count("temporary"); }))
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
    for (furniture *f : r->layout)
    {
        tasks.push_back(new task{horrible_t("furnish"), horrible_t(r), horrible_t(f)});
    }
    r->status = "finished";
    return true;
}

static df::building_type FurnitureBuilding(std::string k)
{
    if (k == "gear_assembly")
        return building_type::GearAssembly;
    if (k == "vertical_axle")
        return building_type::AxleVertical;
    if (k == "traction_bench")
        return building_type::TractionBench;
    if (k == "nestbox")
        return building_type::NestBox;

    k[0] += 'A' - 'a';
    df::building_type bt = df::building_type(-1);
    find_enum_item(&bt, k);
    return bt;
}

bool Plan::try_furnish(color_ostream & out, room *r, furniture *f)
{
    if (f->count("bld_id"))
        return true;
    if (f->count("ignore"))
        return true;
    df::coord tgtile = r->min + df::coord(f->count("x") ? f->at("x").id : 0, f->count("y") ? f->at("y").id : 0, f->count("z") ? f->at("z").id : 0);
    df::tiletype tt = *Maps::getTileType(tgtile);
    if (f->count("construction"))
    {
        if (try_furnish_construction(out, r, f, tgtile))
        {
            if (!f->count("item"))
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

    if (!f->count("item"))
        return true;

    if (f->at("item") == "well")
        return try_furnish_well(out, r, f, tgtile);

    if (f->at("item") == "archerytarget")
        return try_furnish_archerytarget(out, r, f, tgtile);

    if (f->at("item") == "gear_assembly" && !find_item(items_other_id::TRAPPARTS, itm))
        return false;

    if (f->at("item") == "vertical_axle" && !find_item(items_other_id::WOOD, itm))
        return false;

    if (f->at("item") == "windmill")
        return try_furnish_windmill(out, r, f, tgtile);

    if (f->at("item") == "roller")
        return try_furnish_windmill(out, r, f, tgtile);

    if (f->at("item") == "minecart_route")
        return try_furnish_minecart_route(out, r, f, tgtile);

    if (cache_nofurnish.count(f->at("item")))
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
        dig_tile(tgtile, f->count("dig") ? f->at("dig").str : "Default");
        return false;
    }

    if (itm == nullptr)
    {
        itm = ai->stocks->find_furniture_item(f->at("item"));
    }

    if (itm != nullptr)
    {
        if (f->count("subtype") && f->at("subtype") == "cage" && ai->stocks->count["cage"] < 1)
        {
            // avoid too much spam
            return false;
        }
        ai->debug(out, "furnish " + f->at("item").str + " in " + describe_room(r));
        df::building_type bldn = FurnitureBuilding(f->at("item"));
        const static std::map<std::string, int> subtypes = {{"cage", trap_type::CageTrap}, {"lever", trap_type::Lever}, {"trackstop", trap_type::TrackStop}};
        int subtype = f->count("subtype") ? subtypes.at(f->at("subtype")) : -1;
        df::building *bld = Buildings::allocInstance(tgtile, bldn, subtype);
        Buildings::setSize(bld, df::coord(1, 1, 1));
        if (f->count("misc_bldprops"))
        {
            f->at("misc_bldprops").bldprops(out, bld);
        }
        Buildings::constructWithItems(bld, {itm});
        if (f->count("makeroom"))
        {
            r->misc["bld_id"] = bld->id;
        }
        (*f)["bld_id"] = bld->id;
        tasks.push_back(new task{horrible_t("checkfurnish"), horrible_t(r), horrible_t(f)});
        return true;
    }

    cache_nofurnish.insert(f->at("item").str);
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
        Buildings::constructWithItems(bld, {block, mecha, buckt, chain});
        r->misc["bld_id"] = (*f)["bld_id"] = bld->id;
        tasks.push_back(new task{horrible_t("checkfurnish"), horrible_t(r), horrible_t(f)});
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
    virtual_cast<df::building_archerytargetst>(bld)->archery_direction = f->at("y").id > 2 ? df::building_archerytargetst::TopToBottom : df::building_archerytargetst::BottomToTop;
    Buildings::constructWithItems(bld, {bould});
    (*f)["bld_id"] = bld->id;
    tasks.push_back(new task{horrible_t("checkfurnish"), horrible_t(r), horrible_t(f)});
    return true;
}

bool Plan::try_furnish_construction(color_ostream & out, room *r, furniture *f, df::coord t)
{
    df::tiletype tt = *Maps::getTileType(t);
    if (ENUM_ATTR(tiletype, material, tt) == tiletype_material::TREE)
    {
        dig_tile(ai->plan->find_tree_base(t));
        return false;
    }

    df::tiletype_shape_basic sb = ENUM_ATTR(tiletype_shape, basic_shape, ENUM_ATTR(tiletype, shape, tt));
    std::string ctype = f->at("construction");
    if (ctype == "NoRamp")
    {
        if (sb == tiletype_shape_basic::Ramp)
        {
            dig_tile(t);
        }
        return Maps::getTileDesignation(t)->bits.dig == tile_dig_designation::No;
    }

    if (ctype == "Wall")
    {
        if (sb == tiletype_shape_basic::Wall)
        {
            return true;
        }
    }

    if (ctype == "Ramp")
    {
        if (sb == tiletype_shape_basic::Ramp)
        {
            return true;
        }
    }

    if (ctype == "UpStair" || ctype == "DownStair" || ctype == "UpDownStair")
    {
        if (sb == tiletype_shape_basic::Stair)
        {
            return true;
        }
    }

    if (ctype == "Floor")
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

    if (ctype == "Track" || ctype == "TrackRamp")
    {
        std::string wantdir = f->at("dir");
        df::tiletype_shape_basic wantshape = tiletype_shape_basic::Floor;
        if (ctype == "TrackRamp")
        {
            wantshape = tiletype_shape_basic::Ramp;
        }

        if (ENUM_ATTR(tiletype, special, tt) == tiletype_special::TRACK &&
                ENUM_ATTR_STR(tiletype, direction, tt) == wantdir &&
                sb == wantshape)
        {
            return true;
        }

        if (ENUM_ATTR(tiletype, material, tt) == tiletype_material::MINERAL ||
                ENUM_ATTR(tiletype, material, tt) == tiletype_material::STONE)
        {
            if (ctype == "Track" && sb == tiletype_shape_basic::Ramp)
            {
                dig_tile(t);
                return false;
            }

            if (sb == wantshape)
            {
                // carve track
                df::tile_occupancy *occ = Maps::getTileOccupancy(t);
                if (wantdir.find("N") != std::string::npos)
                    occ->bits.carve_track_north = 1;
                if (wantdir.find("S") != std::string::npos)
                    occ->bits.carve_track_south = 1;
                if (wantdir.find("E") != std::string::npos)
                    occ->bits.carve_track_east = 1;
                if (wantdir.find("W") != std::string::npos)
                    occ->bits.carve_track_west = 1;

                Maps::getTileBlock(t)->flags.bits.designated = 1;

                return true;
            }
        }

        ctype += wantdir;
    }

    // fall through = must build actual construction

    if (ENUM_ATTR(tiletype, material, tt) == tiletype_material::CONSTRUCTION)
    {
        // remove existing invalid construction
        dig_tile(t);
        return false;
    }

    for (df::building *b : world->buildings.all)
    {
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

    df::construction_type sub = df::construction_type(-1);
    find_enum_item(&sub, ctype);

    df::building *bld = Buildings::allocInstance(t, building_type::Construction, sub);
    Buildings::setSize(bld, df::coord(1, 1, 1));
    Buildings::constructWithItems(bld, {block});
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
    (*f)["bld_id"] = bld->id;
    tasks.push_back(new task{horrible_t("checkfurnish"), horrible_t(r), horrible_t(f)});
    return true;
}

bool Plan::try_furnish_roller(color_ostream & out, room *r, furniture *f, df::coord t)
{
    df::item *mecha, *chain;
    if (find_item(items_other_id::TRAPPARTS, mecha) &&
            find_item(items_other_id::CHAIN, chain))
    {
        df::building *bld = Buildings::allocInstance(t, building_type::Rollers);
        if (f->count("misc_bldprops"))
            f->at("misc_bldprops").bldprops(out, bld);
        Buildings::setSize(bld, df::coord(1, 1, 1));
        Buildings::constructWithItems(bld, {mecha, chain});
        r->misc["bld_id"] = bld->id;
        (*f)["bld_id"] = bld->id;
        tasks.push_back(new task{horrible_t("checkfurnish"), horrible_t(r), horrible_t(f)});
        return true;
    }
    return false;
}

bool Plan::try_furnish_minecart_route(color_ostream & out, room *r, furniture *f, df::coord t)
{
    if (f->count("route_id"))
        return true;
    if (!r->dfbuilding())
        return false;
    // TODO wait roller ?
    for (df::item *i : world->items.other[items_other_id::TOOL])
    {
        df::item_toolst *mcart = virtual_cast<df::item_toolst>(i);
        if (mcart &&
                mcart->subtype->subtype == ai->stocks->manager_subtype["MakeWoodenMinecart"] &&
                mcart->stockpile.id == -1 &&
                (mcart->vehicle_id == -1 ||
                 df::vehicle::find(mcart->vehicle_id)->route_id == -1))
        {
            df::route_stockpile_link *routelink = df::allocate<df::route_stockpile_link>();
            routelink->building_id = r->misc.at("bld_id");
            routelink->mode.bits.take = 1;
            df::hauling_stop *stop = df::allocate<df::hauling_stop>();
            stop->id = 1;
            stop->pos = t;
            df::stop_depart_condition *cond = df::allocate<df::stop_depart_condition>();
            find_enum_item(&cond->direction, f->at("direction"));
            cond->mode = df::stop_depart_condition::Push;
            cond->load_percent = 100;
            stop->conditions.push_back(cond);
            stop->stockpiles.push_back(routelink);
            stop->cart_id = mcart->id;
            setup_stockpile_settings(out, r->subtype, stop->settings);
            df::hauling_route *route = df::allocate<df::hauling_route>();
            route->id = ui->hauling.next_id;
            route->stops.push_back(stop);
            route->vehicle_ids.push_back(mcart->vehicle_id);
            route->vehicle_stops.push_back(0);
            df::vehicle::find(mcart->vehicle_id)->route_id = route->id;
            virtual_cast<df::building_stockpilest>(r->dfbuilding())->linked_stops.push_back(stop);
            ui->hauling.next_id++;
            ui->hauling.routes.push_back(route);
            // view_* are gui only
            (*f)["route_id"] = route->id;
            return true;
        }
    }
    return false;
}

bool Plan::try_furnish_trap(color_ostream & out, room *r, furniture *f)
{
    df::coord t = r->min + df::coord(f->count("x") ? f->at("x").id : 0, f->count("y") ? f->at("y").id : 0, f->count("z") ? f->at("z").id : 0);

    if (Maps::getTileOccupancy(t)->bits.building != tile_building_occ::None)
        return false;

    df::tiletype tt = *Maps::getTileType(t);
    if (ENUM_ATTR(tiletype, shape, tt) == tiletype_shape::RAMP ||
            ENUM_ATTR(tiletype, material, tt) == tiletype_material::TREE ||
            ENUM_ATTR(tiletype, material, tt) == tiletype_material::ROOT)
    {
        // XXX dont remove last access ramp ?
        dig_tile(t, "Default");
        return false;
    }

    df::item *mecha;
    if (find_item(items_other_id::TRAPPARTS, mecha))
        return false;

    const static std::map<std::string, df::trap_type> subtypes =
    {
        {"lever", trap_type::Lever},
        {"cage", trap_type::CageTrap},
    };

    df::trap_type subtype = subtypes.at(f->at("subtype"));
    df::building *bld = Buildings::allocInstance(t, building_type::Trap, subtype);
    Buildings::setSize(bld, df::coord(1, 1, 1));
    Buildings::constructWithItems(bld, {mecha});
    (*f)["bld_id"] = bld->id;
    tasks.push_back(new task{horrible_t("checkfurnish"), horrible_t(r), horrible_t(f)});

    return true;
}

static int32_t find_custom_building(const std::string & code)
{
    for (auto b : world->raws.buildings.all)
    {
        if (b->code == code)
        {
            return b->id;
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
            Buildings::constructWithItems(bld, {barrel, bucket});
            r->misc["bld_id"] = bld->id;
            tasks.push_back(new task{horrible_t("checkconstruct"), horrible_t(r)});
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
            Buildings::constructWithItems(bld, {block, barrel, bucket});
            r->misc["bld_id"] = bld->id;
            tasks.push_back(new task{horrible_t("checkconstruct"), horrible_t(r)});
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
            Buildings::constructWithItems(bld, {buckt, bould});
            r->misc["bld_id"] = bld->id;
            tasks.push_back(new task{horrible_t("checkconstruct"), horrible_t(r)});
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
            r->misc["bld_id"] = bld->id;
            tasks.push_back(new task{horrible_t("checkconstruct"), horrible_t(r)});
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
            Buildings::constructWithItems(bld, {anvil, bould});
            r->misc["bld_id"] = bld->id;
            tasks.push_back(new task{horrible_t("checkconstruct"), horrible_t(r)});
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
            df::furnace_type furnace_subtype = df::furnace_type(-1);
            if (!find_enum_item(&furnace_subtype, r->subtype))
            {
                ai->debug(out, "could not find furnace subtype for " + r->subtype);
            }
            df::building *bld = Buildings::allocInstance(r->min, building_type::Furnace, furnace_subtype);
            Buildings::setSize(bld, r->size());
            Buildings::constructWithItems(bld, {bould});
            r->misc["bld_id"] = bld->id;
            tasks.push_back(new task{horrible_t("checkconstruct"), horrible_t(r)});
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
            Buildings::constructWithItems(bld, {quern});
            r->misc["bld_id"] = bld->id;
            tasks.push_back(new task{horrible_t("checkconstruct"), horrible_t(r)});
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
            r->misc["bld_id"] = bld->id;
            tasks.push_back(new task{horrible_t("checkconstruct"), horrible_t(r)});
            return true;
        }
    }
    else
    {
        df::workshop_type subtype = df::workshop_type(-1);
        if (!find_enum_item(&subtype, r->subtype))
        {
            ai->debug(out, "could not find workshop subtype for " + r->subtype);
        }
        df::item *bould;
        if (find_item(items_other_id::BOULDER, bould, false, true) ||
                // use wood if we can't find stone
                find_item(items_other_id::WOOD, bould))
        {
            df::building *bld = Buildings::allocInstance(r->min, building_type::Workshop, subtype);
            Buildings::setSize(bld, r->size());
            Buildings::constructWithItems(bld, {bould});
            r->misc["bld_id"] = bld->id;
            tasks.push_back(new task{horrible_t("checkconstruct"), horrible_t(r)});
            return true;
            // XXX else quarry?
        }
    }
    ai->debug(out, "couldn't build " + describe_room(r));
    return false;
}

bool Plan::try_construct_stockpile(color_ostream & out, room *r)
{
    if (!r->constructions_done())
        return false;

    df::coord size = r->size();

    df::building_stockpilest *bld = virtual_cast<df::building_stockpilest>(Buildings::allocInstance(r->min, building_type::Stockpile));
    Buildings::setSize(bld, r->size());
    Buildings::constructAbstract(bld);
    r->misc["bld_id"] = bld->id;
    furnish_room(out, r);

    setup_stockpile_settings(out, r->subtype, bld->settings, bld, r);

    if (r->misc.count("workshop") && r->subtype == "stone")
    {
        room_items(out, r, [](df::item *i) { i->flags.bits.dump = 1; });
    }

    if (r->misc.at("stockpile_level") == 0 &&
            r->subtype != "stone" && r->subtype != "wood")
    {
        if (room *rr = find_room("stockpile", [r](room *o) -> bool { return o->subtype == r->subtype && o->misc.at("stockpile_level") == 1; }))
        {
            wantdig(out, rr);
        }
    }

    // setup stockpile links with adjacent stockpile_level
    find_room("stockpile", [r, bld](room *o) -> bool
            {
                int32_t diff = o->misc.at("stockpile_level").id - r->misc.at("stockpile_level").id;
                if (o->subtype == r->subtype && (diff == -1 || diff == 1))
                {
                    if (df::building_stockpilest *obld = virtual_cast<df::building_stockpilest>(o->dfbuilding()))
                    {
                        df::building_stockpilest *b_from, *b_to;
                        if (o->misc.at("stockpile_level").id > r->misc.at("stockpile_level").id)
                        {
                            b_from = obld;
                            b_to = bld;
                        }
                        else
                        {
                            b_from = bld;
                            b_to = obld;
                        }
                        for (auto btf : b_to->links.take_from_pile)
                        {
                            if (btf->id == b_from->id)
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

    df::building_civzonest *bld = virtual_cast<df::building_civzonest>(Buildings::allocInstance(r->min, building_type::Civzone, civzone_type::ActivityZone));
    Buildings::setSize(bld, r->size());
    Buildings::constructAbstract(bld);
    r->misc["bld_id"] = bld->id;
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

    return true;
}

void Plan::setup_stockpile_settings(color_ostream & out, std::string subtype, df::stockpile_settings & settings, df::building_stockpilest *bld, room *r)
{
#define NUM_ENUM_VALUES(t) (df::enum_traits<df::t>::last_item_value - df::enum_traits<df::t>::first_item_value + 1)
    if (subtype == "stone")
    {
        settings.flags.bits.stone = 1;
        auto & t = settings.stone.mats;
        t.resize(world->raws.inorganics.size());
        if (r && r->misc.count("workshop") &&
                r->misc.at("workshop").r->subtype == "Masons")
        {
            for (size_t i = 0; i < world->raws.inorganics.size(); i++)
            {
                t[i] = !ui->economic_stone[i];
            }
        }
        else if (r && r->misc.count("workshop") &&
                r->misc.at("workshop").r->subtype == "Smelter")
        {
            for (size_t i = 0; i < world->raws.inorganics.size(); i++)
            {
                t[i] = world->raws.inorganics[i]->flags.is_set(inorganic_flags::METAL_ORE);
            }
        }
        else
        {
            t.clear();
            t.resize(world->raws.inorganics.size(), true);
        }
        if (r)
        {
            bld->max_wheelbarrows = 1;
        }
    }
    else if (subtype == "wood")
    {
        settings.flags.bits.wood = 1;
        auto & t = settings.wood.mats;
        t.clear();
        t.resize(world->raws.plants.all.size(), true);
    }
    else if (subtype == "furniture")
    {
        settings.flags.bits.furniture = 1;
        settings.furniture.sand_bags = true;
        auto & s = settings.furniture;
        s.type.clear();
        s.type.resize(NUM_ENUM_VALUES(furniture_type), true);
        s.other_mats.clear();
        s.other_mats.resize(16, true);
        s.mats.clear();
        s.mats.resize(world->raws.inorganics.size(), true);
        FOR_ENUM_ITEMS(item_quality, i)
        {
            s.quality_core[i] = true;
            s.quality_total[i] = true;
        }
    }
    else if (subtype == "finished_goods")
    {
        settings.flags.bits.finished_goods = 1;
        auto & s = settings.finished_goods;
        s.type.clear();
        s.type.resize(NUM_ENUM_VALUES(item_type), true);
        if (r)
        {
            df::coord size = r->size();
            bld->max_bins = size.x * size.y;
        }
        s.other_mats.clear();
        s.other_mats.resize(17, true);
        s.mats.clear();
        s.mats.resize(world->raws.inorganics.size(), true);
        FOR_ENUM_ITEMS(item_quality, i)
        {
            s.quality_core[i] = true;
            s.quality_total[i] = true;
        }
    }
    else if (subtype == "ammo")
    {
        settings.flags.bits.ammo = 1;
        auto & s = settings.ammo;
        s.type.clear();
        s.type.resize(world->raws.itemdefs.ammo.size(), true);
        if (r)
        {
            df::coord size = r->size();
            bld->max_bins = size.x * size.y;
        }
        s.other_mats.clear();
        s.other_mats.resize(2, true);
        s.mats.clear();
        s.mats.resize(world->raws.inorganics.size(), true);
        FOR_ENUM_ITEMS(item_quality, i)
        {
            s.quality_core[i] = true;
            s.quality_total[i] = true;
        }
    }
    else if (subtype == "weapons")
    {
        settings.flags.bits.weapons = 1;
        auto & s = settings.weapons;
        s.weapon_type.clear();
        s.weapon_type.resize(world->raws.itemdefs.weapons.size(), true);
        s.trapcomp_type.clear();
        s.trapcomp_type.resize(world->raws.itemdefs.trapcomps.size(), true);
        s.usable = true;
        s.unusable = true;
        if (r)
        {
            df::coord size = r->size();
            bld->max_bins = size.x * size.y;
        }
        s.other_mats.clear();
        s.other_mats.resize(11, true);
        s.mats.clear();
        s.mats.resize(world->raws.inorganics.size(), true);
        FOR_ENUM_ITEMS(item_quality, i)
        {
            s.quality_core[i] = true;
            s.quality_total[i] = true;
        }
    }
    else if (subtype == "armor")
    {
        settings.flags.bits.armor = 1;
        auto & s = settings.armor;
        s.body.clear();
        s.body.resize(world->raws.itemdefs.armor.size(), true);
        s.head.clear();
        s.head.resize(world->raws.itemdefs.helms.size(), true);
        s.feet.clear();
        s.feet.resize(world->raws.itemdefs.shoes.size(), true);
        s.legs.clear();
        s.legs.resize(world->raws.itemdefs.pants.size(), true);
        s.hands.clear();
        s.hands.resize(world->raws.itemdefs.gloves.size(), true);
        s.shield.clear();
        s.shield.resize(world->raws.itemdefs.shields.size(), true);
        s.usable = true;
        s.unusable = true;
        if (r)
        {
            df::coord size = r->size();
            bld->max_bins = size.x * size.y;
        }
        s.other_mats.clear();
        s.other_mats.resize(11, true);
        s.mats.clear();
        s.mats.resize(world->raws.inorganics.size(), true);
        FOR_ENUM_ITEMS(item_quality, i)
        {
            s.quality_core[i] = true;
            s.quality_total[i] = true;
        }
    }
    else if (subtype == "animals")
    {
        settings.flags.bits.animals = 1;
        auto & s = settings.animals;
        s.empty_cages = s.empty_traps = !r || r->misc.at("stockpile_level").id > 1;
        s.enabled.clear();
        s.enabled.resize(world->raws.creatures.all.size(), true);
    }
    else if (subtype == "refuse" || subtype == "corpses")
    {
        settings.flags.bits.refuse = 1;
        auto & t = settings.refuse;
        size_t creatures = world->raws.creatures.all.size();
        t.fresh_raw_hide = false;
        t.rotten_raw_hide = true;
        if (r && r->misc.count("workshop") &&
                r->misc.at("workshop").r->subtype == "Craftsdwarfs")
        {
            t.corpses.clear();
            t.corpses.resize(creatures, false);
            t.body_parts.clear();
            t.body_parts.resize(creatures, false);
            t.hair.clear();
            t.hair.resize(creatures, false);
            t.skulls.clear();
            t.skulls.resize(creatures, true);
            t.bones.clear();
            t.bones.resize(creatures, true);
            t.shells.clear();
            t.shells.resize(creatures, true);
            t.teeth.clear();
            t.teeth.resize(creatures, true);
            t.horns.clear();
            t.horns.resize(creatures, true);
        }
        else if (subtype == "corpses")
        {
            t.type.clear();
            t.type.resize(NUM_ENUM_VALUES(item_type), true);
            t.type[item_type::REMAINS] = false;
            t.type[item_type::PLANT] = false;
            t.type[item_type::PLANT_GROWTH] = false;
            t.corpses.clear();
            t.corpses.resize(creatures, true);
            t.body_parts.clear();
            t.body_parts.resize(creatures, true);
            t.hair.clear();
            t.hair.resize(creatures, false);
            t.skulls.clear();
            t.skulls.resize(creatures, false);
            t.bones.clear();
            t.bones.resize(creatures, false);
            t.shells.clear();
            t.shells.resize(creatures, false);
            t.teeth.clear();
            t.teeth.resize(creatures, false);
            t.horns.clear();
            t.horns.resize(creatures, false);
        }
        else
        {
            t.fresh_raw_hide = true;
            t.rotten_raw_hide = false;
            t.corpses.clear();
            t.corpses.resize(creatures, false);
            t.body_parts.clear();
            t.body_parts.resize(creatures, false);
            t.hair.clear();
            t.hair.resize(creatures, true);
            t.skulls.clear();
            t.skulls.resize(creatures, true);
            t.bones.clear();
            t.bones.resize(creatures, true);
            t.shells.clear();
            t.shells.resize(creatures, true);
            t.teeth.clear();
            t.teeth.resize(creatures, true);
            t.horns.clear();
            t.horns.resize(creatures, true);
        }
    }
    else if (subtype == "food")
    {
        settings.flags.bits.food = true;
        if (r)
        {
            df::coord size = r->size();
            bld->max_barrels = size.x * size.y;
        }
        auto & t = settings.food;
        auto & mt = world->raws.mat_table;
        if (r && r->misc.count("workshop") &&
                r->misc.at("workshop").r->type == "farmplot")
        {
            t.seeds.clear();
            t.seeds.resize(mt.organic_types[organic_mat_category::Seed].size(), true);
        }
        else if (r && r->misc.count("workshop") &&
                r->misc.at("workshop").r->subtype == "Farmers")
        {
            t.plants.clear();
            t.plants.resize(mt.organic_types[organic_mat_category::Plants].size());
            for (size_t i = 0; i < mt.organic_types[organic_mat_category::Plants].size(); i++)
            {
                // include MILL because the quern is near
                MaterialInfo plant(mt.organic_types[organic_mat_category::Plants][i], mt.organic_indexes[organic_mat_category::Plants][i]);
                t.plants[i] = plant.plant->flags.is_set(plant_raw_flags::THREAD) || plant.plant->flags.is_set(plant_raw_flags::MILL);
            }
        }
        else if (r && r->misc.count("workshop") &&
                r->misc.at("workshop").r->subtype == "Still")
        {
            t.plants.clear();
            t.plants.resize(mt.organic_types[organic_mat_category::Plants].size());
            for (size_t i = 0; i < mt.organic_types[organic_mat_category::Plants].size(); i++)
            {
                MaterialInfo plant(mt.organic_types[organic_mat_category::Plants][i], mt.organic_indexes[organic_mat_category::Plants][i]);
                t.plants[i] = plant.plant->flags.is_set(plant_raw_flags::DRINK);
            }
            t.leaves.clear();
            t.leaves.resize(mt.organic_types[organic_mat_category::Leaf].size());
            for (size_t i = 0; i < mt.organic_types[organic_mat_category::Leaf].size(); i++)
            {
                MaterialInfo plant(mt.organic_types[organic_mat_category::Leaf][i], mt.organic_indexes[organic_mat_category::Leaf][i]);
                t.leaves[i] = plant.plant->flags.is_set(plant_raw_flags::DRINK);
            }
        }
        else if (r && r->misc.count("workshop") &&
                r->misc.at("workshop").r->subtype == "Kitchen")
        {
            t.meat.clear();
            t.meat.resize(mt.organic_types[organic_mat_category::Meat].size(), true);
            t.fish.clear();
            t.fish.resize(mt.organic_types[organic_mat_category::Fish].size(), true);
            t.egg.clear();
            t.egg.resize(mt.organic_types[organic_mat_category::Eggs].size(), true);
            t.leaves.clear();
            t.leaves.resize(mt.organic_types[organic_mat_category::Leaf].size(), true);
        }
        else if (r && r->misc.count("workshop") &&
                r->misc.at("workshop").r->subtype == "Fishery")
        {
            t.unprepared_fish.clear();
            t.unprepared_fish.resize(mt.organic_types[organic_mat_category::UnpreparedFish].size(), true);
        }
        else
        {
            t.prepared_meals = true;
            t.meat.clear();
            t.meat.resize(mt.organic_types[organic_mat_category::Meat].size(), true);
            t.fish.clear();
            t.fish.resize(mt.organic_types[organic_mat_category::Fish].size(), true);
            t.unprepared_fish.clear();
            t.unprepared_fish.resize(mt.organic_types[organic_mat_category::UnpreparedFish].size(), true);
            t.egg.clear();
            t.egg.resize(mt.organic_types[organic_mat_category::Eggs].size(), true);
            t.plants.clear();
            t.plants.resize(mt.organic_types[organic_mat_category::Plants].size(), true);
            t.drink_plant.clear();
            t.drink_plant.resize(mt.organic_types[organic_mat_category::PlantDrink].size(), true);
            t.drink_animal.clear();
            t.drink_animal.resize(mt.organic_types[organic_mat_category::CreatureDrink].size(), true);
            t.cheese_plant.clear();
            t.cheese_plant.resize(mt.organic_types[organic_mat_category::PlantCheese].size(), true);
            t.cheese_animal.clear();
            t.cheese_animal.resize(mt.organic_types[organic_mat_category::CreatureCheese].size(), true);
            t.seeds.clear();
            t.seeds.resize(mt.organic_types[organic_mat_category::Seed].size(), true);
            t.leaves.clear();
            t.leaves.resize(mt.organic_types[organic_mat_category::Leaf].size(), true);
            t.powder_plant.clear();
            t.powder_plant.resize(mt.organic_types[organic_mat_category::PlantPowder].size(), true);
            t.powder_creature.clear();
            t.powder_creature.resize(mt.organic_types[organic_mat_category::CreaturePowder].size(), true);
            t.glob.clear();
            t.glob.resize(mt.organic_types[organic_mat_category::Glob].size(), true);
            t.glob_paste.clear();
            t.glob_paste.resize(mt.organic_types[organic_mat_category::Paste].size(), true);
            t.glob_pressed.clear();
            t.glob_pressed.resize(mt.organic_types[organic_mat_category::Pressed].size(), true);
            t.liquid_plant.clear();
            t.liquid_plant.resize(mt.organic_types[organic_mat_category::PlantLiquid].size(), true);
            t.liquid_animal.clear();
            t.liquid_animal.resize(mt.organic_types[organic_mat_category::CreatureLiquid].size(), true);
            t.liquid_misc.clear();
            t.liquid_misc.resize(mt.organic_types[organic_mat_category::MiscLiquid].size(), true);
        }
    }
    else if (subtype == "cloth")
    {
        settings.flags.bits.cloth = 1;
        if (r)
        {
            df::coord size = r->size();
            bld->max_bins = size.x * size.y;
        }
        auto & t = settings.cloth;
        auto cloth = [&t](bool thread, bool cloth)
        {
            auto & mt = world->raws.mat_table;
            t.thread_silk.clear();
            t.thread_silk.resize(mt.organic_types[organic_mat_category::Silk].size(), thread);
            t.cloth_silk.clear();
            t.cloth_silk.resize(mt.organic_types[organic_mat_category::Silk].size(), cloth);
            t.thread_plant.clear();
            t.thread_plant.resize(mt.organic_types[organic_mat_category::PlantFiber].size(), thread);
            t.cloth_plant.clear();
            t.cloth_plant.resize(mt.organic_types[organic_mat_category::PlantFiber].size(), cloth);
            t.thread_yarn.clear();
            t.thread_yarn.resize(mt.organic_types[organic_mat_category::Yarn].size(), thread);
            t.cloth_yarn.clear();
            t.cloth_yarn.resize(mt.organic_types[organic_mat_category::Yarn].size(), cloth);
            t.thread_metal.clear();
            t.thread_metal.resize(mt.organic_types[organic_mat_category::MetalThread].size(), thread);
            t.cloth_metal.clear();
            t.cloth_metal.resize(mt.organic_types[organic_mat_category::MetalThread].size(), cloth);
        };
        if (r && r->misc.count("workshop") &&
                r->misc.at("workshop").r->subtype == "Loom")
        {
            cloth(true, false);
        }
        else if (r && r->misc.count("workshop") &&
                r->misc.at("workshop").r->subtype == "Clothiers")
        {
            cloth(false, true);
        }
        else
        {
            cloth(true, true);
        }
    }
    else if (subtype == "leather")
    {
        settings.flags.bits.leather = 1;
        if (r)
        {
            df::coord size = r->size();
            bld->max_bins = size.x * size.y;
        }
        auto & t = settings.leather;
        t.mats.clear();
        t.mats.resize(world->raws.mat_table.organic_types[organic_mat_category::Leather].size(), true);
    }
    else if (subtype == "gems")
    {
        settings.flags.bits.gems = 1;
        if (r)
        {
            df::coord size = r->size();
            bld->max_bins = size.x * size.y;
        }
        auto & t = settings.gems;
        if (r && r->misc.count("workshop") &&
                r->misc.at("workshop").r->subtype == "Jewelers")
        {
            t.rough_other_mats.clear();
            t.rough_other_mats.resize(NUM_ENUM_VALUES(builtin_mats), false);
            t.cut_other_mats.clear();
            t.cut_other_mats.resize(NUM_ENUM_VALUES(builtin_mats), false);
            t.rough_mats.clear();
            t.rough_mats.resize(world->raws.inorganics.size(), true);
            t.cut_mats.clear();
            t.cut_mats.resize(world->raws.inorganics.size(), false);
        }
        else
        {
            t.rough_other_mats.clear();
            t.rough_other_mats.resize(NUM_ENUM_VALUES(builtin_mats), true);
            t.cut_other_mats.clear();
            t.cut_other_mats.resize(NUM_ENUM_VALUES(builtin_mats), true);
            t.rough_mats.clear();
            t.rough_mats.resize(world->raws.inorganics.size(), true);
            t.cut_mats.clear();
            t.cut_mats.resize(world->raws.inorganics.size(), true);
        }
    }
    else if (subtype == "bars_blocks")
    {
        settings.flags.bits.bars_blocks = 1;
        if (r)
        {
            df::coord size = r->size();
            bld->max_bins = size.x * size.y;
        }
        auto & s = settings.bars_blocks;
        if (r && r->misc.count("workshop") &&
                r->misc.at("workshop").r->subtype == "MetalsmithsForge")
        {
            s.bars_mats.clear();
            s.bars_mats.resize(world->raws.inorganics.size());
            for (size_t i = 0; i < world->raws.inorganics.size(); i++)
            {
                s.bars_mats[i] = world->raws.inorganics[i]->material.flags.is_set(material_flags::IS_METAL);
            }
            s.bars_other_mats.clear();
            s.bars_other_mats.resize(5, false);
            s.bars_other_mats[0] = true; // coal
            settings.allow_organic = false;
        }
        else
        {
            s.bars_mats.clear();
            s.bars_mats.resize(world->raws.inorganics.size(), true);
            // coal / potash / ash / pearlash / soap
            s.bars_other_mats.clear();
            s.bars_other_mats.resize(5, true);
            // green/clear/crystal glass / wood
            s.blocks_other_mats.clear();
            s.blocks_other_mats.resize(4, true);
        }
    }
    else if (subtype == "coins")
    {
        settings.flags.bits.coins = 1;
        auto & t = settings.coins;
        t.mats.clear();
        t.mats.resize(world->raws.inorganics.size(), true);
    }
#undef NUM_ENUM_VALUES
}


bool Plan::construct_farmplot(color_ostream & out, room *r)
{
    const static std::set<df::tiletype_material> allowed_materials =
    {
        tiletype_material::GRASS_DARK,
        tiletype_material::GRASS_LIGHT,
        tiletype_material::SOIL,
    };
    for (int16_t x = r->min.x; x <= r->max.x; x++)
    {
        for (int16_t y = r->min.y; y <= r->max.y; y++)
        {
            for (int16_t z = r->min.z; z <= r->max.z; z++)
            {
                if (!allowed_materials.count(ENUM_ATTR(tiletype, material, *Maps::getTileType(x, y, z))))
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
    Buildings::constructWithItems(bld, {});
    r->misc["bld_id"] = bld->id;
    furnish_room(out, r);
    if (room *st = find_room("stockpile", [r](room *o) -> bool { return o->misc.count("workshop") && o->misc.at("workshop") == r; }))
    {
        digroom(out, st);
    }
    tasks.push_back(new task{horrible_t("setup_farmplot"), horrible_t(r)});
    return true;
}

void Plan::move_dininghall_fromtemp(color_ostream & out, room *r, room *t)
{
    // if we dug a real hall, get rid of the temporary one
    for (furniture *f : t->layout)
    {
        for (furniture *of : r->layout)
        {
            if (of->at("item").str == f->at("item").str && of->at("users").ids.empty())
            {
                of->at("users").ids = f->at("users").ids;
                if (!f->count("ignore"))
                {
                    of->erase("ignore");
                }
                if (f->count("bld_id"))
                {
                    Buildings::deconstruct(df::building::find(f->at("bld_id").id));
                }
                break;
            }
        }
    }
    rooms.erase(std::remove(rooms.begin(), rooms.end(), t), rooms.end());
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
    for (room *a : r->accesspath)
    {
        smooth_room_access(out, a);
    }
}

void Plan::smooth_cistern(color_ostream & out, room *r)
{
    for (room *a : r->accesspath)
    {
        smooth_room_access(out, a);
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

bool Plan::construct_cistern(color_ostream & out, room *r)
{
    furnish_room(out, r);
    smooth_cistern(out, r);

    // remove boulders
    dump_items_access(out, r);

    // build levers for floodgates
    wantdig(out, find_room("well"));

    // check smoothing progress, channel intermediate levels
    if (r->subtype == "well")
    {
        tasks.push_back(new task{horrible_t("dig_cistern"), horrible_t(r)});
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
    for (room *a : r->accesspath)
    {
        found = dump_items_access(out, a) || found;
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
                for (int32_t id : Maps::getTileBlock(x, y, z)->items)
                {
                    df::item *i = df::item::find(id);
                    if (i->flags.bits.on_ground &&
                            r->min.x <= i->pos.x && i->pos.x <= r->max.x &&
                            r->min.y <= i->pos.y && i->pos.y <= r->max.y &&
                            z == i->pos.x)
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
    for (int16_t x = min.x; x <= max.y; x++)
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
    for (df::coord tile : tiles)
    {
        Maps::getTileDesignation(tile)->bits.smooth = 1;
        Maps::getTileBlock(tile)->flags.bits.designated = 1;
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
    size_t cnt = 0;
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
                        if (!is_smooth(df::coord(x + 1, y - 1, z)) ||
                                !is_smooth(df::coord(x + 1, y, z)) ||
                                !is_smooth(df::coord(x + 1, y + 1, z)))
                        {
                            stop = true;
                            continue;
                        }
                        if (ENUM_ATTR(tiletype_shape, basic_shape,
                                ENUM_ATTR(tiletype, shape,
                                    *Maps::getTileType(x - 1, y, z))) ==
                                tiletype_shape_basic::Floor)
                        {
                            dig_tile(df::coord(x, y, z), "Channel");
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
                                dig_tile(df::coord(x, y, z), "Channel");
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
        r->misc["channeled"] = horrible_t();
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
                    r->dig("Channel");
                    tasks.push_back(new task{horrible_t("dig_garbage"), horrible_t(r)});
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
            dig_tile(t, "Default");
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
    r->dig("Channel");
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
    df::building *bld = f->count("bld_id") ?
        df::building::find(f->at("bld_id").id) : nullptr;
    if (!bld)
    {
        // destroyed building?
        return true;
    }
    if (bld->getBuildStage() < bld->getMaxBuildStage())
        return false;

    if (f->at("item") == "coffin")
    {
        df::building_coffinst *coffin = virtual_cast<df::building_coffinst>(bld);
        coffin->burial_mode.bits.allow_burial = 1;
        coffin->burial_mode.bits.no_citizens = 0;
        coffin->burial_mode.bits.no_pets = 1;
    }
    else if (f->at("item") == "door")
    {
        df::building_doorst *door = virtual_cast<df::building_doorst>(bld);
        door->door_flags.bits.pet_passable = 1;
        door->door_flags.bits.internal = f->count("internal") ? 1 : 0;
    }
    else if (f->at("item") == "trap")
    {
        if (f->at("subtype") == "lever")
        {
            return setup_lever(out, r, f);
        }
    }
    else if (f->at("item") == "floodgate")
    {
        if (f->count("way"))
        {
            for (room *rr : rooms)
            {
                if (rr->status == "plan")
                    continue;
                for (furniture *ff : rr->layout)
                {
                    if (ff->at("item") == "trap" &&
                            ff->at("subtype") == "lever" &&
                            ff->at("target").f == f)
                    {
                        link_lever(out, ff, f);
                    }
                }
            }
        }
    }
    else if (f->at("item") == "archerytarget")
    {
        (*f)["makeroom"] = horrible_t();
    }

    if (!f->count("makeroom"))
        return true;
    if (!r->is_dug())
        return false;

    ai->debug(out, "makeroom " + describe_room(r));

    df::coord size = r->size();

    delete[] bld->room.extents;
    bld->room.extents = new uint8_t[(size.x + 2) * (size.y + 2)]();
    bld->room.x = r->min.x - 1;
    bld->room.y = r->min.y - 1;
    bld->room.width = size.x + 2;
    bld->room.height = size.y + 2;
    auto set_ext = [&bld](int16_t x, int16_t y, uint8_t v)
    {
        bld->room.extents[bld->room.width * (y - bld->room.y) + (x - bld->room.x)] = v;
        if (v != 0)
        {
            for (df::building *o : world->buildings.other[buildings_other_id::IN_PLAY])
            {
                if (o->z != bld->z || o->is_room)
                    continue;
                if (o->x1 == x && o->y1 == y)
                {
                    ui->equipment.update.bits.buildings = 1;
                    bld->children.push_back(o);
                    o->parents.push_back(bld);
                }
            }
        }
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
                set_ext(rx, ry, 3);
            }
        }
    }
    for (furniture *f_ : r->layout)
    {
        if (f->at("item") != "door")
            continue;
        int16_t x = r->min.x + (f->count("x") ? f->at("x").id : 0);
        int16_t y = r->min.y + (f->count("y") ? f->at("y").id : 0);
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

    if (r->type == "barracks")
    {
        df::building *bld = r->dfbuilding();
        if (f->at("item") == "archerytarget")
        {
            bld = df::building::find(f->at("bld_id"));
        }
        if (r->misc.count("squad_id") && bld)
        {
            assign_barrack_squad(out, bld, r->misc.at("squad_id").id);
        }
    }

    return true;
}

bool Plan::setup_lever(color_ostream & out, room *r, furniture *f)
{
    if (r->type == "well")
    {
        std::string way = f->at("way").str;
        if (!f->count("target"))
        {
            room *cistern = find_room("cistern", [](room *r) -> bool { return r->subtype == "reserve"; });
            for (furniture *gate : cistern->layout)
            {
                if (gate->at("item") == "floodgate" && gate->at("way") == way)
                {
                    (*f)["target"] = gate;
                    break;
                }
            }
        }

        if (link_lever(out, f, f->at("target").f))
        {
            pull_lever(out, f);
            if (way == "in")
            {
                tasks.push_back(new task{horrible_t("monitor_cistern")});
            }

            return true;
        }
    }
    return false;
}

bool Plan::link_lever(color_ostream & out, furniture *src, furniture *dst)
{
    if (!src->count("bld_id") || !dst->count("bld_id"))
        return false;
    df::building *bld = df::building::find(src->at("bld_id").id);
    if (!bld || bld->getBuildStage() < bld->getMaxBuildStage())
        return false;
    df::building *tbld = df::building::find(dst->at("bld_id").id);
    if (!tbld || tbld->getBuildStage() < tbld->getMaxBuildStage())
        return false;

    for (auto ref : bld->general_refs)
    {
        df::general_ref_building_triggertargetst *tt = virtual_cast<df::general_ref_building_triggertargetst>(ref);
        if (tt && tt->building_id == tbld->id)
            return false;
    }
    for (auto j : bld->jobs)
    {
        if (j->job_type == job_type::LinkBuildingToTrigger)
        {
            for (auto ref : j->general_refs)
            {
                df::general_ref_building_triggertargetst *tt = virtual_cast<df::general_ref_building_triggertargetst>(ref);
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
    df::building *bld = df::building::find(f->at("bld_id").id);
    if (!bld)
    {
        ai->debug(out, "cistern: missing lever " + f->at("way").str);
        return false;
    }
    if (!bld->jobs.empty())
    {
        ai->debug(out, "cistern: lever has job " + f->at("way").str);
        return false;
    }
    ai->debug(out, "cistern: pull lever " + f->at("way").str);

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
        room *well = find_room("well");
        for (furniture *f : well->layout)
        {
            if (f->at("item") == "trap" && f->at("subtype") == "lever")
            {
                if (f->at("way") == "in")
                    m_c_lever_in = f;
                else if (f->at("way") == "out")
                    m_c_lever_out = f;
            }
        }
        m_c_cistern = find_room("cistern", [](room *r) -> bool { return r->subtype == "well"; });
        m_c_reserve = find_room("cistern", [](room *r) -> bool { return r->subtype == "reserve"; });
        if (m_c_reserve->misc.count("channel_enable"))
        {
            m_c_testgate_delay = 2;
        }
    }

    df::building_trapst *l_in = nullptr, *l_out = nullptr;
    df::building_floodgatest *f_in = nullptr, *f_out = nullptr;
    if (m_c_lever_in->count("bld_id"))
        l_in = virtual_cast<df::building_trapst>(df::building::find(m_c_lever_in->at("bld_id").id));
    if (m_c_lever_in->count("target") && m_c_lever_in->at("target").f->count("bld_id"))
            f_in = virtual_cast<df::building_floodgatest>(df::building::find(m_c_lever_in->at("target").f->at("bld_id").id));
    if (m_c_lever_out->count("bld_id"))
        l_out = virtual_cast<df::building_trapst>(df::building::find(m_c_lever_out->at("bld_id").id));
    if (m_c_lever_out->count("target") && m_c_lever_out->at("target").f->count("bld_id"))
            f_out = virtual_cast<df::building_floodgatest>(df::building::find(m_c_lever_out->at("target").f->at("bld_id").id));

    if (l_in && !f_out && !l_in->linked_mechanisms.empty())
    {
        // f_in is linked, can furnish f_out now without risking walling
        // workers in the reserve
        for (furniture *l : m_c_reserve->layout)
        {
            l->erase("ignore");
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
    for (furniture *f : fort_entrance->layout)
    {
        if (f->erase("ignore"))
        {
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

        df::coord gate = m_c_reserve->misc.at("channel_enable").c;
        if (ENUM_ATTR(tiletype_shape, basic_shape,
                    ENUM_ATTR(tiletype, shape,
                        *Maps::getTileType(gate))) ==
                tiletype_shape_basic::Wall)
        {
            ai->debug(out, "cistern: test channel");
            bool empty = true;
            std::list<room *> todo = {m_c_reserve};
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
                            if (!is_smooth(t))
                            {
                                ai->debug(out, stl_sprintf("cistern: unsmoothed (%d, %d, %d)", x - r->min.x, y - r->min.y, z - r->min.z));
                                empty = false;
                                break;
                            }
                            df::tile_occupancy occ = *Maps::getTileOccupancy(t);
                            if (occ.bits.unit || occ.bits.unit_grounded)
                            {
                                for (df::unit *u : world->units.active)
                                {
                                    if (Units::getPosition(u) == t)
                                    {
                                        ai->debug(out, stl_sprintf("cistern: unit (%d, %d, %d) %s", x - r->min.x, y - r->min.y, z - r->min.z, AI::describe_unit(u).c_str()));
                                        break;
                                    }
                                }
                                empty = false;
                                break;
                            }
                            if (occ.bits.item)
                            {
                                for (int32_t id : Maps::getTileBlock(t)->items)
                                {
                                    df::item *i = df::item::find(id);
                                    if (Items::getPosition(i) == t)
                                    {
                                        ai->debug(out, stl_sprintf("cistern: item (%d, %d, %d) %s", x - r->min.x, y - r->min.y, z - r->min.z, AI::describe_item(i).c_str()));
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
                    dig_tile(gate + df::coord(0, 0, 1), "Channel");
                    m_c_testgate_delay = 16;
                }
            }
            else
            {
                room *well = find_room("well");
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
                    if (spiral_search(gate + df::coord(x, y, 0), 1, [](df::coord ttt) -> bool { return Maps::getTileDesignation(ttt)->bits.feature_local; }).isValid())
                    {
                        dig_tile(gate + df::coord(x, y, 1), "Channel");
                    }
                }
            }
        }
        return;
    }

    if (!m_c_cistern->misc.count("channeled"))
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
    for (auto f : world->features.map_features)
    {
        ifeat = virtual_cast<df::feature_init_outdoor_riverst>(f);
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
    for (furniture *i : fort_entrance->layout)
    {
        (*i)["z"] = surface_tile_at(fort_entrance->min.x + i->at("x").id, fort_entrance->min.y + i->at("y").id, true).z - fort_entrance->min.z;
    }
    fort_entrance->layout.erase(std::remove_if(fort_entrance->layout.begin(), fort_entrance->layout.end(), [this](furniture *i) -> bool
                {
                    df::coord t = fort_entrance->min + df::coord(i->at("x").id, i->at("y").id, i->at("z").id - 1);
                    df::tiletype tt = *Maps::getTileType(t);
                    df::tiletype_material tm = ENUM_ATTR(tiletype, material, tt);
                    if (ENUM_ATTR(tiletype_shape, basic_shape, ENUM_ATTR(tiletype, shape, tt)) != tiletype_shape_basic::Wall || (tm != tiletype_material::STONE && tm != tiletype_material::MINERAL && tm != tiletype_material::SOIL && tm != tiletype_material::ROOT && (!allow_ice || tm != tiletype_material::FROZEN_LIQUID)))
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

                river = spiral_search(river, [this](df::coord t) -> bool
                        {
                            // TODO rooms outline
                            int16_t y = fort_entrance->pos().y;
                            return (t.y < y + MinY || t.y > y + MaxY) && Maps::getTileDesignation(t)->bits.feature_local;
                        });
                if (!river.isValid())
                    return true;

                // find a safe place for the first tile
                df::coord t1 = spiral_search(river, [this](df::coord t) -> bool
                        {
                            if (!map_tile_in_rock(t))
                                return false;
                            df::coord st = surface_tile_at(t.x, t.y);
                            return map_tile_in_rock(st + df::coord(0, 0, -1));
                        });
                if (!t1.isValid())
                    return true;
                t1 = surface_tile_at(t1.x, t1.y);

                // if the game hasn't done any pathfinding yet, wait until the next frame and try again
                uint16_t t1w = Maps::getTileBlock(t1)->walkable[t1.x & 0xf][t1.y & 0xf];
                if (t1w == 0)
                    return false;

                // find the second tile
                df::coord t2 = spiral_search(t1, [this, t1w](df::coord t) -> bool
                        {
                            df::map_block *block = Maps::getTileBlock(t);
                            if (!block)
                            {
                                return false;
                            }
                            int16_t tw = block->walkable[t.x & 0xf][t.y & 0xf];
                            return tw != 0 and tw != t1w;
                        });
                if (!t2.isValid())
                    return true;
                int16_t t2w = Maps::getTileBlock(t2)->walkable[t2.x & 0xf][t2.y & 0xf];

                // make sure the second tile is in a safe place
                t2 = spiral_search(t2, [this, t2w](df::coord t) -> bool
                        {
                            df::map_block *block = Maps::getTileBlock(t);
                            return block && block->walkable[t.x & 0xf][t.y & 0xf] == t2w && map_tile_in_rock(t - df::coord(0, 0, 1));
                        });
                if (!t2.isValid())
                    return true;

                // find the bottom of the staircases
                int16_t z;
                for (auto f : world->features.map_features)
                {
                    df::feature_init_outdoor_riverst *r = virtual_cast<df::feature_init_outdoor_riverst>(f);
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

                for (room *c : cor)
                {
                    corridors.push_back(c);
                    wantdig(out, c);
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
                auto & block = world->map.block_index[xb][yb][z];
                for (auto event : block->block_events)
                {
                    df::block_square_event_mineralst *vein = virtual_cast<df::block_square_event_mineralst>(event);
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
size_t Plan::dig_vein(color_ostream & out, int32_t mat, size_t want_boulders)
{
    // mat => [x, y, z, dig_mode] marked for to dig
    size_t count = 0;
    // check previously queued veins
    if (map_vein_queue.count(mat))
    {
        auto & q = map_vein_queue.at(mat);
        q.erase(std::remove_if(q.begin(), q.end(), [this, mat, &count, &out](std::pair<df::coord, std::string> d) -> bool
                    {
                        df::tiletype tt = *Maps::getTileType(d.first);
                        df::tiletype_shape_basic sb = ENUM_ATTR(tiletype_shape, basic_shape, ENUM_ATTR(tiletype, shape, tt));
                        if (sb == tiletype_shape_basic::Open)
                        {
                            furniture f{{"construction", horrible_t(d.second == "Default" ? "Floor" : d.second)}};
                            return try_furnish_construction(out, nullptr, &f, d.first);
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
        size_t cnt = do_dig_vein(out, mat, v);
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

size_t Plan::do_dig_vein(color_ostream & out, int32_t mat, df::coord b)
{
    ai->debug(out, "dig_vein " + world->raws.inorganics[mat]->id);
    size_t count = 0;
    int16_t fort_minz = 0x7fff;
    for (room *c : corridors)
    {
        if (c->subtype != "veinshaft")
        {
            fort_minz = std::min(fort_minz, c->min.z);
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
    std::vector<std::pair<df::coord, std::string>> todo;
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
                    todo.push_back(std::make_pair(t, "Default"));
                    if (ENUM_ATTR(tiletype, material, *Maps::getTileType(t)) == tiletype_material::MINERAL && mat_index_vein(t) == mat)
                        count++;
                    need_shaft = ns;
                }
            }
        }
    }
    for (auto d : todo)
    {
        q.push_back(d);
        dig_tile(d.first, d.second);
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
            dig_tile(t0, "Default");
            q.push_back(std::make_pair(t0, "Default"));
            if (t0.y > vert.y)
                t0.y--;
            else
                t0.y++;
        }
        while (t0.x != vert.x)
        {
            dig_tile(t0, "Default");
            q.push_back(std::make_pair(t0, "Default"));
            if (t0.x > vert.x)
                t0.x--;
            else
                t0.x++;
        }
        while (Maps::getTileDesignation(t0)->bits.hidden)
        {
            if (t0.z % 16 == 0)
            {
                dig_tile(t0, "DownStair");
                q.push_back(std::make_pair(t0, "DownStair"));
                if (t0.z % 32 >= 16)
                    t0.x++;
                else
                    t0.x--;
                dig_tile(t0, "UpStair");
                q.push_back(std::make_pair(t0, "UpStair"));
            }
            else
            {
                dig_tile(t0, "UpDownStair");
                q.push_back(std::make_pair(t0, "UpDownStair"));
            }
            t0.z++;
        }
        if (!ENUM_ATTR(tiletype_shape, passable_low, ENUM_ATTR(tiletype, shape, *Maps::getTileType(t0))) && Maps::getTileDesignation(t0)->bits.dig == tile_dig_designation::No)
        {
            dig_tile(t0, "DownStair");
            q.push_back(std::make_pair(t0, "DownStair"));
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
    const static std::vector<df::coord> sides{df::coord(0, 1, 0), df::coord(1, 0, 0), df::coord(0, -1, 0), df::coord(-1, 0, 0)};
    for (int16_t r = min; r < max; r += step)
    {
        for (df::coord d : sides)
        {
            df::coord tt = t + d * r;
            if (b(tt))
                return tt;
        }

        for (size_t s = 0; s < sides.size(); s++)
        {
            df::coord dr = sides[(s + sides.size() - 1) % sides.size()];
            df::coord dv = sides[s];

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

        ai->debug(out, "Can't find a fortress entrance spot. We need a 3x5 flat area with solid ground for at least 2 tiles on each side.");
        return CR_FAILURE;
    }

    df::coord ent = surface_tile_at(ent0.x, ent0.y);

    fort_entrance = new room(ent - df::coord(0, 1, 0), ent + df::coord(0, 1, 0));
    for (int i = 0; i < 3; i++)
    {
        fort_entrance->layout.push_back(new furniture{
                    {"item", horrible_t("trap")},
                    {"subtype", horrible_t("cage")},
                    {"x", horrible_t(i - 1)},
                    {"y", horrible_t(-1)},
                    {"ignore", horrible_t()},
                });
        fort_entrance->layout.push_back(new furniture{
                    {"item", horrible_t("trap")},
                    {"subtype", horrible_t("cage")},
                    {"x", horrible_t(1)},
                    {"y", horrible_t(i)},
                    {"ignore", horrible_t()},
                });
        fort_entrance->layout.push_back(new furniture{
                    {"item", horrible_t("trap")},
                    {"subtype", horrible_t("cage")},
                    {"x", horrible_t(1 - i)},
                    {"y", horrible_t(3)},
                    {"ignore", horrible_t()},
                });
        fort_entrance->layout.push_back(new furniture{
                    {"item", horrible_t("trap")},
                    {"subtype", horrible_t("cage")},
                    {"x", horrible_t(-1)},
                    {"y", horrible_t(2 - i)},
                    {"ignore", horrible_t()},
                });
    }
    for (int i = 0; i < 5; i++)
    {
        fort_entrance->layout.push_back(new furniture{
                    {"item", horrible_t("trap")},
                    {"subtype", horrible_t("cage")},
                    {"x", horrible_t(i - 2)},
                    {"y", horrible_t(-2)},
                    {"ignore", horrible_t()},
                });
        fort_entrance->layout.push_back(new furniture{
                    {"item", horrible_t("trap")},
                    {"subtype", horrible_t("cage")},
                    {"x", horrible_t(2)},
                    {"y", horrible_t(i - 1)},
                    {"ignore", horrible_t()},
                });
        fort_entrance->layout.push_back(new furniture{
                    {"item", horrible_t("trap")},
                    {"subtype", horrible_t("cage")},
                    {"x", horrible_t(2 - i)},
                    {"y", horrible_t(4)},
                    {"ignore", horrible_t()},
                });
        fort_entrance->layout.push_back(new furniture{
                    {"item", horrible_t("trap")},
                    {"subtype", horrible_t("cage")},
                    {"x", horrible_t(-2)},
                    {"y", horrible_t(3 - i)},
                    {"ignore", horrible_t()},
                });
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

    ai->debug(out, "Too many caverns, cant find room for fort. We need more minerals!");
    return CR_FAILURE;
}

// assign rooms in the space found by scan_fort_*
command_result Plan::setup_blueprint_rooms(color_ostream & out)
{
    // hardcoded layout
    corridors.push_back(fort_entrance);

    df::coord f = fort_entrance->pos();

    command_result res;

    f.z = fort_entrance->min.z;
    res = setup_blueprint_workshops(out, f, {fort_entrance});
    if (res != CR_OK)
        return res;
    ai->debug(out, "workshop floor ready");

    fort_entrance->min.z--;
    f.z--;
    res = setup_blueprint_utilities(out, f, {fort_entrance});
    if (res != CR_OK)
        return res;
    ai->debug(out, "utility floor ready");

    fort_entrance->min.z--;
    f.z--;
    res = setup_blueprint_stockpiles(out, f, {fort_entrance});
    if (res != CR_OK)
        return res;
    ai->debug(out, "stockpile floor ready");

    for (size_t i = 0; i < 2; i++)
    {
        fort_entrance->min.z--;
        f.z--;
        res = setup_blueprint_bedrooms(out, f, {fort_entrance});
        if (res != CR_OK)
            return res;
        ai->debug(out, stl_sprintf("bedroom floor ready %d/2", i + 1));
    }

    return CR_OK;
}

command_result Plan::setup_blueprint_workshops(color_ostream & out, df::coord f, std::vector<room *> entr)
{
    room *corridor_center0 = new room(f + df::coord(-1, -1, 0), f + df::coord(-1, 1, 0));
    corridor_center0->layout.push_back(new furniture{
                {"item", horrible_t("door")},
                {"x", horrible_t(-1)},
                {"y", horrible_t(0)},
            });
    corridor_center0->layout.push_back(new furniture{
                {"item", horrible_t("door")},
                {"x", horrible_t(-1)},
                {"y", horrible_t(2)},
            });
    corridor_center0->accesspath = entr;
    corridors.push_back(corridor_center0);

    room *corridor_center2 = new room(f + df::coord(1, -1, 0), f + df::coord(1, 1, 0));
    corridor_center2->layout.push_back(new furniture{
                {"item", horrible_t("door")},
                {"x", horrible_t(1)},
                {"y", horrible_t(0)},
            });
    corridor_center2->layout.push_back(new furniture{
                {"item", horrible_t("door")},
                {"x", horrible_t(1)},
                {"y", horrible_t(2)},
            });
    corridor_center2->accesspath = entr;
    corridors.push_back(corridor_center2);

    // Millstone, Siege, magma workshops/furnaces
    std::vector<std::string> types{
        "Still", "Kitchen", "Fishery", "Butchers", "Leatherworks", "Tanners",
        "Loom", "Clothiers", "Dyers", "Bowyers", "", "Kiln",
        "Masons", "Carpenters", "Mechanics", "Farmers", "Craftsdwarfs", "Jewelers",
        "Smelter", "MetalsmithsForge", "Ashery", "WoodFurnace", "SoapMaker", "GlassFurnace",
    };
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
                r->misc["workshop_level"] = 0;
                rooms.push_back(r);

                c.x = cx - 6;
                r = new room("workshop", "Quern", c, c);
                r->accesspath.push_back(cor_x);
                r->misc["workshop_level"] = 1;
                rooms.push_back(r);

                c.x = cx + 2;
                r = new room("workshop", "Quern", c, c);
                r->accesspath.push_back(cor_x);
                r->misc["workshop_level"] = 2;
                rooms.push_back(r);

                c.x = cx - 2;
                r = new room("workshop", "ScrewPress", c, c);
                r->accesspath.push_back(cor_x);
                r->misc["workshop_level"] = 0;
                rooms.push_back(r);
            }

            std::string t = *type_it++;

            room *r = new room("workshop", t, df::coord(cx - 1, f.y - 5, f.z), df::coord(cx + 1, f.y - 3, f.z));
            r->accesspath.push_back(cor_x);
            r->layout.push_back(new furniture{
                        {"item", horrible_t("door")},
                        {"x", horrible_t(1)},
                        {"y", horrible_t(3)},
                    });
            r->misc["workshop_level"] = 0;
            if (dirx == -1 && dx == 1)
            {
                r->layout.push_back(new furniture{
                            {"item", horrible_t("nestbox")},
                            {"x", horrible_t(-1)},
                            {"y", horrible_t(4)},
                        });
            }
            rooms.push_back(r);

            r = new room("workshop", t, df::coord(cx - 1, f.y - 8, f.z), df::coord(cx + 1, f.y - 6, f.z));
            r->accesspath.push_back(rooms.back());
            r->misc["workshop_level"] = 1;
            rooms.push_back(r);

            r = new room("workshop", t, df::coord(cx - 1, f.y - 11, f.z), df::coord(cx + 1, f.y - 9, f.z));
            r->accesspath.push_back(rooms.back());
            r->misc["workshop_level"] = 2;
            rooms.push_back(r);

            t = *type_it++;
            r = new room("workshop", t, df::coord(cx - 1, f.y + 3, f.z), df::coord(cx + 1, f.y + 5, f.z));
            r->accesspath.push_back(cor_x);
            r->layout.push_back(new furniture{
                        {"item", horrible_t("door")},
                        {"x", horrible_t(1)},
                        {"y", horrible_t(-1)},
                    });
            r->misc["workshop_level"] = 0;
            if (dirx == -1 && dx == 1)
            {
                r->layout.push_back(new furniture{
                            {"item", horrible_t("nestbox")},
                            {"x", horrible_t(-1)},
                            {"y", horrible_t(-2)},
                        });
            }
            rooms.push_back(r);

            r = new room("workshop", t, df::coord(cx - 1, f.y + 6, f.z), df::coord(cx + 1, f.y + 8, f.z));
            r->accesspath.push_back(rooms.back());
            r->misc["workshop_level"] = 1;
            rooms.push_back(r);

            r = new room("workshop", t, df::coord(cx - 1, f.y + 9, f.z), df::coord(cx + 1, f.y + 11, f.z));
            r->accesspath.push_back(rooms.back());
            r->misc["workshop_level"] = 2;
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
        r->misc["workshop_level"] = 0;
        r->layout.push_back(new furniture{
                    {"dig", horrible_t("Ramp")},
                    {"x", horrible_t(-1)},
                    {"y", horrible_t(1)},
                });
        r->layout.push_back(new furniture{
                    {"dig", horrible_t("Ramp")},
                    {"x", horrible_t(-1)},
                    {"y", horrible_t(2)},
                });
        r->layout.push_back(new furniture{
                    {"dig", horrible_t("Ramp")},
                    {"x", horrible_t(-1)},
                    {"y", horrible_t(3)},
                });
        rooms.push_back(r);
    }
    else
    {
        room *r = new room("workshop", "TradeDepot", df::coord(f.x - 7, f.y - 2, fort_entrance->max.z), df::coord(f.x - 3, f.y + 2, fort_entrance->max.z));
        r->misc["workshop_level"] = 0;
        for (int16_t x = 0; x < 5; x++)
        {
            for (int16_t y = 0; y < 5; y++)
            {
                r->layout.push_back(new furniture{
                            {"construction", horrible_t("Floor")},
                            {"x", horrible_t(x)},
                            {"y", horrible_t(y)},
                        });
            }
        }
        rooms.push_back(r);
    }
    return CR_OK;
}

command_result Plan::setup_blueprint_stockpiles(color_ostream & out, df::coord f, std::vector<room *> entr)
{
    room *corridor_center0 = new room(f + df::coord(-1, -1, 0), f + df::coord(-1, 1, 0));
    corridor_center0->layout.push_back(new furniture{
                {"item", horrible_t("door")},
                {"x", horrible_t(-1)},
                {"y", horrible_t(0)},
            });
    corridor_center0->layout.push_back(new furniture{
                {"item", horrible_t("door")},
                {"x", horrible_t(-1)},
                {"y", horrible_t(2)},
            });
    corridor_center0->accesspath = entr;
    corridors.push_back(corridor_center0);

    room *corridor_center2 = new room(f + df::coord(1, -1, 0), f + df::coord(1, 1, 0));
    corridor_center2->layout.push_back(new furniture{
                {"item", horrible_t("door")},
                {"x", horrible_t(1)},
                {"y", horrible_t(0)},
            });
    corridor_center2->layout.push_back(new furniture{
                {"item", horrible_t("door")},
                {"x", horrible_t(1)},
                {"y", horrible_t(2)},
            });

    std::vector<std::string> types{
        "food", "furniture", "wood", "stone", "refuse", "animals", "corpses", "gems",
        "finished_goods", "cloth", "bars_blocks", "leather", "ammo", "armor", "weapons", "coins",
    };
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
            r0->misc["stockpile_level"] = 1;
            r1->misc["stockpile_level"] = 1;
            r0->accesspath.push_back(cor_x);
            r1->accesspath.push_back(cor_x);
            r0->layout.push_back(new furniture{
                        {"item", horrible_t("door")},
                        {"x", horrible_t(2)},
                        {"y", horrible_t(2)},
                    });
            r0->layout.push_back(new furniture{
                        {"item", horrible_t("door")},
                        {"x", horrible_t(4)},
                        {"y", horrible_t(2)},
                    });
            r1->layout.push_back(new furniture{
                        {"item", horrible_t("door")},
                        {"x", horrible_t(2)},
                        {"y", horrible_t(-1)},
                    });
            r1->layout.push_back(new furniture{
                        {"item", horrible_t("door")},
                        {"x", horrible_t(4)},
                        {"y", horrible_t(-1)},
                    });
            rooms.push_back(r0);
            rooms.push_back(r1);

            r0 = new room("stockpile", t0, df::coord(cx - 3, f.y - 11, f.z), df::coord(cx + 3, f.y - 5, f.z));
            r1 = new room("stockpile", t0, df::coord(cx - 3, f.y + 5, f.z), df::coord(cx + 3, f.y + 11, f.z));
            r0->misc["stockpile_level"] = 2;
            r1->misc["stockpile_level"] = 2;
            rooms.push_back(r0);
            rooms.push_back(r1);

            r0 = new room("stockpile", t0, df::coord(cx - 3, f.y - 20, f.z), df::coord(cx + 3, f.y - 12, f.z));
            r1 = new room("stockpile", t0, df::coord(cx - 3, f.y + 12, f.z), df::coord(cx + 3, f.y + 20, f.z));
            r0->misc["stockpile_level"] = 3;
            r1->misc["stockpile_level"] = 3;
            rooms.push_back(r0);
            rooms.push_back(r1);
        }
    }
    for (room *r : rooms)
    {
        if (r->type == "stockpile" && r->subtype == "coins" &&
                r->misc.at("stockpile_level").id > 1)
        {
            r->subtype = "furniture";
            r->misc["stockpile_level"].id += 2;
        }
    }

    command_result res;
    res = setup_blueprint_minecarts(out);
    if (res != CR_OK)
        return res;
    res = setup_blueprint_pitcage(out);
    if (res != CR_OK)
        return res;
    return CR_OK;
}

command_result Plan::setup_blueprint_minecarts(color_ostream & out)
{
#if 0
    // disabled 2015-02-04 BenLubar

    last_stone = @rooms.find_all { |r| r.type == :stockpile and r.subtype == :stone }.last
    return if not last_stone

    rx = last_stone.x
    ry = last_stone.y + (last_stone.y > @fort_entrance.y ? 6 : -6)
    rz = last_stone.z
    dirx = (rx > @fort_entrance.x ? 1 : -1)
    diry = (ry > @fort_entrance.y ? 1 : -1)
    x1 = (dirx > 0 ? 1 : 0)
    y1 = (diry > 0 ? 1 : 0)

    # minecart dumping microstockpile
    r = Room.new(:stockpile, :stone, rx, rx, ry, ry+diry, rz)
    r.layout << { :item => :door, :x => 0, :y => (diry > 0 ? -1 : 2) }
    r.misc[:stockpile_level] = last_stone.misc[:stockpile_level] + 1
    r.accesspath = [last_stone]
    @rooms << r

    # cartway: dumping side (end of jump, trackstop dump, fall into channel to reset path
    r1 = Room.new(:cartway, :fort, rx-dirx, rx-2*dirx, ry, ry+diry, rz)
    r1.layout << { :construction => :Track, :x =>   x1, :y =>   y1, :dir => [:n, :s], :dig => :Ramp }
    r1.layout << { :construction => :Track, :x =>   x1, :y => 1-y1, :dir => [(dirx > 0 ? :w : :e), (diry > 0 ? :s : :n)] }
    r1.layout << { :construction => :Track, :x => 1-x1, :y => 1-y1, :dir => [(dirx > 0 ? :e : :w), (diry > 0 ? :s : :n)] }
    r1.layout << { :construction => :Track, :x => 1-x1, :y =>   y1, :dir => [:n, :s] }
    r1.layout << { :item => :trap, :subtype => :trackstop, :x => x1, :y => 1-y1,
        :misc_bldprops => { :friction => 10, :dump_x_shift => dirx, :dump_y_shift => 0, :use_dump => 1 } }
    r1.accesspath = [@rooms.last]

    ry += 2*diry
    ey = (diry > 0 ? (2*df.world.map.y_count+ry)/3 : ry/3)

    corr = Corridor.new(rx, rx, ry+3*diry, ry+3*diry, rz-1, rz)
    corr.z2 += 1 while map_tile_in_rock(corr.maptile2)
    @corridors << corr

    # power shaft + windmill to power the roller
    rshaft = Room.new(:cartway, :shaft, rx, rx, ry+4*diry, ry+5*diry, rz)
    rshaft.accesspath = [corr]
    rshaft.z2 += 1 while map_tile_in_rock(rshaft.maptile2)
    rshaft.layout << { :item => :gear_assembly, :x => 0, :y => (diry > 0 ? 1 : 0), :z => 0 }
    rshaft.layout << { :item => :roller, :x => -dirx,    :y => (diry > 0 ? 1 : 0), :z => 0,
            :misc_bldprops => {:speed => 50000, :direction => (diry > 0 ? :FromSouth : :FromNorth)} }
    (1...rshaft.h_z-1).each { |i|
        rshaft.layout << { :item => :vertical_axle, :x => 0, :y => (diry > 0 ? 1 : 0), :z => i, :dig => :Channel }
    }
    rshaft.layout << { :item => :windmill, :x => 0, :y => (diry > 0 ? 1 : 0), :z => rshaft.h_z-1, :dig => :Channel }

    # main 'outside' room with minecart jump (ramp), air path, and way back on track (from trackstop channel)
    # 2xYx2 dimensions + cheat with layout
    # fitting tightly within r1+rshaft
    # use :dig => :Ramp hack to channel z+1
    r2 = Room.new(:cartway, :out, rx-dirx, rx-2*dirx, ry, ey, rz-1)
    r2.z2 += 1
    r2.accesspath = [corr]
    dx0 = (dirx>0 ? 0 : 1)
    # part near fort
    # air gap
    r2.layout << { :construction => :NoRamp, :x =>   dx0, :y => (diry > 0 ? 0 : r2.h-1 ), :z => 0, :dig => :Ramp }
    r2.layout << { :construction => :NoRamp, :x => 1-dx0, :y => (diry > 0 ? 0 : r2.h-1 ), :z => 0, :dig => :Ramp }
    # z-1: channel fall reception
    (1..4).each { |i|
        r2.layout << { :construction => :Track,  :x =>   dx0, :y => (diry > 0 ? i : r2.h-1-i ), :z => 0, :dig => :Ramp, :dir => [:n, :s] }
        r2.layout << { :construction => :NoRamp, :x => 1-dx0, :y => (diry > 0 ? i : r2.h-1-i ), :z => 0, :dig => :Ramp } if i < 3
    }
    # z: jump
    r2.layout << { :construction => :Wall, :x => 1-dx0, :y => (diry > 0 ? 3 : r2.h-4), :z => 1, :dig => :No }
    r2.layout << { :construction => :TrackRamp, :x => 1-dx0, :y => (diry > 0 ? 4 : r2.h-5), :z => 1, :dir => [:n, :s], :dig => :Ramp }
    # z+1: airway
    r2.layout << { :x => 1-dx0, :y => (diry > 0 ? 0 : r2.h-1), :z => 2, :dig => :Channel }
    r2.layout << { :x => 1-dx0, :y => (diry > 0 ? 1 : r2.h-2), :z => 2, :dig => :Channel }
    r2.layout << { :x => 1-dx0, :y => (diry > 0 ? 2 : r2.h-3), :z => 2, :dig => :Channel }
    r2.layout << { :x => (dirx>0 ? 2 : -1), :y => (diry > 0 ? 1 : r2.h-2), :z => 2 } # access to dig preceding Channels from rshaft
    r2.layout << { :x => (dirx>0 ? 2 : -1), :y => (diry > 0 ? 2 : r2.h-3), :z => 2 }
    r2.layout << { :construction => :Track, :x => 1-dx0, :y => (diry > 0 ? 3 : r2.h-4), :z => 2, :dir => [:n, :s] }
    # main track
    (5..(r2.h-2)).each { |i|
        r2.layout << { :construction => :Track, :x =>   dx0, :y => (diry > 0 ? i : r2.h-1-i), :z => 0, :dir => [:n, :s] }
        r2.layout << { :construction => :Track, :x => 1-dx0, :y => (diry > 0 ? i : r2.h-1-i), :z => 1, :dir => [:n, :s] }
    }
    # track termination + manual reset ramp
    r2.layout << { :construction => :Ramp, :x =>   dx0, :y => (diry > 0 ? r2.h-1 : 0), :z => 0, :dig => :Ramp }
    r2.layout << { :construction => :Wall, :x => 1-dx0, :y => (diry > 0 ? r2.h-1 : 0), :z => 0, :dig => :No }
    r2.layout << { :construction => :Track, :x => 1-dx0, :y => (diry > 0 ? r2.h-1 : 0), :z => 1, :dir => [(diry > 0 ? :n : :s)] }

    # order matters to prevent unauthorized access to fortress (eg temporary ramps)
    @rooms << rshaft << r2 << r1

    corr = Corridor.new(rx-dirx, rx-dirx, ey+4*diry, ey+4*diry, rz, rz)
    corr.z2 += 1 while map_tile_in_rock(corr.maptile2)
    @corridors << corr
    st = Room.new(:stockpile, :stone, rx-dirx, rx-dirx, ey+diry, ey+3*diry, rz)
    st.misc[:stockpile_level] = 100
    st.layout << { :item => :minecart_route, :x => 0, :y => (diry > 0 ? -1 : 3), :direction => (diry > 0 ? :North : :South) }
    st.accesspath = [corr]
    @rooms << st
    [1, -1, 2, -2].each { |dx|
        st = Room.new(:stockpile, :stone, rx-dirx+dx, rx-dirx+dx, ey+diry, ey+3*diry, rz)
        st.misc[:stockpile_level] = 101
        st.accesspath = [@rooms.last]
        @rooms << st
    }
#endif
    return CR_OK;
}

command_result Plan::setup_blueprint_pitcage(color_ostream & out)
{
    room *gpit = find_room("garbagepit");
    if (!gpit)
        return CR_OK;
    auto layout = [](room *r)
    {
        r->layout.push_back(new furniture{
                    {"construction", horrible_t("UpStair")},
                    {"x", horrible_t(-1)},
                    {"y", horrible_t(1)},
                    {"z", horrible_t(-10)},
                });
        for (int16_t z = -9; z <= -1; z++)
        {
            r->layout.push_back(new furniture{
                        {"construction", horrible_t("UpDownStair")},
                        {"x", horrible_t(-1)},
                        {"y", horrible_t(1)},
                        {"z", horrible_t(z)},
                    });
        }
        r->layout.push_back(new furniture{
                    {"construction", horrible_t("DownStair")},
                    {"x", horrible_t(-1)},
                    {"y", horrible_t(1)},
                    {"z", horrible_t(0)},
                });
        std::vector<int16_t> dxs{0, 1, 2};
        std::vector<int16_t> dys{1, 0, 2};
        for (int16_t dx : dxs)
        {
            for (int16_t dy : dys)
            {
                if (dx == 1 && dy == 1)
                {
                    r->layout.push_back(new furniture{
                                {"dig", horrible_t("Channel")},
                                {"x", horrible_t(dx)},
                                {"y", horrible_t(dy)},
                            });
                }
                else
                {
                    r->layout.push_back(new furniture{
                                {"construction", horrible_t("Floor")},
                                {"x", horrible_t(dx)},
                                {"y", horrible_t(dy)},
                            });
                }
            }
        }
        r->layout.push_back(new furniture{
                    {"construction", horrible_t("Floor")},
                    {"x", horrible_t(3)},
                    {"y", horrible_t(1)},
                    {"item", horrible_t("hive")},
                });
    };

    room *r = new room("pitcage", "", gpit->min + df::coord(-1, -1, 10), gpit->min + df::coord(1, 1, 10));
    layout(r);
    rooms.push_back(r);

    room *stockpile = new room("stockpile", "animals", r->min, r->max);
    stockpile->misc["stockpile_level"] = 0;
    layout(stockpile);
    rooms.push_back(stockpile);

    return CR_OK;
}

command_result Plan::setup_blueprint_utilities(color_ostream & out, df::coord f, std::vector<room *> entr)
{
    room *corridor_center0 = new room(f + df::coord(-1, -1, 0), f + df::coord(-1, 1, 0));
    corridor_center0->layout.push_back(new furniture{
                {"item", horrible_t("door")},
                {"x", horrible_t(-1)},
                {"y", horrible_t(0)},
            });
    corridor_center0->layout.push_back(new furniture{
                {"item", horrible_t("door")},
                {"x", horrible_t(-1)},
                {"y", horrible_t(2)},
            });
    corridor_center0->accesspath = entr;
    corridors.push_back(corridor_center0);

    room *corridor_center2 = new room(f + df::coord(1, -1, 0), f + df::coord(1, 1, 0));
    corridor_center2->layout.push_back(new furniture{
                {"item", horrible_t("door")},
                {"x", horrible_t(1)},
                {"y", horrible_t(0)},
            });
    corridor_center2->layout.push_back(new furniture{
                {"item", horrible_t("door")},
                {"x", horrible_t(1)},
                {"y", horrible_t(2)},
            });
    corridor_center2->accesspath = entr;
    corridors.push_back(corridor_center2);

    // dining halls
    int16_t ocx = f.x - 2;
    room *old_cor = corridor_center0;

    // temporary dininghall, for fort start
    room *tmp = new room("dininghall", "", f + df::coord(-4, -1, 0), f + df::coord(-3, 1, 0));
    tmp->misc["temporary"] = horrible_t();
    for (int16_t dy = 0; dy <= 2; dy++)
    {
        tmp->layout.push_back(new furniture{
                    {"item", horrible_t("table")},
                    {"x", horrible_t(0)},
                    {"y", horrible_t(dy)},
                    {"users", horrible_t(std::set<int32_t>())},
                });
        tmp->layout.push_back(new furniture{
                    {"item", horrible_t("chair")},
                    {"x", horrible_t(1)},
                    {"y", horrible_t(dy)},
                    {"users", horrible_t(std::set<int32_t>())},
                });
    }
    (*tmp->layout[0])["makeroom"] = horrible_t();
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

        for (int16_t dy = -1; dy <= 1; dy += 2)
        {
            room *dinner = new room("dininghall", "", df::coord(f.x - ax * 12 - 2 - 10, f.y + dy * 9, f.z), df::coord(f.x - ax * 12 - 2, f.y + dy * 3, f.z));
            dinner->layout.push_back(new furniture{
                        {"item", horrible_t("door")},
                        {"x", horrible_t(7)},
                        {"y", horrible_t(dy > 0 ? -1 : 7)},
                    });
            dinner->layout.push_back(new furniture{
                        {"construction", horrible_t("Wall")},
                        {"dig", horrible_t("No")},
                        {"x", horrible_t(2)},
                        {"y", horrible_t(3)},
                    });
            dinner->layout.push_back(new furniture{
                        {"construction", horrible_t("Wall")},
                        {"dig", horrible_t("No")},
                        {"x", horrible_t(8)},
                        {"y", horrible_t(3)},
                    });
            const static std::vector<int16_t> dxs{5, 3, 6, 3, 7, 2, 8, 1, 9};
            for (int16_t dx : dxs)
            {
                for (int16_t sy = -1; sy <= 1; sy += 2)
                {
                    dinner->layout.push_back(new furniture{
                                {"item", horrible_t("table")},
                                {"x", horrible_t(dx)},
                                {"y", horrible_t(3 + dy * sy * 1)},
                                {"ignore", horrible_t()},
                                {"users", horrible_t(std::set<int32_t>())},
                            });
                    dinner->layout.push_back(new furniture{
                                {"item", horrible_t("chair")},
                                {"x", horrible_t(dx)},
                                {"y", horrible_t(3 + dy * sy * 2)},
                                {"ignore", horrible_t()},
                                {"users", horrible_t(std::set<int32_t>())},
                            });
                }
            }
            for (furniture *f : dinner->layout)
            {
                if (f->count("item") && f->at("item") == "table")
                {
                    (*f)["makeroom"] = horrible_t();
                    break;
                }
            }
            dinner->accesspath.push_back(cor);
            rooms.push_back(dinner);
        }
    }

    if (allow_ice)
    {
        ai->debug(out, "icy embark, no well");
    }
    else
    {
        df::coord river = scan_river(out);
        if (river.isValid())
        {
            command_result res = setup_blueprint_cistern_fromsource(out, river, f);
            if (res != CR_OK)
                return res;
        }
        else
        {
            // TODO pool, pumps, etc
            ai->debug(out, "no river, no well");
        }
    }

    // farm plots
    const int16_t farm_w = 3;
    const int16_t farm_h = 3;
    size_t dpf = farm_w * farm_h * dwarves_per_farmtile_num / dwarves_per_farmtile_den;
    size_t nrfarms = (220 + dpf - 1) / dpf;

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
    size_t soilcnt = 0;
    for (int16_t z = cz; z < world->map.z_count; z++)
    {
        bool ok = true;
        size_t scnt = 0;
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
    room *cor = new room(df::coord(cx, cy, cz2), df::coord(cx + 1, cy, cz2));
    cor->accesspath.push_back(farm_stairs);
    corridors.push_back(cor);
    auto make_farms = [this, cor, nrfarms, cx, cy, cz2](int16_t dy, std::string st)
    {
        for (int16_t dx = 0; dx < nrfarms / 3; dx++)
        {
            for (int16_t ddy = 0; ddy < 3; ddy++)
            {
                room *r = new room("farmplot", st, df::coord(cx + farm_w * dx, cy + dy * 2 + dy * ddy * farm_h, cz2), df::coord(cx + farm_w * dx + farm_w - 1, cy + dy * (2 + farm_h - 1) + dy * ddy * farm_h, cz2));
                r->misc["users"] = std::set<int32_t>();
                if (dx == 0 && ddy == 0)
                {
                    r->layout.push_back(new furniture{
                                {"item", horrible_t("door")},
                                {"x", horrible_t(1)},
                                {"y", horrible_t(dy > 0 ? -1 : farm_h)},
                            });
                    r->accesspath.push_back(cor);
                }
                else
                {
                    r->accesspath.push_back(rooms.back());
                }
                rooms.push_back(r);
            }
        }
    };
    make_farms(-1, "food");
    make_farms(1, "cloth");

    // seeds stockpile
    room *r = new room("stockpile", "food", df::coord(cx + 2, cy, cz2), df::coord(cx + 4, cy, cz));
    r->misc["stockpile_level"] = 0;
    r->misc["workshop"] = rooms.at(rooms.size() - 2 * nrfarms);
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
    infirmary->layout.push_back(new furniture{
                {"item", horrible_t("door")},
                {"x", horrible_t(3)},
                {"y", horrible_t(5)},
            });
    infirmary->layout.push_back(new furniture{
                {"item", horrible_t("bed")},
                {"x", horrible_t(0)},
                {"y", horrible_t(1)},
            });
    infirmary->layout.push_back(new furniture{
                {"item", horrible_t("table")},
                {"x", horrible_t(1)},
                {"y", horrible_t(1)},
            });
    infirmary->layout.push_back(new furniture{
                {"item", horrible_t("bed")},
                {"x", horrible_t(2)},
                {"y", horrible_t(1)},
            });
    infirmary->layout.push_back(new furniture{
                {"item", horrible_t("traction_bench")},
                {"x", horrible_t(0)},
                {"y", horrible_t(2)},
            });
    infirmary->layout.push_back(new furniture{
                {"item", horrible_t("traction_bench")},
                {"x", horrible_t(2)},
                {"y", horrible_t(2)},
            });
    infirmary->layout.push_back(new furniture{
                {"item", horrible_t("bed")},
                {"x", horrible_t(0)},
                {"y", horrible_t(3)},
            });
    infirmary->layout.push_back(new furniture{
                {"item", horrible_t("table")},
                {"x", horrible_t(1)},
                {"y", horrible_t(3)},
            });
    infirmary->layout.push_back(new furniture{
                {"item", horrible_t("bed")},
                {"x", horrible_t(2)},
                {"y", horrible_t(3)},
            });
    infirmary->layout.push_back(new furniture{
                {"item", horrible_t("chest")},
                {"x", horrible_t(4)},
                {"y", horrible_t(1)},
            });
    infirmary->layout.push_back(new furniture{
                {"item", horrible_t("chest")},
                {"x", horrible_t(4)},
                {"y", horrible_t(2)},
            });
    infirmary->layout.push_back(new furniture{
                {"item", horrible_t("chest")},
                {"x", horrible_t(4)},
                {"y", horrible_t(3)},
            });
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
                        cemetary->layout.push_back(new furniture{
                                    {"item", horrible_t("coffin")},
                                    {"x", horrible_t(dx + 1 - rx)},
                                    {"y", horrible_t(dy + 1)},
                                    {"ignore", horrible_t()},
                                    {"users", horrible_t(std::set<int32_t>())},
                                });
                    }
                }
                if (rx == 0 && ry == 0 && rrx == 0)
                {
                    cemetary->layout.push_back(new furniture{
                                {"item", horrible_t("door")},
                                {"x", horrible_t(4)},
                                {"y", horrible_t(4)},
                            });
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
        corridors.push_back(old_cor);
        old_cor = cor;
        oldcx = cor->max.x + 1;

        for (int16_t ry = -1; ry <= 1; ry += 2)
        {
            if (ry == -1 && rx < 3) // infirmary/cemetary
                continue;

            room *barracks = new room("barracks", "", df::coord(f.x + 2 + 10 * rx, f.y + 3 * ry, f.z), df::coord(f.x + 2 + 10 * rx + 6, f.y + 10 * ry, f.z));
            barracks->layout.push_back(new furniture{
                        {"item", horrible_t("door")},
                        {"x", horrible_t(3)},
                        {"y", horrible_t(ry > 0 ? -1 : 8)},
                    });
            for (int16_t dy_ = 0; dy_ < 8; dy_++)
            {
                int16_t dy = ry < 0 ? 7 - dy_ : dy_;
                barracks->layout.push_back(new furniture{
                            {"item", horrible_t("armorstand")},
                            {"x", horrible_t(5)},
                            {"y", horrible_t(dy)},
                            {"ignore", horrible_t()},
                            {"users", horrible_t(std::set<int32_t>())},
                        });
                barracks->layout.push_back(new furniture{
                            {"item", horrible_t("bed")},
                            {"x", horrible_t(6)},
                            {"y", horrible_t(dy)},
                            {"ignore", horrible_t()},
                            {"users", horrible_t(std::set<int32_t>())},
                        });
                barracks->layout.push_back(new furniture{
                            {"item", horrible_t("cabinet")},
                            {"x", horrible_t(0)},
                            {"y", horrible_t(dy)},
                            {"ignore", horrible_t()},
                            {"users", horrible_t(std::set<int32_t>())},
                        });
                barracks->layout.push_back(new furniture{
                            {"item", horrible_t("chest")},
                            {"x", horrible_t(1)},
                            {"y", horrible_t(dy)},
                            {"ignore", horrible_t()},
                            {"users", horrible_t(std::set<int32_t>())},
                        });
            }
            barracks->layout.push_back(new furniture{
                        {"item", horrible_t("weaponrack")},
                        {"x", horrible_t(4)},
                        {"y", horrible_t(ry > 0 ? 7 : 0)},
                        {"makeroom", horrible_t()},
                        {"users", horrible_t(std::set<int32_t>())},
                    });
            barracks->layout.push_back(new furniture{
                        {"item", horrible_t("weaponrack")},
                        {"x", horrible_t(2)},
                        {"y", horrible_t(ry > 0 ? 7 : 0)},
                        {"ignore", horrible_t()},
                        {"users", horrible_t(std::set<int32_t>())},
                    });
            barracks->layout.push_back(new furniture{
                        {"item", horrible_t("archerytarget")},
                        {"x", horrible_t(3)},
                        {"y", horrible_t(ry > 0 ? 7 : 0)},
                        {"ignore", horrible_t()},
                        {"users", horrible_t(std::set<int32_t>())},
                    });
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

command_result Plan::setup_blueprint_cistern_fromsource(color_ostream & out, df::coord src, df::coord f)
{
    // TODO dynamic layout, at least move the well/cistern on the side of the river
    // TODO scan for solid ground for all this

    // well
    room *cor = new room(f + df::coord(-18, -1, 0), f + df::coord(-26, 1, 0));
    cor->accesspath.push_back(corridors.back());
    corridors.push_back(cor);

    df::coord c = f - df::coord(32, 0, 0);
    room *well = new room("well", "well", c - df::coord(4, 4, 0), c + df::coord(4, 4, 0));
    well->layout.push_back(new furniture{
                {"item", horrible_t("well")},
                {"x", horrible_t(4)},
                {"y", horrible_t(4)},
                {"makeroom", horrible_t()},
                {"dig", horrible_t("Channel")},
            });
    well->layout.push_back(new furniture{
                {"item", horrible_t("door")},
                {"x", horrible_t(9)},
                {"y", horrible_t(3)},
            });
    well->layout.push_back(new furniture{
                {"item", horrible_t("door")},
                {"x", horrible_t(9)},
                {"y", horrible_t(5)},
            });
    well->layout.push_back(new furniture{
                {"item", horrible_t("trap")},
                {"subtype", horrible_t("lever")},
                {"x", horrible_t(1)},
                {"y", horrible_t(0)},
                {"way", horrible_t("out")},
            });
    well->layout.push_back(new furniture{
                {"item", horrible_t("trap")},
                {"subtype", horrible_t("lever")},
                {"x", horrible_t(0)},
                {"y", horrible_t(0)},
                {"way", horrible_t("in")},
            });
    well->accesspath.push_back(cor);
    rooms.push_back(well);

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
    reserve->layout.push_back(new furniture{
                {"item", horrible_t("floodgate")},
                {"x", horrible_t(-1)},
                {"y", horrible_t(3)},
                {"way", horrible_t("in")},
            });
    reserve->layout.push_back(new furniture{
                {"item", horrible_t("floodgate")},
                {"x", horrible_t(5)},
                {"y", horrible_t(3)},
                {"way", horrible_t("out")},
                {"ignore", horrible_t()},
            });
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
        cor = new room(p1, p2);
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

    // find safe tile near the river
    df::coord output = spiral_search(src, [this](df::coord t) -> bool { return map_tile_in_rock(t); });

    // find tile to channel to start water flow
    df::coord channel = spiral_search(output, 1, 1, [this](df::coord t) -> bool
            {
                return spiral_search(t, 1, 1, [this](df::coord tt) -> bool
                        {
                            return Maps::getTileDesignation(tt)->bits.feature_local;
                        }).isValid();
            });
    if (!channel.isValid())
        channel = spiral_search(output, 1, 1, [this](df::coord t) -> bool
                {
                    return spiral_search(t, 1, 1, [this](df::coord tt) -> bool
                            {
                                return Maps::getTileDesignation(tt)->bits.flow_size != 0 ||
                                        ENUM_ATTR(tiletype, material, *Maps::getTileType(tt)) == tiletype_material::FROZEN_LIQUID;
                            }).isValid();
                });
    if (channel.isValid())
    {
        ai->debug(out, stl_sprintf("cistern: channel_enable (%d, %d, %d)", channel.x, channel.y, channel.z));
    }

    // TODO check that 'channel' is easily channelable (eg river in a hole)

    int16_t y_x = 0;
    if (dst.x - 1 > output.x)
    {
        cor = new room(df::coord(dst.x - 1, dst.y, dst.z), df::coord(output.x + 1, dst.y, dst.z));
        corridors.push_back(cor);
        r->accesspath.push_back(cor);
        r = cor;
        y_x = 1;
    }
    else if (output.x - 1 > dst.x)
    {
        cor = new room(df::coord(dst.x + 1, dst.y, dst.z), df::coord(output.x - 1, dst.y, dst.z));
        corridors.push_back(cor);
        r->accesspath.push_back(cor);
        r = cor;
        y_x = -1;
    }

    if (dst.y - 1 > output.y)
    {
        cor = new room(df::coord(output.x + y_x, dst.y + 1, dst.z), df::coord(output.x + y_x, output.y, dst.z));
        corridors.push_back(cor);
        r->accesspath.push_back(cor);
    }
    else if (output.y - 1 > dst.y)
    {
        cor = new room(df::coord(output.x + y_x, dst.y - 1, dst.z), df::coord(output.x + y_x, output.y, dst.z));
        corridors.push_back(cor);
        r->accesspath.push_back(cor);
    }

    up = find_corridor_tosurface(out, df::coord(output, dst.z));
    r->accesspath.push_back(up.at(0));

    if (channel.isValid())
    {
        reserve->misc["channel_enable"] = channel;
    }
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
                        for (auto be : Maps::getTileBlock(t)->block_events)
                        {
                            df::block_square_event_grassst *grass = virtual_cast<df::block_square_event_grassst>(be);
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
                    r->misc["users"] = std::set<int32_t>();
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
                        const static std::set<df::tiletype_material> allowed_materials{tiletype_material::GRASS_DARK, tiletype_material::GRASS_LIGHT, tiletype_material::SOIL};
                        if (!allowed_materials.count(ENUM_ATTR(tiletype, material, tt)))
                        {
                            return false;
                        }
                    }
                }
                room *r = new room("farmplot", want % 2 == 0 ? "food" : "cloth", sf - df::coord(1, 1, 0), sf + df::coord(1, 1, 0));
                r->misc["users"] = std::set<int32_t>();
                r->misc["outdoor"] = horrible_t();
                want--;
                return want == 0;
            });
    return CR_OK;
}

command_result Plan::setup_blueprint_bedrooms(color_ostream & out, df::coord f, std::vector<room *> entr)
{
    room *corridor_center0 = new room(f + df::coord(-1, -1, 0), f + df::coord(-1, 1, 0));
    corridor_center0->layout.push_back(new furniture{
                {"item", horrible_t("door")},
                {"x", horrible_t(-1)},
                {"y", horrible_t(0)},
            });
    corridor_center0->layout.push_back(new furniture{
                {"item", horrible_t("door")},
                {"x", horrible_t(-1)},
                {"y", horrible_t(2)},
            });
    corridor_center0->accesspath = entr;
    corridors.push_back(corridor_center0);

    room *corridor_center2 = new room(f + df::coord(1, -1, 0), f + df::coord(1, 1, 0));
    corridor_center2->layout.push_back(new furniture{
                {"item", horrible_t("door")},
                {"x", horrible_t(1)},
                {"y", horrible_t(0)},
            });
    corridor_center2->layout.push_back(new furniture{
                {"item", horrible_t("door")},
                {"x", horrible_t(1)},
                {"y", horrible_t(2)},
            });
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
                        r->layout.push_back(new furniture{
                                    {"item", horrible_t("bed")},
                                    {"x", horrible_t(r->min.x < cx ? 0 : 1)},
                                    {"y", horrible_t(diry < 0 ? 1 : 0)},
                                    {"makeroom", horrible_t()},
                                });
                        r->layout.push_back(new furniture{
                                    {"item", horrible_t("cabinet")},
                                    {"x", horrible_t(r->min.x < cx ? 0 : 1)},
                                    {"y", horrible_t(diry < 0 ? 0 : 1)},
                                    {"ignore", horrible_t()},
                                });
                        r->layout.push_back(new furniture{
                                    {"item", horrible_t("chest")},
                                    {"x", horrible_t(r->min.x < cx ? 1 : 0)},
                                    {"y", horrible_t(diry < 0 ? 0 : 1)},
                                    {"ignore", horrible_t()},
                                });
                        r->layout.push_back(new furniture{
                                    {"item", horrible_t("door")},
                                    {"x", horrible_t(r->min.x < cx ? 2 : -1)},
                                    {"y", horrible_t(diry < 0 ? 1 : 0)},
                                });
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
            r->misc["noblesuite"] = noblesuite;
            r->accesspath.push_back(cor_x);
            r->layout.push_back(new furniture{
                        {"item", horrible_t("chair")},
                        {"x", horrible_t(1)},
                        {"y", horrible_t(1)},
                        {"makeroom", horrible_t()},
                    });
            r->layout.push_back(new furniture{
                        {"item", horrible_t("chair")},
                        {"x", horrible_t(1 - dirx)},
                        {"y", horrible_t(1)},
                    });
            r->layout.push_back(new furniture{
                        {"item", horrible_t("chest")},
                        {"x", horrible_t(1 + dirx)},
                        {"y", horrible_t(0)},
                        {"ignore", horrible_t()},
                    });
            r->layout.push_back(new furniture{
                        {"item", horrible_t("cabinet")},
                        {"x", horrible_t(1 + dirx)},
                        {"y", horrible_t(2)},
                        {"ignore", horrible_t()},
                    });
            r->layout.push_back(new furniture{
                        {"item", horrible_t("door")},
                        {"x", horrible_t(1)},
                        {"y", horrible_t(1 - 2 * diry)},
                    });
            rooms.push_back(r);

            r = new room("nobleroom", "bedroom", df::coord(cx - 1, f.y + diry * 7, f.z), df::coord(cx + 1, f.y + diry * 9, f.z));
            r->misc["noblesuite"] = noblesuite;
            r->layout.push_back(new furniture{
                        {"item", horrible_t("bed")},
                        {"x", horrible_t(1)},
                        {"y", horrible_t(1)},
                        {"makeroom", horrible_t()},
                    });
            r->layout.push_back(new furniture{
                        {"item", horrible_t("armorstand")},
                        {"x", horrible_t(1 - dirx)},
                        {"y", horrible_t(0)},
                        {"ignore", horrible_t()},
                    });
            r->layout.push_back(new furniture{
                        {"item", horrible_t("weaponrack")},
                        {"x", horrible_t(1 - dirx)},
                        {"y", horrible_t(2)},
                        {"ignore", horrible_t()},
                    });
            r->layout.push_back(new furniture{
                        {"item", horrible_t("door")},
                        {"x", horrible_t(1)},
                        {"y", horrible_t(1 - 2 * diry)},
                        {"internal", horrible_t()},
                    });
            r->accesspath.push_back(rooms.back());
            rooms.push_back(r);

            r = new room("nobleroom", "diningroom", df::coord(cx - 1, f.y + diry * 11, f.z), df::coord(cx + 1, f.y + diry * 13, f.z));
            r->misc["noblesuite"] = noblesuite;
            r->layout.push_back(new furniture{
                        {"item", horrible_t("table")},
                        {"x", horrible_t(1)},
                        {"y", horrible_t(1)},
                        {"makeroom", horrible_t()},
                    });
            r->layout.push_back(new furniture{
                        {"item", horrible_t("chair")},
                        {"x", horrible_t(1 + dirx)},
                        {"y", horrible_t(1)},
                    });
            r->layout.push_back(new furniture{
                        {"item", horrible_t("cabinet")},
                        {"x", horrible_t(1 - dirx)},
                        {"y", horrible_t(0)},
                        {"ignore", horrible_t()},
                    });
            r->layout.push_back(new furniture{
                        {"item", horrible_t("chest")},
                        {"x", horrible_t(1 - dirx)},
                        {"y", horrible_t(2)},
                        {"ignore", horrible_t()},
                    });
            r->layout.push_back(new furniture{
                        {"item", horrible_t("door")},
                        {"x", horrible_t(1)},
                        {"y", horrible_t(1 - 2 * diry)},
                        {"internal", horrible_t()},
                    });
            r->accesspath.push_back(rooms.back());
            rooms.push_back(r);

            r = new room("nobleroom", "tomb", df::coord(cx - 1, f.y + diry * 15, f.z), df::coord(cx + 1, f.y + diry * 17, f.z));
            r->misc["noblesuite"] = noblesuite;
            r->layout.push_back(new furniture{
                        {"item", horrible_t("coffin")},
                        {"x", horrible_t(1)},
                        {"y", horrible_t(1)},
                        {"makeroom", horrible_t()},
                    });
            r->layout.push_back(new furniture{
                        {"item", horrible_t("door")},
                        {"x", horrible_t(1)},
                        {"y", horrible_t(1 - 2 * diry)},
                        {"internal", horrible_t()},
                    });
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
                int16_t & y = setup_outdoor_gathering_zones_counters[0];
                int16_t & i = setup_outdoor_gathering_zones_counters[0];
                std::map<int16_t, std::set<df::coord2d>> & ground = setup_outdoor_gathering_zones_ground;
                if (i == 31 || x + i == world->map.x_count)
                {
                    for (auto g : ground)
                    {
                        df::building_civzonest *bld = virtual_cast<df::building_civzonest>(Buildings::allocInstance(df::coord(x, y, g.first), building_type::Civzone, civzone_type::ActivityZone));
                        int16_t w = 31;
                        int16_t h = 31;
                        if (x + 31 > world->map.x_count)
                            w = world->map.x_count % 31;
                        if (y + 31 > world->map.y_count)
                            h = world->map.y_count % 31;
                        Buildings::setSize(bld, df::coord(w, h, 1));
                        for (int16_t dx = 0; dx < w; dx++)
                        {
                            for (int16_t dy = 0; dy < h; dy++)
                            {
                                bld->room.extents[dx + w * dy] = g.second.count(df::coord2d(dx, dy)) ? 1 : 0;
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
            ai->debug(out, stl_sprintf("find_corridor_tosurface: loop: %d, %d, %d", origin.x, origin.y, origin.z));
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
    for (task *t : tasks)
    {
        task_count[t->at(0).str]++;
    }
    std::ostringstream s;
    for (auto tc : task_count)
    {
        if (!s.str().empty())
        {
            s << ", ";
        }
        s << tc.first << ": " << tc.second;
    }
    if (task *t = is_digging())
    {
        s << ", digging: " << describe_room(t->at(1).r);
    }
    return s.str();
}

void Plan::categorize_all()
{
    room_category.clear();
    for (room *r : rooms)
    {
        room_category[r->type].push_back(r);
    }

    if (room_category.count("stockpile"))
    {
        auto & stockpiles = room_category.at("stockpile");
        std::sort(stockpiles.begin(), stockpiles.end(), [this](room *a, room *b) -> bool
                {
                    int32_t alevel = a->misc.at("stockpile_level").id;
                    int32_t blevel = b->misc.at("stockpile_level").id;
                    if (alevel < blevel)
                        return true;
                    if (alevel > blevel)
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

    if (r->misc.count("squad_id"))
    {
        if (df::squad *squad = df::squad::find(r->misc.at("squad_id").id))
        {
            s << " (used by " << Translation::capitalize(Translation::TranslateName(&squad->name, true)) << ")";
        }
    }

    if (r->misc.count("stockpile_level"))
    {
        s << " (" << r->misc.at("stockpile_level").id << ")";
    }

    if (r->misc.count("workshop_level"))
    {
        s << " (" << r->misc.at("workshop_level").id << ")";
    }

    if (r->misc.count("workshop"))
    {
        s << " (" << describe_room(r->misc.at("workshop").r) << ")";
    }

    if (r->misc.count("users"))
    {
        s << " (" << r->misc.at("users").ids.size() << " users)";
    }

    s << " (" << r->status << ")";

    return s.str();
}

room *Plan::find_room(std::string type)
{
    if (room_category.empty())
    {
        for (room *r : rooms)
        {
            if (r->type == type)
            {
                return r;
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
        for (room *r : rooms)
        {
            if (r->type == type && b(r))
            {
                return r;
            }
        }
        return nullptr;
    }

    if (room_category.count(type))
    {
        for (room *r : room_category.at(type))
        {
            if (b(r))
            {
                return r;
            }
        }
    }

    return nullptr;
}

bool Plan::map_tile_intersects_room(df::coord t)
{
    for (room *r : rooms)
    {
        if (r->safe_include(t))
            return true;
    }
    for (room *r : corridors)
    {
        if (r->safe_include(t))
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

    for (df::plant *tree : world->plants.tree_dry)
    {
        if (find(tree))
        {
            return tree->pos;
        }
    }

    for (df::plant *tree : world->plants.tree_wet)
    {
        if (find(tree))
        {
            return tree->pos;
        }
    }

    df::coord invalid;
    invalid.clear();
    return invalid;
}

// XXX
bool Plan::corridor_include_hack(const room *r, df::coord t)
{
    for (room *c : corridors)
    {
        if (!c->include(t))
        {
            continue;
        }

        if (c->min.z != c->max.z)
        {
            return true;
        }
        for (room *a : c->accesspath)
        {
            if (a == r)
            {
                return true;
            }
        }
    }
    return false;
}

// vim: et:sw=4:ts=4

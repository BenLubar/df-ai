#include "ai.h"
#include "plan.h"
#include "stocks.h"

#include "modules/Buildings.h"
#include "modules/Items.h"
#include "modules/Job.h"
#include "modules/Maps.h"
#include "modules/Materials.h"
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

const size_t manager_taskmax = 4; // when stacking manager jobs, do not stack more than this
const size_t manager_maxbacklog = 10; // add new masonly if more that this much mason manager orders
const size_t dwarves_per_table = 3; // number of dwarves per dininghall table/chair
const size_t dwarves_per_farmtile_num = 3; // number of dwarves per farmplot tile
const size_t dwarves_per_farmtile_den = 2;
const size_t wantdig_max = 2; // dig at most this much wantdig rooms at a time
const size_t spare_bedroom = 3; // dig this much free bedroom in advance when idle

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

    bool want_reupdate = false;
    events.onupdate_register_once("df-ai plan bg", [this, &want_reupdate](color_ostream & out) -> bool
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
        if (setup_blueprint_caverns(out))
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

void Plan::idleidle(color_ostream & out)
{
    ai->debug(out, "smooth fort");
    std::vector<room *> tab;
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
            tab.push_back(r);
    }
    for (auto r : corridors)
    {
        if (r->status != "plan" && r->status != "dig")
            tab.push_back(r);
    }

    auto it = tab.begin();

    events.onupdate_register_once("df-ai plan idleidle", 4, [this, tab, &it](color_ostream & out)
            {
                if (it == tab.end())
                {
                    return true;
                }
                smooth_room(out, *it);
                it++;
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
                }))
    {
        wantdig(out, r);
        for (furniture *f : r->layout)
        {
            if (f->at("item") == "table" &&
                    f->at("users").ids.size() < dwarves_per_table)
            {
                f->erase("ignore");
                (*f)["users"].ids.insert(id);
                break;
            }
        }
        for (furniture *f : r->layout)
        {
            if (f->at("item") == "chair" &&
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

    if (r->status == "finsihed")
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
        if (f->at("item") == "floodgate")
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

    df::building *bld = Buildings::allocInstance(t, building_type::Windmill);
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

    df::workshop_type subtype = df::workshop_type(-1);
    find_enum_item(&subtype, r->subtype);

    if (r->subtype == "Dyers")
    {
        df::item *barrel, *bucket;
        if (find_item(items_other_id::BARREL, barrel) &&
                find_item(items_other_id::BUCKET, bucket))
        {
            df::building *bld = Buildings::allocInstance(r->pos(), building_type::Workshop, subtype);
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
            df::building *bld = Buildings::allocInstance(r->pos(), building_type::Workshop, subtype);
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
            df::building *bld = Buildings::allocInstance(r->pos(), building_type::Workshop, workshop_type::Custom, find_custom_building("SOAP_MAKER"));
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
            df::building *bld = Buildings::allocInstance(r->pos(), building_type::Workshop, workshop_type::Custom, find_custom_building("SCREW_PRESS"));
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
            df::building *bld = Buildings::allocInstance(r->pos(), building_type::Workshop, subtype);
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
            find_enum_item(&furnace_subtype, r->subtype);
            df::building *bld = Buildings::allocInstance(r->pos(), building_type::Furnace, furnace_subtype);
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
            df::building *bld = Buildings::allocInstance(r->pos(), building_type::Workshop, subtype);
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
            df::building *bld = Buildings::allocInstance(r->pos(), building_type::TradeDepot);
            Buildings::constructWithItems(bld, boulds);
            r->misc["bld_id"] = bld->id;
            tasks.push_back(new task{horrible_t("checkconstruct"), horrible_t(r)});
            return true;
        }
    }
    else
    {
        df::item *bould;
        if (find_item(items_other_id::BOULDER, bould, false, true))
        {
            df::building *bld = Buildings::allocInstance(r->pos(), building_type::Workshop, subtype);
            Buildings::constructWithItems(bld, {bould});
            r->misc["bld_id"] = bld->id;
            tasks.push_back(new task{horrible_t("checkconstruct"), horrible_t(r)});
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

    df::coord size = r->size();

    df::building_stockpilest *bld = virtual_cast<df::building_stockpilest>(Buildings::allocInstance(r->pos(), building_type::Stockpile));
    bld->room.extents = new uint8_t[size.x * size.y]();
    bld->room.x = r->min.x;
    bld->room.y = r->min.y;
    bld->room.width = size.x;
    bld->room.height = size.y;
    for (int16_t x = 0; x < size.x; x++)
    {
        for (int16_t y = 0; y < size.y; y++)
        {
            df::tiletype tt = *Maps::getTileType(r->min + df::coord(x, y, 0));
            if (ENUM_ATTR(tiletype_shape, basic_shape, ENUM_ATTR(tiletype, shape, tt)) == tiletype_shape_basic::Floor)
            {
                bld->room.extents[x + size.x * y] = 1;
            }
            else
            {
                bld->room.extents[x + size.x * y] = 0;
            }
        }
    }
    bld->is_room = 1;
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
                if (o->subtype == r->subtype && -1 <= diff && diff <= 1)
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

    df::building_civzonest *bld = virtual_cast<df::building_civzonest>(Buildings::allocInstance(r->pos(), building_type::Civzone, civzone_type::ActivityZone));
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
        // pit_flags |= 2
    }
    else if (r->type == "pitcage")
    {
        bld->zone_flags.bits.pit_pond = 1;
    }

    df::coord size = r->size();
    bld->room.extents = new uint8_t[size.x * size.y]();
    bld->room.x = r->min.x;
    bld->room.y = r->min.y;
    bld->room.width = size.x;
    bld->room.height = size.y;
    for (int16_t x = 0; x < size.x; x++)
    {
        for (int16_t y = 0; y < size.y; y++)
        {
            bld->room.extents[x + size.x * y] = 1;
        }
    }
    bld->is_room = 1;
    Buildings::constructAbstract(bld);
    r->misc["bld_id"] = bld->id;

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
        if (r && r->misc.count("workshop") &
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

    df::building *bld = Buildings::allocInstance(r->pos(), building_type::FarmPlot);
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
    smooth_xyz(r->min - 1, r->max + 1);
}

// smooth a room and its accesspath corridors (recursive)
void Plan::smooth_room_access(color_ostream & out, room *r)
{
    smooth_room(out, r);
    for (room *a : r->accesspath)
    {
        smooth_room_access(out, r);
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
    bld->is_room = 1;

    set_owner(out, r, r->owner);
    furnish_room(out, r);

    if (r->type == "dininghall")
    {
        virtual_cast<df::building_tablest>(bld)->table_flags.bits.meeting_hall = 1;
    }
    else if (r->type == "barracks")
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

/*
# returns one tile of an outdoor river (if one exists)
def scan_river
    ifeat = df.world.features.map_features.find { |f| f.kind_of?(DFHack::FeatureInitOutdoorRiverst) }
    return if not ifeat
    feat = ifeat.feature

    feat.embark_pos.x.length.times { |i|
        x = 48*(feat.embark_pos.x[i] - df.world.map.region_x)
        y = 48*(feat.embark_pos.y[i] - df.world.map.region_y)
        next if x < 0 or x >= df.world.map.x_count or y < 0 or y >= df.world.map.y_count
        z1, z2 = feat.min_map_z[i], feat.max_map_z[i]
        zhalf = (z1+z2)/2 - 1   # the river is most probably here, try it first
        [zhalf, *(z1..z2)].each { |z|
            48.times { |dx|
                48.times { |dy|
                    t = df.map_tile_at(x+dx, y+dy, z)
                    return t if t and t.designation.feature_local
                }
            }
        }
    }

    nil
end

attr_accessor :fort_entrance, :rooms, :corridors
def setup_blueprint
    # TODO use existing fort facilities (so we can relay the user or continue from a save)
    ai.debug 'setting up fort blueprint...'
    # TODO place fort body first, have main stair stop before surface, and place trade depot on path to surface
    scan_fort_entrance
    ai.debug 'blueprint found entrance'
    # TODO if no room for fort body, make surface fort
    scan_fort_body
    ai.debug 'blueprint found body'
    setup_blueprint_rooms
    ai.debug 'blueprint found rooms'
    # ensure traps are on the surface
    @fort_entrance.layout.each { |i|
        i[:z] = surface_tile_at(@fort_entrance.x1+i[:x], @fort_entrance.y1+i[:y], true).z-@fort_entrance.z1
    }
    @fort_entrance.layout.delete_if { |i|
        t = df.map_tile_at(@fort_entrance.x1+i[:x], @fort_entrance.y1+i[:y], @fort_entrance.z1+i[:z]-1)
        tm = t.tilemat
        t.shape_basic != :Wall or (tm != :STONE and tm != :MINERAL and tm != :SOIL and tm != :ROOT and (not @allow_ice or tm != :FROZEN_LIQUID))
    }
    list_map_veins
    setup_outdoor_gathering_zones
    setup_blueprint_caverns
    make_map_walkable
    ai.debug 'LET THE GAME BEGIN!'
end

def make_map_walkable
    df.onupdate_register_once('df-ai plan make_map_walkable') {
        # if we don't have a river, we're fine
        next true unless river = scan_river
        next true if surface = surface_tile_at(river) and surface.tilemat == :BROOK

        river = spiral_search(river) { |t|
            # TODO rooms outline
            (t.y < @fort_entrance.y+MinY or t.y > @fort_entrance.y+MaxY) and
            t.designation.feature_local
        }
        next true unless river

        # find a safe place for the first tile
        t1 = spiral_search(river) { |t| map_tile_in_rock(t) and st = surface_tile_at(t) and map_tile_in_rock(st.offset(0, 0, -1)) }
        next true unless t1
        t1 = surface_tile_at(t1)

        # if the game hasn't done any pathfinding yet, wait until the next frame and try again
        next false if (t1w = t1.mapblock.walkable[t1.x & 15][t1.y & 15]) == 0

        # find the second tile
        t2 = spiral_search(t1) { |t| tw = t.mapblock.walkable[t.x & 15][t.y & 15] and tw != 0 and tw != t1w }
        next true unless t2
        t2w = t2.mapblock.walkable[t2.x & 15][t2.y & 15]

        # make sure the second tile is in a safe place
        t2 = spiral_search(t2) { |t| t.mapblock.walkable[t.x & 15][t.y & 15] == t2w and map_tile_in_rock(t.offset(0, 0, -1)) }

        # find the bottom of the staircases
        z = df.world.features.map_features.find { |f| f.kind_of?(DFHack::FeatureInitOutdoorRiverst) }.feature.min_map_z.min - 1

        # make the corridors
        cor = []
        cor << Corridor.new(t1.x, t1.x, t1.y, t1.y, t1.z, z)
        cor << Corridor.new(t2.x, t2.x, t2.y, t2.y, t2.z, z)
        cor << Corridor.new(t1.x - (t1.x <=> t2.x), t2.x - (t2.x <=> t1.x), t1.y, t1.y, z, z) if (t1.x - t2.x).abs > 1
        cor << Corridor.new(t2.x, t2.x, t1.y - (t1.y <=> t2.y), t2.y - (t2.y <=> t1.y), z, z) if (t1.y - t2.y).abs > 1
        cor << Corridor.new(t2.x, t2.x, t1.y, t1.y, z, z) if (t1.x - t2.x).abs > 1 and (t1.y - t2.y).abs > 1

        cor.each do |c|
            @corridors << c
            wantdig c
        end

        true
    }
end

attr_accessor :map_veins

# scan the map, list all map veins in @map_veins (mat_index => [block coords], sorted by z)
def list_map_veins
    @map_veins = {}
    i = 0
    df.onupdate_register_once('df-ai plan list_map_veins') {
        if i < df.world.map.z_count
            df.each_map_block_z(i) { |block|
                block.block_events.grep(DFHack::BlockSquareEventMineralst).each { |vein|
                    @map_veins[vein.inorganic_mat] ||= []
                    @map_veins[vein.inorganic_mat] << [block.map_pos.x, block.map_pos.y, block.map_pos.z]
                }
            }
            i += 1
            false
        else
            @map_veins.each { |mat, list|
                list.replace list.sort_by { |x, y, z| z + rand(6) }
            }
            true
        end
    }
end

# mark a vein of a mat for digging, return expected boulder count
def dig_vein(mat, want_boulders = 1)
    # mat => [x, y, z, dig_mode] marked for to dig
    @map_vein_queue ||= {}
    @dug_veins ||= {}

    count = 0
    # check previously queued veins
    if q = @map_vein_queue[mat]
        q.delete_if { |x, y, z, dd|
            t = df.map_tile_at(x, y, z)
            if t.shape_basic == :Open
                try_furnish_construction(nil, {:construction => dd == :Default ? :Floor : dd}, t)
            elsif t.shape_basic != :Wall
                true
            else
                t.dig(dd) if t.designation.dig == :No     # warm/wet tile
                count += 1 if t.tilemat == :MINERAL and t.mat_index_vein == mat
                false
            end
        }
        @map_vein_queue.delete mat if q.empty?
    end

    # queue new vein
    # delete it from @map_veins
    # discard tiles that would dig into a @plan room/corridor, or into a cavern (hidden + !wall)
    # try to find a vein close to one we already dug
    16.times {
        break if count/4 >= want_boulders
        @map_veins.delete mat if @map_veins[mat] == []
        break if not @map_veins[mat]
        v = @map_veins[mat].find { |x, y, z|
            (-1..1).find { |vx| (-1..1).find { |vy| @dug_veins[[x+16*vx, y+16*vy, z]] } }
        } || @map_veins[mat].last
        @map_veins[mat].delete v
        cnt = do_dig_vein(mat, *v)
        @dug_veins[v] = true if cnt > 0
        count += cnt
    }

    count/4
end

def do_dig_vein(mat, bx, by, bz)
    ai.debug "dig_vein #{df.world.raws.inorganics[mat].id}"
    count = 0
    fort_minz ||= @corridors.map { |c| c.z1 if c.subtype != :veinshaft }.compact.min
    if bz >= fort_minz
        # TODO rooms outline
        if by > @fort_entrance.y+MinY and by < @fort_entrance.y+MaxY
            return count
        end
    end

    q = @map_vein_queue[mat] ||= []

    # dig whole block
    # TODO have the dwarves search for the vein
    # TODO mine in (visible?) chunks
    dxs = []
    dys = []
    (0..15).each { |dx| (0..15).each { |dy|
        next if bx+dx == 0 or by+dy == 0 or bx+dx >= df.world.map.x_count-1 or by+dy >= df.world.map.y_count-1
        t = df.map_tile_at(bx+dx, by+dy, bz)
        if t.tilemat == :MINERAL and t.mat_index_vein == mat
            dxs |= [dx]
            dys |= [dy]
        end
    } }

    return count if dxs.empty?

    need_shaft = true
    todo = []
    ai.debug "do_dig_vein #{dxs.min}..#{dxs.max} #{dys.min}..#{dys.max}"
    (dxs.min..dxs.max).each { |dx| (dys.min..dys.max).each { |dy|
        t = df.map_tile_at(bx+dx, by+dy, bz)
        if t.designation.dig == :No
            ok = true
            ns = need_shaft
            (-1..1).each { |ddx| (-1..1).each { |ddy|
                tt = t.offset(ddx, ddy)
                if tt.shape_basic != :Wall
                    tt.designation.hidden ? ok = false : ns = false
                elsif tt.designation.dig != :No
                    ns = false
                end
            } }
            if ok
                todo << [t.x, t.y, t.z, :Default]
                count += 1 if t.tilemat == :MINERAL and t.mat_index_vein == mat
                need_shaft = ns
            end
        end
    } }
    todo.each { |x, y, z, dd|
        q << [x, y, z, dd]
        df.map_tile_at(x, y, z).dig(dd)
    }

    if need_shaft
        # TODO minecarts?

        # avoid giant vertical runs: slalom x+-1 every z%16

        if by > @fort_entrance.y
            vert = df.map_tile_at(@fort_entrance.x, @fort_entrance.y+30, bz)   # XXX 30...
        else
            vert = df.map_tile_at(@fort_entrance.x, @fort_entrance.y-30, bz)
        end
        vert = vert.offset(1, 0) if (vert.z % 32) > 16  # XXX check this

        t0 = df.map_tile_at(bx+(dxs.min+dxs.max)/2, by+(dys.min+dys.max)/2, bz)
        while t0.y != vert.y
            t0.dig(:Default)
            q << [t0.x, t0.y, t0.z, :Default]
            t0 = t0.offset(0, (t0.y > vert.y ? -1 : 1))
        end
        while t0.x != vert.x
            t0.dig(:Default)
            q << [t0.x, t0.y, t0.z, :Default]
            t0 = t0.offset((t0.x > vert.x ? -1 : 1), 0)
        end
        while t0.designation.hidden
            if t0.z % 16 == 0
                t0.dig(:DownStair)
                q << [t0.x, t0.y, t0.z, :DownStair]
                t0 = t0.offset(((t0.z % 32) >= 16 ? 1 : -1), 0)
                t0.dig(:UpStair)
                q << [t0.x, t0.y, t0.z, :UpStair]
            else
                t0.dig(:UpDownStair)
                q << [t0.x, t0.y, t0.z, :UpDownStair]
            end
            break if not t0 = t0.offset(0, 0, 1)
        end
        if t0 and not t0.designation.hidden and not t0.shape_passablelow and t0.designation.dig == :No
            t0.dig(:DownStair)
            q << [t0.x, t0.y, t0.z, :DownStair]
        end
    end

    count
end

# same as tile.spiral_search, but search starting with center of each side first
def spiral_search(tile, max=100, min=0, step=1)
    (min..max).step(step) { |rng|
        if rng != 0
            [[rng, 0], [0, rng], [-rng, 0], [0, -rng]].each { |dx, dy|
                next if not tt = tile.offset(dx, dy)
                return tt if yield(tt)
            }
        end
        if tt = tile.spiral_search(rng, rng, step) { |_tt| yield _tt }
            return tt
        end
    }
    nil
end

MinX, MinY, MinZ = -48, -22, -5
MaxX, MaxY, MaxZ = 35, 22, 1

# search a valid tile for fortress entrance
def scan_fort_entrance
    # map center
    cx = df.world.map.x_count / 2
    cy = df.world.map.y_count / 2
    center = surface_tile_at(cx, cy, true)
    rangez = (0...df.world.map.z_count).to_a.reverse
    cz = rangez.find { |z| t = df.map_tile_at(cx, cy, z) and tsb = t.shape_basic and (tsb == :Floor or tsb == :Ramp) }
    center = df.map_tile_at(cx, cy, cz)

    ent0 = center.spiral_search { |t0|
        # test the whole map for 3x5 clear spots
        next unless t = surface_tile_at(t0)

        # make sure we're not too close to the edge of the map.
        next unless t.offset(MinX, MinY, MinZ) and t.offset(MaxX, MaxY, MaxZ)

        (-1..1).all? { |_x|
            (-2..2).all? { |_y|
                tt = t.offset(_x, _y, -1) and tt.shape == :WALL and tm = tt.tilemat and (tm == :STONE or tm == :MINERAL or tm == :SOIL or tm == :ROOT) and
                ttt = t.offset(_x, _y) and ttt.shape == :FLOOR and ttt.designation.flow_size == 0 and
                 not ttt.designation.hidden and not df.building_find(ttt)
            }
        } and (-3..3).all? { |_x|
            (-4..4).all? { |_y|
                surface_tile_at(t.x + _x, t.y + _y, true)
            }
        }
    }

    if not ent0
        @allow_ice = true

        ent0 = center.spiral_search { |t0|
            # test the whole map for 3x5 clear spots
            next unless t = surface_tile_at(t0)

            # make sure we're not too close to the edge of the map.
            next unless t.offset(MinX, MinY, MinZ) and t.offset(MaxX, MaxY, MaxZ)

            (-1..1).all? { |_x|
                (-2..2).all? { |_y|
                    tt = t.offset(_x, _y, -1) and tt.shape == :WALL and
                    ttt = t.offset(_x, _y) and ttt.shape == :FLOOR and ttt.designation.flow_size == 0 and
                     not ttt.designation.hidden and not df.building_find(ttt)
                }
            } and (-3..3).all? { |_x|
                (-4..4).all? { |_y|
                    surface_tile_at(t.x + _x, t.y + _y, true)
                }
            }
        }
    end

    raise 'Can\'t find a fortress entrance spot. We need a 3x5 flat area with solid ground for at least 2 tiles on each side.' unless ent0
    ent = surface_tile_at(ent0)

    @fort_entrance = Corridor.new(ent.x, ent.x, ent.y-1, ent.y+1, ent.z, ent.z)
    3.times { |i|
        @fort_entrance.layout << {:item => :trap, :subtype => :cage, :x => i-1, :y => -1,  :ignore => true}
        @fort_entrance.layout << {:item => :trap, :subtype => :cage, :x => 1,   :y => i,   :ignore => true}
        @fort_entrance.layout << {:item => :trap, :subtype => :cage, :x => 1-i, :y => 3,   :ignore => true}
        @fort_entrance.layout << {:item => :trap, :subtype => :cage, :x => -1,  :y => 2-i, :ignore => true}
    }
    5.times { |i|
        @fort_entrance.layout << {:item => :trap, :subtype => :cage, :x => i-2, :y => -2,  :ignore => true}
        @fort_entrance.layout << {:item => :trap, :subtype => :cage, :x => 2,   :y => i-1, :ignore => true}
        @fort_entrance.layout << {:item => :trap, :subtype => :cage, :x => 2-i, :y => 4,   :ignore => true}
        @fort_entrance.layout << {:item => :trap, :subtype => :cage, :x => -2,  :y => 3-i, :ignore => true}
    }

end

# search how much we need to dig to find a spot for the full fortress body
# here we cheat and work as if the map was fully reveal()ed
def scan_fort_body
    # use a hardcoded fort layout
    cx, cy, cz = @fort_entrance.x, @fort_entrance.y, @fort_entrance.z

    @fort_entrance.z1 = (0..cz).to_a.reverse.find { |cz1|
        # stop searching if we hit a cavern or an aquifer inside our main staircase
        break unless (0..0).all? { |dx|
            (-1..1).all? { |dy|
                t = df.map_tile_at(cx+dx, cy+dy, cz1+MaxZ) and
                map_tile_nocavern(t) and
                not t.designation.water_table
            }
        }

        (MinZ..MaxZ).all? { |dz|
            # scan perimeter first to quickly eliminate caverns / bad rock layers
            (MinX..MaxX).all? { |dx|
                [MinY, MaxY].all? { |dy|
                    t = df.map_tile_at(cx+dx, cy+dy, cz1+dz) and t.shape == :WALL and
                    not t.designation.water_table and tm = t.tilemat and (tm == :STONE or tm == :MINERAL or (@allow_ice and tm == :FROZEN_LIQUID) or (dz > -1 and (tm == :SOIL or tm == :ROOT)))
                }
            } and
            [MinX, MaxX].all? { |dx|
                (MinY..MaxY).all? { |dy|
                    t = df.map_tile_at(cx+dx, cy+dy, cz1+dz) and t.shape == :WALL and
                    not t.designation.water_table and tm = t.tilemat and (tm == :STONE or tm == :MINERAL or (@allow_ice and tm == :FROZEN_LIQUID) or (dz > -1 and (tm == :SOIL or tm == :ROOT)))
                }
            }
        }  and
        # perimeter ok, full scan
        (MinZ..MaxZ).all? { |dz|
            ((MinX+1)..(MaxX-1)).all? { |dx|
                ((MinY+1)..(MaxY-1)).all? { |dy|
                    t = df.map_tile_at(cx+dx, cy+dy, cz1+dz) and t.shape == :WALL and
                    not t.designation.water_table and tm = t.tilemat and (tm == :STONE or tm == :MINERAL or (@allow_ice and tm == :FROZEN_LIQUID) or (dz > -1 and (tm == :SOIL or tm == :ROOT)))
                }
            }
        }
    }

    raise 'Too many caverns, cant find room for fort. We need more minerals!' unless @fort_entrance.z1
end

# assign rooms in the space found by scan_fort_*
def setup_blueprint_rooms
    # hardcoded layout
    @corridors << @fort_entrance

    fx = @fort_entrance.x
    fy = @fort_entrance.y

    fz = @fort_entrance.z1
    setup_blueprint_workshops(fx, fy, fz, [@fort_entrance])

    fz = @fort_entrance.z1 -= 1
    setup_blueprint_utilities(fx, fy, fz, [@fort_entrance])

    fz = @fort_entrance.z1 -= 1
    setup_blueprint_stockpiles(fx, fy, fz, [@fort_entrance])

    2.times {
        fz = @fort_entrance.z1 -= 1
        setup_blueprint_bedrooms(fx, fy, fz, [@fort_entrance])
    }
end

def setup_blueprint_workshops(fx, fy, fz, entr)
    corridor_center0 = Corridor.new(fx-1, fx-1, fy-1, fy+1, fz, fz)
    corridor_center0.layout << {:item => :door, :x => -1, :y => 0}
    corridor_center0.layout << {:item => :door, :x => -1, :y => 2}
    corridor_center0.accesspath = entr
    @corridors << corridor_center0
    corridor_center2 = Corridor.new(fx+1, fx+1, fy-1, fy+1, fz, fz)
    corridor_center2.layout << {:item => :door, :x => 1, :y => 0}
    corridor_center2.layout << {:item => :door, :x => 1, :y => 2}
    corridor_center2.accesspath = entr
    @corridors << corridor_center2

    # Millstone, Siege, magma workshops/furnaces
    types = [:Still,:Kitchen, :Fishery,:Butchers, :Leatherworks,:Tanners,
        :Loom,:Clothiers, :Dyers,:Bowyers, nil,:Kiln]
    types += [:Masons,:Carpenters, :Mechanics,:Farmers, :Craftsdwarfs,:Jewelers,
        :Smelter,:MetalsmithsForge, :Ashery,:WoodFurnace, :SoapMaker,:GlassFurnace]

    [-1, 1].each { |dirx|
        prev_corx = (dirx < 0 ? corridor_center0 : corridor_center2)
        ocx = fx + dirx*3
        (1..6).each { |dx|
            # segments of the big central horizontal corridor
            cx = fx + dirx*4*dx
            cor_x = Corridor.new(ocx, cx + (dx <= 5 ? 0 : dirx), fy-1, fy+1, fz, fz)
            cor_x.accesspath = [prev_corx]
            @corridors << cor_x
            prev_corx = cor_x
            ocx = cx+dirx

            if dirx == 1 and dx == 3
                # stuff a quern&screwpress near the farmers'
                @rooms << Room.new(:workshop, :Quern, cx-2, cx-2, fy+1, fy+1, fz)
                @rooms.last.accesspath = [cor_x]
                @rooms.last.misc[:workshop_level] = 0

                @rooms << Room.new(:workshop, :Quern, cx-6, cx-6, fy+1, fy+1, fz)
                @rooms.last.accesspath = [cor_x]
                @rooms.last.misc[:workshop_level] = 1

                @rooms << Room.new(:workshop, :Quern, cx+2, cx+2, fy+1, fy+1, fz)
                @rooms.last.accesspath = [cor_x]
                @rooms.last.misc[:workshop_level] = 2

                @rooms << Room.new(:workshop, :ScrewPress, cx-2, cx-2, fy-1, fy-1, fz)
                @rooms.last.accesspath = [cor_x]
                @rooms.last.misc[:workshop_level] = 0
            end

            t = types.shift
            @rooms << Room.new(:workshop, t, cx-1, cx+1, fy-5, fy-3, fz)
            @rooms.last.accesspath = [cor_x]
            @rooms.last.layout << {:item => :door, :x => 1, :y => 3}
            @rooms.last.misc[:workshop_level] = 0
            if dirx == -1 and dx == 1
                @rooms.last.layout << {:item => :nestbox, :x => -1, :y => 4}
            end

            @rooms << Room.new(:workshop, t, cx-1, cx+1, fy-8, fy-6, fz)
            @rooms.last.accesspath = [@rooms[@rooms.length - 2]]
            @rooms.last.misc[:workshop_level] = 1

            @rooms << Room.new(:workshop, t, cx-1, cx+1, fy-11, fy-9, fz)
            @rooms.last.accesspath = [@rooms[@rooms.length - 2]]
            @rooms.last.misc[:workshop_level] = 2

            t = types.shift
            @rooms << Room.new(:workshop, t, cx-1, cx+1, fy+3, fy+5, fz)
            @rooms.last.accesspath = [cor_x]
            @rooms.last.layout << {:item => :door, :x => 1, :y => -1}
            @rooms.last.misc[:workshop_level] = 0
            if dirx == -1 and dx == 1
                @rooms.last.layout << {:item => :nestbox, :x => -1, :y => -2}
            end

            @rooms << Room.new(:workshop, t, cx-1, cx+1, fy+6, fy+8, fz)
            @rooms.last.accesspath = [@rooms[@rooms.length - 2]]
            @rooms.last.misc[:workshop_level] = 1

            @rooms << Room.new(:workshop, t, cx-1, cx+1, fy+9, fy+11, fz)
            @rooms.last.accesspath = [@rooms[@rooms.length - 2]]
            @rooms.last.misc[:workshop_level] = 2
        }
    }

    depot_center = spiral_search(df.map_tile_at(@fort_entrance.x - 4, @fort_entrance.y, @fort_entrance.z2 - 1)) { |t|
        (-2..2).all? { |dx| (-2..2).all? { |dy|
            tt = t.offset(dx, dy, 0) and
            map_tile_in_rock(tt) and
            not map_tile_intersects_room(tt)
        } } and (-1..1).all? { |dy|
            tt = t.offset(-3, dy, 0) and
            map_tile_in_rock(tt) and
            ttt = tt.offset(0, 0, 1) and
            ttt.shape_basic == :Floor and
            not map_tile_intersects_room(tt) and
            not map_tile_intersects_room(ttt)
        }
    }

    if depot_center
        r = Room.new(:workshop, :TradeDepot, depot_center.x-2, depot_center.x+2, depot_center.y-2, depot_center.y+2, depot_center.z)
        r.misc[:workshop_level] = 0
        r.layout << { :dig => :Ramp, :x => -1, :y => 1 }
        r.layout << { :dig => :Ramp, :x => -1, :y => 2 }
        r.layout << { :dig => :Ramp, :x => -1, :y => 3 }
        @rooms << r
    else
        r = Room.new(:workshop, :TradeDepot, @fort_entrance.x-7, @fort_entrance.x-3, @fort_entrance.y-2, @fort_entrance.y+2, @fort_entrance.z2)
        r.misc[:workshop_level] = 0
        5.times { |x| 5.times { |y|
            r.layout << { :construction => :Floor, :x => x, :y => y }
        } }
        @rooms << r
    end
end

def setup_blueprint_stockpiles(fx, fy, fz, entr)
    corridor_center0 = Corridor.new(fx-1, fx-1, fy-1, fy+1, fz, fz)
    corridor_center0.layout << {:item => :door, :x => -1, :y => 0}
    corridor_center0.layout << {:item => :door, :x => -1, :y => 2}
    corridor_center0.accesspath = entr
    @corridors << corridor_center0
    corridor_center2 = Corridor.new(fx+1, fx+1, fy-1, fy+1, fz, fz)
    corridor_center2.layout << {:item => :door, :x => 1, :y => 0}
    corridor_center2.layout << {:item => :door, :x => 1, :y => 2}
    corridor_center2.accesspath = entr
    @corridors << corridor_center2

    types = [:food,:furniture, :wood,:stone, :refuse,:animals, :corpses,:gems]
    types += [:finished_goods,:cloth, :bars_blocks,:leather, :ammo,:armor, :weapons,:coins]

    # TODO side stairs to workshop level ?
    [-1, 1].each { |dirx|
        prev_corx = (dirx < 0 ? corridor_center0 : corridor_center2)
        ocx = fx + dirx*3
        (1..4).each { |dx|
            # segments of the big central horizontal corridor
            cx = fx + dirx*(8*dx-4)
            cor_x = Corridor.new(ocx, cx+dirx, fy-1, fy+1, fz, fz)
            cor_x.accesspath = [prev_corx]
            @corridors << cor_x
            prev_corx = cor_x
            ocx = cx+2*dirx

            t0, t1 = types.shift, types.shift
            @rooms << Room.new(:stockpile, t0, cx-3, cx+3, fy-3, fy-4, fz)
            @rooms << Room.new(:stockpile, t1, cx-3, cx+3, fy+3, fy+4, fz)
            @rooms[-2, 2].each { |r| r.misc[:stockpile_level] = 1 }
            @rooms[-2, 2].each { |r|
                r.accesspath = [cor_x]
                r.layout << {:item => :door, :x => 2, :y => (r.y < fy ? 2 : -1)}
                r.layout << {:item => :door, :x => 4, :y => (r.y < fy ? 2 : -1)}
            }
            @rooms << Room.new(:stockpile, t0, cx-3, cx+3, fy-5, fy-11, fz)
            @rooms << Room.new(:stockpile, t1, cx-3, cx+3, fy+5, fy+11, fz)
            @rooms[-2, 2].each { |r| r.misc[:stockpile_level] = 2 }
            @rooms << Room.new(:stockpile, t0, cx-3, cx+3, fy-12, fy-20, fz)
            @rooms << Room.new(:stockpile, t1, cx-3, cx+3, fy+12, fy+20, fz)
            @rooms[-2, 2].each { |r| r.misc[:stockpile_level] = 3 }
        }
    }
    @rooms.each { |r|
        if r.type == :stockpile and r.subtype == :coins and r.misc[:stockpile_level] > 1
            r.subtype = :furniture
            r.misc[:stockpile_level] += 2
        end
    }

    setup_blueprint_minecarts
    setup_blueprint_pitcage
end

def setup_blueprint_minecarts
    return # disabled 2015-02-04 BenLubar

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
end

def setup_blueprint_pitcage
    return if not gpit = find_room(:garbagepit)
    r = Room.new(:pitcage, nil, gpit.x1-1, gpit.x1+1, gpit.y1-1, gpit.y1+1, gpit.z1+10)
    r.layout << { :construction => :UpStair, :x => -1, :y => 1, :z => -10 }
    r.layout << { :construction => :UpDownStair, :x => -1, :y => 1, :z => -9 }
    r.layout << { :construction => :UpDownStair, :x => -1, :y => 1, :z => -8 }
    r.layout << { :construction => :UpDownStair, :x => -1, :y => 1, :z => -7 }
    r.layout << { :construction => :UpDownStair, :x => -1, :y => 1, :z => -6 }
    r.layout << { :construction => :UpDownStair, :x => -1, :y => 1, :z => -5 }
    r.layout << { :construction => :UpDownStair, :x => -1, :y => 1, :z => -4 }
    r.layout << { :construction => :UpDownStair, :x => -1, :y => 1, :z => -3 }
    r.layout << { :construction => :UpDownStair, :x => -1, :y => 1, :z => -2 }
    r.layout << { :construction => :UpDownStair, :x => -1, :y => 1, :z => -1 }
    r.layout << { :construction => :DownStair, :x => -1, :y => 1, :z => 0 }
    [0, 1, 2].each { |dx| [1, 0, 2].each { |dy|
        if dx == 1 and dy == 1
            r.layout << { :dig => :Channel, :x => dx, :y => dy }
        else
            r.layout << { :construction => :Floor, :x => dx, :y => dy }
        end
    } }
    r.layout << { :construction => :Floor, :x => 3, :y => 1, :item => :hive }
    @rooms << r
    stockpile = Room.new(:stockpile, :animals, r.x1, r.x2, r.y1, r.y2, r.z1)
    stockpile.misc[:stockpile_level] = 0
    stockpile.layout = r.layout
    @rooms << stockpile
end

def setup_blueprint_utilities(fx, fy, fz, entr)
    corridor_center0 = Corridor.new(fx-1, fx-1, fy-1, fy+1, fz, fz)
    corridor_center0.layout << {:item => :door, :x => -1, :y => 0}
    corridor_center0.layout << {:item => :door, :x => -1, :y => 2}
    corridor_center0.accesspath = entr
    @corridors << corridor_center0
    corridor_center2 = Corridor.new(fx+1, fx+1, fy-1, fy+1, fz, fz)
    corridor_center2.layout << {:item => :door, :x => 1, :y => 0}
    corridor_center2.layout << {:item => :door, :x => 1, :y => 2}
    corridor_center2.accesspath = entr
    @corridors << corridor_center2

    # dining halls
    ocx = fx-2
    old_cor = corridor_center0

    # temporary dininghall, for fort start
    tmp = Room.new(:dininghall, nil, fx-4, fx-3, fy-1, fy+1, fz)
    tmp.misc[:temporary] = true
    [0, 1, 2].each { |dy|
        tmp.layout << {:item => :table, :x => 0, :y => dy, :users => []}
        tmp.layout << {:item => :chair, :x => 1, :y => dy, :users => []}
    }
    tmp.layout[0][:makeroom] = true
    tmp.accesspath = [old_cor]
    @rooms << tmp


    # dininghalls x 4 (54 users each)
    [0, 1].each { |ax|
        cor = Corridor.new(ocx-1, fx-ax*12-5, fy-1, fy+1, fz, fz)
        cor.accesspath = [old_cor]
        @corridors << cor
        ocx = fx-ax*12-4
        old_cor = cor

        [-1, 1].each { |dy|
            dinner = Room.new(:dininghall, nil, fx-ax*12-2-10, fx-ax*12-2, fy+dy*9, fy+dy*3, fz)
            dinner.layout << {:item => :door, :x => 7, :y => (dy>0 ? -1 : 7)}
            dinner.layout << {:construction => :Wall, :dig => :No, :x => 2, :y => 3}
            dinner.layout << {:construction => :Wall, :dig => :No, :x => 8, :y => 3}
            [5,4,6,3,7,2,8,1,9].each { |dx|
                [-1, 1].each { |sy|
                    dinner.layout << {:item => :table, :x => dx, :y => 3+dy*sy*1, :ignore => true, :users => []}
                    dinner.layout << {:item => :chair, :x => dx, :y => 3+dy*sy*2, :ignore => true, :users => []}
                }
            }
            dinner.layout.find { |f| f[:item] == :table }[:makeroom] = true
            dinner.accesspath = [cor]
            @rooms << dinner
        }
    }

    if @allow_ice
        ai.debug "icy embark, no well"
    elsif river = scan_river
        setup_blueprint_cistern_fromsource(river, fx, fy, fz)
    else
        # TODO pool, pumps, etc
        ai.debug "no river, no well"
    end

    # farm plots
    farm_h = 3
    farm_w = 3
    dpf = (@dwarves_per_farmtile * farm_h * farm_w).to_i
    nrfarms = (220+dpf-1)/dpf

    cx = fx+4*6       # end of workshop corridor (last ws door)
    cy = fy
    cz = find_room(:workshop) { |r| r.subtype == :Farmers }.z1
    ws_cor = Corridor.new(fx+3, cx+1, cy, cy, cz, cz)    # ws_corr.access_path ...
    @corridors << ws_cor
    farm_stairs = Corridor.new(cx+2, cx+2, cy, cy, cz, cz)
    farm_stairs.accesspath = [ws_cor]
    @corridors << farm_stairs
    cx += 3
    soilcnt = {}
    (cz...df.world.map.z_count).each { |z|
        scnt = 0
        if (-1..(nrfarms*farm_w/3)).all? { |dx|
            (-(3*farm_h+farm_h-1)..(3*farm_h+farm_h-1)).all? { |dy|
                t = df.map_tile_at(cx+dx, cy+dy, z)
                next if not t or t.shape != :WALL
                scnt += 1 if t.tilemat == :SOIL
                true
            }
        }
            soilcnt[z] = scnt
        end
    }
    cz2 = soilcnt.index(soilcnt.values.max) || cz

    farm_stairs.z2 = cz2
    cor = Corridor.new(cx, cx+1, cy, cy, cz2, cz2)
    cor.accesspath = [farm_stairs]
    @corridors << cor
    types = [:food, :cloth]
    [-1, 1].each { |dy|
        st = types.shift
        (nrfarms/3).times { |dx|
            3.times { |ddy|
                r = Room.new(:farmplot, st, cx+farm_w*dx, cx+farm_w*dx+farm_w-1, cy+dy*2+dy*ddy*farm_h, cy+dy*(2+farm_h-1)+dy*ddy*farm_h, cz2)
                r.misc[:users] = []
                if dx == 0 and ddy == 0
                    r.layout << {:item => :door, :x => 1, :y => (dy>0 ? -1 : farm_h)}
                    r.accesspath = [cor]
                else
                    r.accesspath = [@rooms.last]
                end
                @rooms << r
            }
        }
    }
    # seeds stockpile
    r = Room.new(:stockpile, :food, cx+2, cx+4, cy, cy, cz2)
    r.misc[:stockpile_level] = 0
    r.misc[:workshop] = @rooms[-2*nrfarms]
    r.accesspath = [cor]
    @rooms << r

    # garbage dump
    # TODO ensure flat space, no pools/tree, ...
    r = Room.new(:garbagedump, nil, cx+5, cx+5, cy, cy, cz)
    tile = spiral_search(df.map_tile_at(r)) { |t|
        t = surface_tile_at(t) and
        (t.x >= cx + 5 or
        (t.z > cz + 2 and t.x > fx + 5)) and
        map_tile_in_rock(t.offset(0, 0, -1)) and
        map_tile_in_rock(t.offset(2, 0, -1)) and
        t.shape_basic == :Floor and
        t.offset(1, 0, 0).shape_basic == :Floor and
        t.offset(2, 0, 0).shape_basic == :Floor and
        (t.offset(1, 1, 0).shape_basic == :Floor or
        t.offset(1, -1, 0).shape_basic == :Floor)
    }
    r.x1 = r.x2 = tile.x
    r.y1 = r.y2 = tile.y
    r.z2 = r.z1 = surface_tile_at(tile).z
    @rooms << r
    r = Room.new(:garbagepit, nil, @rooms.last.x + 1, @rooms.last.x + 2, @rooms.last.y, @rooms.last.y, @rooms.last.z)
    @rooms << r

    # infirmary
    old_cor = corridor_center2
    cor = Corridor.new(fx+3, fx+5, fy-1, fy+1, fz, fz)
    cor.accesspath = [old_cor]
    @corridors << cor
    old_cor = cor

    infirmary = Room.new(:infirmary, nil, fx+2, fx+6, fy-3, fy-7, fz)
    infirmary.layout << {:item => :door, :x => 3, :y => 5}
    infirmary.layout << {:item => :bed, :x => 0, :y => 1}
    infirmary.layout << {:item => :table, :x => 1, :y => 1}
    infirmary.layout << {:item => :bed, :x => 2, :y => 1}
    infirmary.layout << {:item => :traction_bench, :x => 0, :y => 2}
    infirmary.layout << {:item => :traction_bench, :x => 2, :y => 2}
    infirmary.layout << {:item => :bed, :x => 0, :y => 3}
    infirmary.layout << {:item => :table, :x => 1, :y => 3}
    infirmary.layout << {:item => :bed, :x => 2, :y => 3}
    infirmary.layout << {:item => :chest, :x => 4, :y => 1}
    infirmary.layout << {:item => :chest, :x => 4, :y => 2}
    infirmary.layout << {:item => :chest, :x => 4, :y => 3}
    infirmary.accesspath = [cor]
    @rooms << infirmary

    # cemetary lots (160 spots)
    cor = Corridor.new(fx+6, fx+14, fy-1, fy+1, fz, fz)
    cor.accesspath = [old_cor]
    @corridors << cor
    old_cor = cor

    500.times { |ry|
        break if (-1..19).find { |tx| (-1..4).find { |ty|
            not t = df.map_tile_at(fx+10+tx, fy-3-3*ry-ty, fz) or t.shape_basic != :Wall
        } }
        2.times { |rrx|
            2.times { |rx|
                ox = fx+10+5*rx + 9*rrx
                oy = fy-3-3*ry
                cemetary = Room.new(:cemetary, nil, ox, ox+4, oy, oy-3, fz)
                4.times { |dx|
                    2.times { |dy|
                        cemetary.layout << {:item => :coffin, :x => dx+1-rx, :y =>dy+1, :ignore => true, :users => []}
                    }
                }
                if rx == 0 and ry == 0 and rrx == 0
                    cemetary.layout << {:item => :door, :x => 4, :y => 4}
                    cemetary.accesspath = [cor]
                end
                @rooms << cemetary
            }
        }
    }

    # barracks
    # 8 dwarf per squad, 20% pop => 40 soldiers for 200 dwarves => 5 barracks
    old_cor = corridor_center2
    oldcx = old_cor.x2+2     # door
    4.times { |rx|
        cor = Corridor.new(oldcx, fx+5+10*rx, fy-1, fy+1, fz, fz)
        cor.accesspath = [old_cor]
        @corridors << cor
        old_cor = cor
        oldcx = cor.x2+1

        [1, -1].each { |ry|
            next if ry == -1 and rx < 3 # infirmary/cemetary

            barracks = Room.new(:barracks, nil, fx+2+10*rx, fx+2+10*rx+6, fy+3*ry, fy+10*ry, fz)
            barracks.layout << {:item => :door, :x => 3, :y => (ry > 0 ? -1 : 8)}
            8.times { |dy|
                dy = 7-dy if ry < 0
                barracks.layout << {:item => :armorstand, :x => 5, :y => dy, :ignore => true, :users => []}
                barracks.layout << {:item => :bed, :x => 6, :y => dy, :ignore => true, :users => []}
                barracks.layout << {:item => :cabinet, :x => 0, :y => dy, :ignore => true, :users => []}
                barracks.layout << {:item => :chest, :x => 1, :y => dy, :ignore => true, :users => []}
            }
            barracks.layout << {:item => :weaponrack, :x => 4, :y => (ry>0 ? 7 : 0), :makeroom => true, :users => []}
            barracks.layout << {:item => :weaponrack, :x => 2, :y => (ry>0 ? 7 : 0), :ignore => true, :users => []}
            barracks.layout << {:item => :archerytarget, :x => 3, :y => (ry>0 ? 7 : 0), :ignore => true, :users => []}
            barracks.accesspath = [cor]
            @rooms << barracks
        }
    }

    setup_blueprint_pastures
    setup_blueprint_outdoor_farms(nrfarms * 2)
end

def setup_blueprint_cistern_fromsource(src, fx, fy, fz)
    # TODO dynamic layout, at least move the well/cistern on the side of the river
    # TODO scan for solid ground for all this

    # well
    cor = Corridor.new(fx-18, fx-26, fy-1, fy+1, fz, fz)
    cor.accesspath = [@corridors.last]
    @corridors << cor

    cx = fx-32
    well = Room.new(:well, :well, cx-4, cx+4, fy-4, fy+4, fz)
    well.layout << {:item => :well, :x => 4, :y => 4, :makeroom => true, :dig => :Channel}
    well.layout << {:item => :door, :x => 9, :y => 3}
    well.layout << {:item => :door, :x => 9, :y => 5}
    well.layout << {:item => :trap, :subtype => :lever, :x => 1, :y => 0, :way => :out}
    well.layout << {:item => :trap, :subtype => :lever, :x => 0, :y => 0, :way => :in}
    well.accesspath = [cor]
    @rooms << well

    # water cistern under the well (in the hole of bedroom blueprint)
    cist_cors = find_corridor_tosurface(df.map_tile_at(cx-8, fy, fz))
    cist_cors[0].z1 -= 3

    cistern = Room.new(:cistern, :well, cx-7, cx+1, fy-1, fy+1, fz-1)
    cistern.z1 -= 2
    cistern.accesspath = [cist_cors[0]]

    # handle low rivers / high mountain forts
    fz = src.z if fz > src.z
    # should be fine with cistern auto-fill checks
    cistern.z1 = fz if cistern.z1 > fz

    # staging reservoir to fill the cistern, one z-level at a time
    # should have capacity = 1 cistern level @7/7 + itself @1/7 (rounded up)
    #  cistern is 9x3 + 1 (stairs)
    #  reserve is 5x7 (can fill cistern 7/7 + itself 1/7 + 14 spare
    reserve = Room.new(:cistern, :reserve, cx-10, cx-14, fy-3, fy+3, fz)
    reserve.layout << {:item => :floodgate, :x => -1, :y => 3, :way => :in}
    reserve.layout << {:item => :floodgate, :x =>  5, :y => 3, :way => :out, :ignore => true}
    reserve.accesspath = [cist_cors[0]]

    # cisterns are dug in order
    # try to dig reserve first, so we have liquid water even if river freezes
    @rooms << reserve
    @rooms << cistern

    # link the cistern reserve to the water source

    # trivial walk of the river tiles to find a spot closer to dst
    move_river = lambda { |dst|
        nsrc = src
        500.times {
            break if not nsrc
            src = nsrc
            dist = src.distance_to(dst)
            nsrc = spiral_search(src, 1, 1) { |t|
                next if t.distance_to(dst) > dist
                t.designation.feature_local
            }
        }
    }

    # 1st end: reservoir input
    p1 = df.map_tile_at(cx-16, fy, fz)
    move_river[p1]
    ai.debug "cistern: reserve/in (#{p1.x}, #{p1.y}, #{p1.z}), river (#{src.x}, #{src.y}, #{src.z})"

    # XXX hardcoded layout again
    if src.x <= p1.x
        p = p1
        r = reserve
    else
        # the tunnel should walk around other blueprint rooms
        p2 = p1.offset(0, (src.y >= p1.y ? 26 : -26))
        cor = Corridor.new(p1.x, p2.x, p1.y, p2.y, p1.z, p2.z)
        @corridors << cor
        reserve.accesspath << cor
        move_river[p2]
        p = p2
        r = cor
    end

    up = find_corridor_tosurface(p)
    r.accesspath << up[0]

    dst = up[-1].maptile2.offset(0, 0, -2)
    dst = df.map_tile_at(dst.x, dst.y, src.z - 1) if src.z <= dst.z
    move_river[dst]

    if (dst.x - src.x).abs > 1
        p3 = dst.offset(src.x-dst.x, 0)
        move_river[p3]
    end

    # find safe tile near the river
    out = spiral_search(src) { |t| map_tile_in_rock(t) }

    # find tile to channel to start water flow
    channel = spiral_search(out, 1, 1) { |t|
        t.spiral_search(1, 1) { |tt| tt.designation.feature_local }
    } || spiral_search(out, 1, 1) { |t|
        t.spiral_search(1, 1) { |tt| tt.designation.flow_size != 0 or tt.tilemat == :FROZEN_LIQUID }
    }
    ai.debug "cistern: channel_enable (#{channel.x}, #{channel.y}, #{channel.z})" if channel

    # TODO check that 'channel' is easily channelable (eg river in a hole)

    if (dst.x - out.x).abs > 1
        cor = Corridor.new(dst.x - (dst.x <=> out.x), out.x - (out.x <=> dst.x), dst.y, dst.y, dst.z, dst.z)
        @corridors << cor
        r.accesspath << cor
        r = cor
    end

    if (dst.y - out.y).abs > 1
        cor = Corridor.new(out.x - (out.x <=> dst.x), out.x - (out.x <=> dst.x), dst.y + (out.y <=> dst.y), out.y, dst.z, dst.z)
        @corridors << cor
        r.accesspath << cor
    end

    up = find_corridor_tosurface(df.map_tile_at(out.x, out.y, dst.z))
    r.accesspath << up[0]

    reserve.misc[:channel_enable] = [channel.x, channel.y, channel.z] if channel
end

# scan for 11x11 flat areas with grass
def setup_blueprint_pastures
    want = 36
    @fort_entrance.maptile.spiral_search { |_t|
        next unless sf = surface_tile_at(_t)
        floortile = 0
        grasstile = 0
        if (-5..5).all? { |dx| (-5..5).all? { |dy|
            if tt = sf.offset(dx, dy) and
                (tt.shape_basic == :Floor or tt.tilemat == :TREE) and
                tt.designation.flow_size == 0 and
                tt.tilemat != :FROZEN_LIQUID

                grasstile += 1 if tt.mapblock.block_events.any? { |be|
                    be.kind_of?(DFHack::BlockSquareEventGrassst) and be.amount[tt.dx][tt.dy] > 0
                }
                floortile += 1
            end

            tt and not map_tile_intersects_room(tt)
        } } and floortile >= 9*9 and grasstile >= 8*8
            @rooms << Room.new(:pasture, nil, sf.x-5, sf.x+5, sf.y-5, sf.y+5, sf.z)
            @rooms.last.misc[:users] = []
            want -= 1
            true if want == 0
        end
    }
end

# scan for 3x3 flat areas with soil
def setup_blueprint_outdoor_farms(want)
    @fort_entrance.maptile.spiral_search([df.world.map.x_count, df.world.map.y_count].max, 9, 3) { |_t|
        next unless sf = surface_tile_at(_t)
        sd = sf.designation
        if (-1..1).all? { |dx| (-1..1).all? { |dy|
            tt = sf.offset(dx, dy) and
            td = tt.designation and
            ((sd.subterranean and td.subterranean) or
            (not sd.subterranean and not td.subterranean and
                sd.biome == td.biome)) and
            tt.shape_basic == :Floor and
            tt.designation.flow_size == 0 and
            [:GRASS_DARK, :GRASS_LIGHT, :SOIL].include?(tt.tilemat)
        } }
            @rooms << Room.new(:farmplot, [:food, :cloth][want % 2], sf.x-1, sf.x+1, sf.y-1, sf.y+1, sf.z)
            @rooms.last.misc[:users] = []
            @rooms.last.misc[:outdoor] = true
            want -= 1
            true if want == 0
        end
    }
end

def setup_blueprint_bedrooms(fx, fy, fz, entr)
    corridor_center0 = Corridor.new(fx-1, fx-1, fy-1, fy+1, fz, fz)
    corridor_center0.layout << {:item => :door, :x => -1, :y => 0}
    corridor_center0.layout << {:item => :door, :x => -1, :y => 2}
    corridor_center0.accesspath = entr
    @corridors << corridor_center0
    corridor_center2 = Corridor.new(fx+1, fx+1, fy-1, fy+1, fz, fz)
    corridor_center2.layout << {:item => :door, :x => 1, :y => 0}
    corridor_center2.layout << {:item => :door, :x => 1, :y => 2}
    corridor_center2.accesspath = entr
    @corridors << corridor_center2

    [-1, 1].each { |dirx|
        prev_corx = (dirx < 0 ? corridor_center0 : corridor_center2)
        ocx = fx + dirx*3
        (1..3).each { |dx|
            # segments of the big central horizontal corridor
            cx = fx + dirx*(9*dx-4)
            cor_x = Corridor.new(ocx, cx, fy-1, fy+1, fz, fz)
            cor_x.accesspath = [prev_corx]
            @corridors << cor_x
            prev_corx = cor_x
            ocx = cx+dirx

            [-1, 1].each { |diry|
                prev_cory = cor_x
                ocy = fy + diry*2
                (1..6).each { |dy|
                    cy = fy + diry*3*dy
                    cor_y = Corridor.new(cx, cx-dirx*1, ocy, cy, fz, fz)
                    cor_y.accesspath = [prev_cory]
                    @corridors << cor_y
                    prev_cory = cor_y
                    ocy = cy+diry

                    @rooms << Room.new(:bedroom, nil, cx-dirx*4, cx-dirx*3, cy, cy+diry, fz)
                    @rooms << Room.new(:bedroom, nil, cx+dirx*2, cx+dirx*3, cy, cy+diry, fz)
                    @rooms[-2, 2].each { |r|
                        r.accesspath = [cor_y]
                        r.layout << {:item => :bed, :x => (r.x<cx ? 0 : 1), :y => (diry<0 ? 1 : 0), :makeroom => true}
                        r.layout << {:item => :cabinet, :x => (r.x<cx ? 0 : 1), :y => (diry<0 ? 0 : 1), :ignore => true}
                        r.layout << {:item => :chest, :x => (r.x<cx ? 1 : 0), :y => (diry<0 ? 0 : 1), :ignore => true}
                        r.layout << {:item => :door, :x => (r.x<cx ? 2 : -1), :y => (diry<0 ? 1 : 0)}
                    }
                }
            }
        }

        # noble suites
        cx = fx + dirx*(9*3-4+6)
        cor_x = Corridor.new(ocx, cx, fy-1, fy+1, fz, fz)
        cor_x2 = Corridor.new(ocx-dirx, fx+dirx*3, fy, fy, fz, fz)
        cor_x.accesspath = [cor_x2]
        cor_x2.accesspath = [dirx < 0 ? corridor_center0 : corridor_center2]
        @corridors << cor_x << cor_x2

        [-1, 1].each { |diry|
            @noblesuite ||= -1
            @noblesuite += 1

            r = Room.new(:nobleroom, :office, cx-1, cx+1, fy+diry*3, fy+diry*5, fz)
            r.misc[:noblesuite] = @noblesuite
            r.accesspath = [cor_x]
            r.layout << {:item => :chair, :x => 1, :y => 1, :makeroom => true}
            r.layout << {:item => :chair, :x => 1-dirx, :y => 1}
            r.layout << {:item => :chest, :x => 1+dirx, :y => 0, :ignore => true}
            r.layout << {:item => :cabinet, :x => 1+dirx, :y => 2, :ignore => true}
            r.layout << {:item => :door, :x => 1, :y => 1-2*diry}
            @rooms << r

            r = Room.new(:nobleroom, :bedroom, cx-1, cx+1, fy+diry*7, fy+diry*9, fz)
            r.misc[:noblesuite] = @noblesuite
            r.layout << {:item => :bed, :x => 1, :y => 1, :makeroom => true}
            r.layout << {:item => :armorstand, :x => 1-dirx, :y => 0, :ignore => true}
            r.layout << {:item => :weaponrack, :x => 1-dirx, :y => 2, :ignore => true}
            r.layout << {:item => :door, :x => 1, :y => 1-2*diry, :internal => true}
            r.accesspath = [@rooms.last]
            @rooms << r

            r = Room.new(:nobleroom, :diningroom, cx-1, cx+1, fy+diry*11, fy+diry*13, fz)
            r.misc[:noblesuite] = @noblesuite
            r.layout << {:item => :table, :x => 1, :y => 1, :makeroom => true}
            r.layout << {:item => :chair, :x => 1+dirx, :y => 1}
            r.layout << {:item => :cabinet, :x => 1-dirx, :y => 0, :ignore => true}
            r.layout << {:item => :chest, :x => 1-dirx, :y => 2, :ignore => true}
            r.layout << {:item => :door, :x => 1, :y => 1-2*diry, :internal => true}
            r.accesspath = [@rooms.last]
            @rooms << r

            r = Room.new(:nobleroom, :tomb, cx-1, cx+1, fy+diry*15, fy+diry*17, fz)
            r.misc[:noblesuite] = @noblesuite
            r.layout << {:item => :coffin, :x => 1, :y => 1, :makeroom => true}
            r.layout << {:item => :door, :x => 1, :y => 1-2*diry, :internal => true}
            r.accesspath = [@rooms.last]
            @rooms << r
        }
    }
end

def setup_outdoor_gathering_zones
    x = 0
    y = 0
    i = 0
    ground = {}
    bg = df.onupdate_register('df-ai plan setup_outdoor_gathering_zones', 10) do
        bg.description = "df-ai plan setup_outdoor_gathering_zones #{i}"
        if x+i == [x+31, df.world.map.x_count].min
            ground.keys.each do |tz|
                g = ground[tz]
                bld = df.building_alloc(:Civzone, :ActivityZone)
                bld.zone_flags.active = true
                bld.zone_flags.gather = true
                bld.gather_flags.pick_trees = true
                bld.gather_flags.pick_shrubs = true
                bld.gather_flags.gather_fallen = true
                w = 31
                h = 31
                w = df.world.map.x_count % 31 if x + 31 > df.world.map.x_count
                h = df.world.map.y_count % 31 if y + 31 > df.world.map.y_count
                df.building_position(bld, [x, y, tz], w, h)
                bld.room.extents = df.malloc(w * h)
                bld.room.x = x
                bld.room.y = y
                bld.room.width = w
                bld.room.height = h
                w.times do |cx|
                    h.times do |cy|
                        bld.room.extents[cx + w * cy] = g[[cx, cy]] ? 1 : 0
                    end
                end
                bld.is_room = 1
                df.building_construct_abstract(bld)
            end

            ground.clear
            i = 0
            x += 31
            if x >= df.world.map.x_count
                x = 0
                y += 31
                if y >= df.world.map.y_count
                    df.onupdate_unregister(bg)
                    ai.debug 'plan setup_outdoor_gathering_zones finished'
                    next
                end
            end
            next
        end

        tx = x + i
        (y...([y+31, df.world.map.y_count].min)).each do |ty|
            next unless t = surface_tile_at(tx, ty, true)
            tz = t.z
            ground[tz] ||= {}
            ground[tz][[tx % 31, ty % 31]] = true
        end
        i += 1
    end
end

def setup_blueprint_caverns
    wall = nil
    unless (0...(@cavern_max_level ||= df.world.map.z_count)).to_a.reverse.any? { |z|
        ai.debug "outpost: searching z-level #{z}"
        (0...df.world.map.x_count).any? { |x|
            (0...df.world.map.y_count).any? { |y|
                t = df.map_tile_at(x, y, z) and
                map_tile_in_rock(t) and
                spiral_search(t, 2) { |_t|
                    map_tile_cavernfloor(_t)
                } and
                #@rooms.all? { |r|
                #    not r.safe_include?(t.x, t.y, r.z)
                #} and @corridors.all? { |r|
                #    not r.safe_include?(t.x, t.y, r.z)
                #} and
                wall = t
            }
        }
    }
        ai.debug 'outpost: could not find a cavern wall tile'
        @cavern_max_level = 0
        return
    end

    @cavern_max_level = wall.z + 1

    # find a floor next to the wall
    unless target = spiral_search(wall, 2) { |t|
        map_tile_cavernfloor(t)
    }
        ai.debug 'outpost: could not find a cavern floor tile'
        @cavern_max_level = 0
        return
    end

    r = Room.new(:outpost, :cavern, target.x, target.x, target.y, target.y, target.z)

    if (wall.x - target.x).abs > 1
        cor = Corridor.new(wall.x - (wall.x <=> target.x), target.x - (target.x <=> wall.x), wall.y, wall.y, wall.z, wall.z)
        @corridors << cor
        r.accesspath << cor
    end

    if (wall.y - target.y).abs > 1
        cor = Corridor.new(target.x - (target.x <=> wall.x), target.x - (target.x <=> wall.x), wall.y + (target.y <=> wall.y), target.y, wall.z, wall.z)
        @corridors << cor
        r.accesspath << cor
    end

    up = find_corridor_tosurface(wall)
    r.accesspath << up[0]

    ai.debug "outpost: wall (#{wall.x}, #{wall.y}, #{wall.z})"
    ai.debug "outpost: target (#{target.x}, #{target.y}, #{target.z})"
    ai.debug "outpost: up (#{up.last.x2}, #{up.last.y2}, #{up.last.z2})"

    @rooms << r

    true
end

# check that tile is surrounded by solid rock/soil walls
def map_tile_in_rock(tile)
    tile and (-1..1).all? { |dx| (-1..1).all? { |dy|
        t = tile.offset(dx, dy) and t.shape_basic == :Wall and tm = t.tilemat and (tm == :STONE or tm == :MINERAL or tm == :SOIL or tm == :ROOT or (@allow_ice and tm == :FROZEN_LIQUID))
    } }
end

# check tile is surrounded by solid walls or visible tile
def map_tile_nocavern(tile)
    (-1..1).all? { |dx| (-1..1).all? { |dy|
        next if not t = tile.offset(dx, dy)
        tm = t.tilemat
        if !t.designation.hidden
            t.designation.flow_size < 4 and (@allow_ice or tm != :FROZEN_LIQUID)
        else
            t.shape_basic == :Wall and (tm == :STONE or tm == :MINERAL or tm == :SOIL or tm == :ROOT or (@allow_ice and tm == :FROZEN_LIQUID))
        end
    } }
end

# check tile is a hidden floor
def map_tile_cavernfloor(t)
    td = t.designation and
    td.hidden and
    td.flow_size == 0 and
    tm = t.tilemat and
    (tm == :STONE or tm == :MINERAL or tm == :SOIL or tm == :ROOT or tm == :GRASS_LIGHT or tm == :GRASS_DARK or tm == :PLANT or tm == :SOIL) and
    t.shape_basic == :Floor
end

# create a new Corridor from origin to surface, through rock
# may create multiple chunks to avoid obstacles, all parts are added to @corridors
# returns an array of Corridors, 1st = origin, last = surface
def find_corridor_tosurface(origin)
    cors = []
    while true
        cor = Corridor.new(origin.x, origin.x, origin.y, origin.y, origin.z, origin.z)
        cors.last.accesspath << cor unless cors.empty?
        cors << cor

        cor.z2 += 1 while map_tile_in_rock(cor.maptile2) and not map_tile_intersects_room(cor.maptile2.offset(0, 0, 1))

        if out = cor.maptile2 and (
            (out.shape_basic != :Ramp and out.shape_basic != :Floor) or
            out.tilemat == :TREE or
            out.tilemat == :RAMP or
            out.designation.flow_size != 0 or
            out.designation.hidden)

            out2 = spiral_search(out) { |t|
                t = t.offset(0, 0, 1) while map_tile_in_rock(t)

                (t.shape_basic == :Ramp or t.shape_basic == :Floor) and
                t.tilemat != :TREE and t.designation.flow_size == 0 and
                not t.designation.hidden and
                not map_tile_intersects_room(t)
            }

            if out.designation.flow_size > 0
                # damp stone located
                cor.z2 -= 2
                out2 = out2.offset(0, 0, -2)
            else
                cor.z2 -= 1
                out2 = out2.offset(0, 0, -1)
            end

            if (out2.x - out.x).abs > 1
                cor = Corridor.new(out2.x - (out2.x <=> out.x), out.x - (out.x <=> out2.x), out2.y, out2.y, out2.z, out2.z)
                cors.last.accesspath << cor
                cors << cor
            end

            if (out2.y - out.y).abs > 1
                cor = Corridor.new(out.x - (out.x <=> out2.x), out.x - (out.x <=> out2.x), out2.y + (out.y <=> out2.y), out.y, out2.z, out2.z)
                cors.last.accesspath << cor
                cors << cor
            end

            raise "find_corridor_tosurface: loop: #{origin.inspect}" if df.same_pos?(origin, out2)
            ai.debug "find_corridor_tosurface: #{origin.inspect} -> #{out2.inspect}"

            origin = out2
        else
            break
        end
    end
    @corridors += cors
    cors
end

def surface_tile_at(t, ty=nil, allow_trees=false)
    @rangez ||= (0...df.world.map.z_count).to_a.reverse

    if ty
        tx = t
    else
        tx, ty = t.x, t.y
    end

    dx, dy = tx & 15, ty & 15

    tree = false
    @rangez.each do |z|
        next unless b = df.map_block_at(tx, ty, z)
        next unless tt = b.tiletype[dx][dy]
        next unless ts = DFHack::Tiletype::Shape[tt]
        next unless tsb = DFHack::TiletypeShape::BasicShape[ts]
        next if tsb == :Open
        next unless tm = DFHack::Tiletype::Material[tt]
        return if tm == :POOL or tm == :RIVER
        if tsb == :Floor or tsb == :Ramp
            return df.map_tile_at(tx, ty, z) if tm != :TREE
        end
        if tm == :TREE
            tree = true
        elsif tree
            return df.map_tile_at(tx, ty, z + 1) if allow_trees
            return
        end
    end
    return
end

def status
    status = @tasks.inject(Hash.new(0)) { |h, t| h[t[0]] += 1; h }.map { |t, n| "#{t}: #{n}" }.join(', ')
    if task = digging?
        status << ", digging: #{describe_room(task[1])}"
    end
    status
end

def categorize_all
    @room_category = {}
    @rooms.each { |r|
        (@room_category[r.type] ||= []) << r
    }
    @room_category[:stockpile] = @room_category[:stockpile].sort_by { |r|
        [r.misc[:stockpile_level], (r.x1 < @fort_entrance.x ? -r.x1 : r.x1), r.y1]
    } if @room_category[:stockpile]
end

def describe_room(r)
    "#{@rooms.index(r) or @corridors.index(r)} #{r}"
end

def find_room(type, &b)
    if @room_category.empty?
        if b
            @rooms.find { |r| r.type == type and b[r] }
        else
            @rooms.find { |r| r.type == type }
        end
    else
        if b
            @room_category[type].to_a.find(&b)
        else
            @room_category[type].to_a.first
        end
    end
end

def serialize
    {
        :rooms     => @rooms.map(&:serialize),
        :corridors => @corridors.map(&:serialize),
        :tasks     => @tasks.map{ |t| t.map{ |v|
            if i = $dwarfAI.plan.instance_variable_get(:@rooms).index(v) # XXX
                [:room, i]
            elsif i = $dwarfAI.plan.instance_variable_get(:@corridors).index(v) # XXX
                [:corridor, i]
            else
                v
            end
        } },
    }
end

def map_tile_intersects_room(t)
    x, y, z = t.x, t.y, t.z
    @rooms.any? { |r|
        r.safe_include?(x, y, z)
    } or @corridors.any? { |r|
        r.safe_include?(x, y, z)
    }
end

def self.find_tree_base(t)
    tree = (df.world.plants.tree_dry.to_a | df.world.plants.tree_wet.to_a).find { |tree|
        next true if tree.pos.x == t.x and tree.pos.y == t.y and tree.pos.z == t.z
        next unless tree.tree_info and tree.tree_info.body
        sx = tree.pos.x - tree.tree_info.dim_x / 2
        sy = tree.pos.y - tree.tree_info.dim_y / 2
        sz = tree.pos.z
        next if t.x < sx or t.y < sy or t.z < sz or t.x >= sx + tree.tree_info.dim_x or t.y >= sy + tree.tree_info.dim_y or t.z >= sz + tree.tree_info.body_height
        next unless tree.tree_info.body[(t.z - sz)]
        tile = tree.tree_info.body[(t.z - sz)][(t.x - sx) + tree.tree_info.dim_x * (t.y - sy)]
        tile._whole != 0 and not tile.blocked
    }
    if tree
        df.map_tile_at(tree)
    else
        $dwarfAI.debug "failed to find tree at #{t.inspect}"
        t
    end
end

class Corridor
    attr_accessor :x1, :x2, :y1, :y2, :z1, :z2, :accesspath, :status, :layout, :type
    attr_accessor :owner, :subtype
    attr_accessor :misc
    def w; x2-x1+1; end
    def h; y2-y1+1; end
    def h_z; z2-z1+1; end
    def x; x1+w/2; end
    def y; y1+h/2; end
    def z; z1+h_z/2; end
    def maptile;  df.map_tile_at(x,  y,  z);  end
    def maptile1; df.map_tile_at(x1, y1, z1); end
    def maptile2; df.map_tile_at(x2, y2, z2); end

    def initialize(x1, x2, y1, y2, z1, z2)
        @misc = {}
        @status = :plan
        @accesspath = []
        @layout = []
        @type = :corridor
        x1, x2 = x2, x1 if x1 > x2
        y1, y2 = y2, y1 if y1 > y2
        z1, z2 = z2, z1 if z1 > z2
        @x1, @x2, @y1, @y2, @z1, @z2 = x1, x2, y1, y2, z1, z2
    end

    def dig(mode=nil)
        if mode == :plan
            plandig = true
            mode = nil
        end
        (@x1..@x2).each { |x| (@y1..@y2).each { |y| (@z1..@z2).each { |z|
            if t = df.map_tile_at(x, y, z)
                next if t.tilemat == :CONSTRUCTION
                dm = mode || dig_mode(t.x, t.y, t.z)
                if dm != :No and t.tilemat == :TREE
                    dm = :Default
                    t = DwarfAI::Plan::find_tree_base(t)
                end
                t.dig dm if ((dm == :DownStair or dm == :Channel) and t.shape != :STAIR_DOWN and t.shape_basic != :Open) or
                        t.shape == :WALL
            end
        } } }
        return if plandig
        @layout.each { |f|
            if t = df.map_tile_at(x1+f[:x].to_i, y1+f[:y].to_i, z1+f[:z].to_i)
                next if t.tilemat == :CONSTRUCTION
                if f[:dig]
                    t.dig f[:dig] if t.shape_basic == :Wall or (f[:dig] == :Channel and t.shape_basic != :Open)
                else
                    dm = dig_mode(t.x, t.y, t.z)
                    t.dig dm if (dm == :DownStair and t.shape != :STAIR_DOWN) or t.shape == :WALL
                end
            end
        }
    end

    def fixup_open
        (x1..x2).each { |x| (y1..y2).each { |y| (z1..z2).each { |z|
            if f = layout.find { |f|
                fx, fy, fz = x1 + f[:x].to_i, y1 + f[:y].to_i, z1 + f[:z].to_i
                fx == x and fy == y and fz == z
            }
                fixup_open_tile(x, y, z, f[:dig] || :Default, f) unless f[:construction]
            else
                fixup_open_tile(x, y, z, dig_mode(x, y, z))
            end
        } } }
    end

    def fixup_open_tile(x, y, z, d, f=nil)
        return unless t = df.map_tile_at(x, y, z)
        case d
        when :Channel, :No
            # do nothing
        when :Default
             fixup_open_helper(x, y, z, :Floor, f) if t.shape_basic == :Open
        when :UpDownStair, :UpStair, :Ramp
             fixup_open_helper(x, y, z, d, f) if t.shape_basic == :Open or t.shape_basic == :Floor
        when :DownStair
             fixup_open_helper(x, y, z, d, f) if t.shape_basic == :Open
        end
    end

    def fixup_open_helper(x, y, z, c, f=nil)
        unless f
            f = {:x => x - x1, :y => y - y1, :z => z - z1}
            layout << f
        end
        f[:construction] = c
    end

    def include?(x, y, z)
        x1 <= x and x2 >= x and y1 <= y and y2 >= y and z1 <= z and z2 >= z
    end

    def safe_include?(x, y, z)
        (x1 - 1 <= x and x2 + 1 >= x and y1 - 1 <= y and y2 + 1 >= y and z1 <= z and z2 >= z) or
        layout.any? { |f|
            fx, fy, fz = x1 + f[:x].to_i, y1 + f[:y].to_i, z1 + f[:z].to_i
            fx - 1 <= x and fx + 1 >= x and fy - 1 <= y and fy + 1 >= y and fz == z
        }
    end

    def dig_mode(x, y, z)
        return :Default if @type != :corridor
        wantup = include?(x, y, z+1)
        wantdown = include?(x, y, z-1)
        # XXX
        wantup ||= $dwarfAI.plan.instance_variable_get(:@corridors).any? { |r|
            (accesspath.include?(r) or r.z1 != r.z2) and r.include?(x, y, z+1)
        }
        wantdown ||= $dwarfAI.plan.instance_variable_get(:@corridors).any? { |r|
            (accesspath.include?(r) or r.z1 != r.z2) and r.include?(x, y, z-1)
        }

        if wantup
            wantdown ? :UpDownStair : :UpStair
        else
            wantdown ? :DownStair : :Default
        end
    end

    def dug?(want=nil)
        holes = []
        @layout.each { |f|
            next if f[:ignore]
            return if not t = df.map_tile_at(x1+f[:x].to_i, y1+f[:y].to_i, z1+f[:z].to_i)
            next holes << t if f[:dig] == :No
            case t.shape_basic
            when :Wall; return false
            when :Open
            else return false if f[:dig] == :Channel
            end
        }
        (@x1..@x2).each { |x| (@y1..@y2).each { |y| (@z1..@z2).each { |z|
            if t = df.map_tile_at(x, y, z)
                return false if (t.shape == :WALL or (want and t.shape_basic != want)) and not holes.find { |h| h.x == t.x and h.y == t.y and h.z == t.z }
            end
        } } }
        true
    end

    def constructions_done?
        @layout.each { |f|
            next if not f[:construction]
            return if not t = df.map_tile_at(x1+f[:x].to_i, y1+f[:y].to_i, z1+f[:z].to_i)
            # TODO check actual tile shape vs construction type
            return if t.shape_basic == :Open
        }
    end

    def to_s
        s = type.to_s
        s << " (#{subtype})" if subtype
        if owner and u = df.unit_find(owner)
            s << " (owned by #{DwarfAI::describe_unit(u)})"
        end
        if misc[:squad_id] and squad = df.world.squads.all.binsearch(misc[:squad_id])
            s << " (used by #{DwarfAI::capitalize_all(squad.name.to_s(true))})"
        end
        s << " (#{misc[:stockpile_level]})" if misc[:stockpile_level]
        s << " (#{misc[:workshop_level]})" if misc[:workshop_level]
        s << " (#{misc[:workshop]})" if misc[:workshop]
        s << " (#{misc[:users].length} users)" if misc[:users]
        s << " (#{status})"
        s
    end

    def serialize
        {
            :x1         => x1,
            :x2         => x2,
            :y1         => y1,
            :y2         => y2,
            :z1         => z1,
            :z2         => z2,
            :accesspath => accesspath.map{ |ap|
                if i = $dwarfAI.plan.instance_variable_get(:@rooms).index(ap) # XXX
                    [:room, i]
                elsif i = $dwarfAI.plan.instance_variable_get(:@corridors).index(ap) # XXX
                    [:corridor, i]
                else
                    raise "access path #{ap.inspect} not in @rooms or @corridors"
                end
            },
            :status     => status,
            :layout     => layout,
            :type       => type,
            :owner      => owner,
            :subtype    => subtype,
            :misc       => Hash[misc.map{ |k, v|
                if k == :workshop
                    [k, $dwarfAI.plan.instance_variable_get(:@rooms).index(v)] # XXX
                else
                    [k, v]
                end
            }],
        }
    end
end

class Room < Corridor
    def initialize(type, subtype, x1, x2, y1, y2, z)
        super(x1, x2, y1, y2, z, z)
        @type = type
        @subtype = subtype
    end

    def dfbuilding
        df.building_find(misc[:bld_id]) if misc[:bld_id]
    end
end
*/

// vim: et:sw=4:ts=4

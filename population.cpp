#include "ai.h"
#include "population.h"
#include "plan.h"
#include "stocks.h"

#include "modules/Gui.h"
#include "modules/Translation.h"
#include "modules/Units.h"

#include "df/abstract_building_inn_tavernst.h"
#include "df/abstract_building_libraryst.h"
#include "df/abstract_building_templest.h"
#include "df/building_civzonest.h"
#include "df/building_farmplotst.h"
#include "df/building_stockpilest.h"
#include "df/building_tradedepotst.h"
#include "df/building_workshopst.h"
#include "df/caste_raw.h"
#include "df/creature_raw.h"
#include "df/entity_position.h"
#include "df/entity_position_assignment.h"
#include "df/entity_position_raw.h"
#include "df/entity_raw.h"
#include "df/general_ref_building_civzone_assignedst.h"
#include "df/general_ref_building_holderst.h"
#include "df/general_ref_contains_itemst.h"
#include "df/general_ref_contains_unitst.h"
#include "df/general_ref_unit_workerst.h"
#include "df/histfig_entity_link_positionst.h"
#include "df/historical_entity.h"
#include "df/historical_figure.h"
#include "df/incident.h"
#include "df/itemdef_weaponst.h"
#include "df/job.h"
#include "df/manager_order.h"
#include "df/occupation.h"
#include "df/reaction.h"
#include "df/squad.h"
#include "df/squad_ammo_spec.h"
#include "df/squad_order_kill_listst.h"
#include "df/squad_order_trainst.h"
#include "df/squad_position.h"
#include "df/squad_schedule_order.h"
#include "df/squad_uniform_spec.h"
#include "df/ui.h"
#include "df/uniform_category.h"
#include "df/unit_misc_trait.h"
#include "df/unit_skill.h"
#include "df/unit_soul.h"
#include "df/unit_wound.h"
#include "df/viewscreen_locationsst.h"
#include "df/world.h"
#include "df/world_site.h"

REQUIRE_GLOBAL(cur_year);
REQUIRE_GLOBAL(cur_year_tick);
REQUIRE_GLOBAL(standing_orders_forbid_used_ammo);
REQUIRE_GLOBAL(ui);
REQUIRE_GLOBAL(world);

const static struct LaborInfo
{
    std::vector<df::unit_labor> list;
    std::set<df::unit_labor> tool;
    std::map<df::unit_labor, df::job_skill> skill;
    std::set<df::unit_labor> idle;
    std::set<df::unit_labor> medical;
    std::map<df::unit_labor, std::set<std::string>> stocks;
    std::set<df::unit_labor> hauling;
    std::map<df::unit_labor, int32_t> min, max, min_pct, max_pct;
    std::set<df::job_type> wont_work_job;

    LaborInfo()
    {
        FOR_ENUM_ITEMS(unit_labor, ul)
        {
            if (ul != unit_labor::NONE)
            {
                list.push_back(ul);
                min[ul] = 2;
                max[ul] = 8;
                min_pct[ul] = 0;
                max_pct[ul] = 40;
                if (ENUM_KEY_STR(unit_labor, ul).find("HAUL") != std::string::npos)
                {
                    hauling.insert(ul);
                }
            }
        }

        tool.insert(unit_labor::MINE);
        tool.insert(unit_labor::CUTWOOD);
        tool.insert(unit_labor::HUNT);

        FOR_ENUM_ITEMS(job_skill, js)
        {
            df::unit_labor ul = ENUM_ATTR(job_skill, labor, js);
            if (ul != unit_labor::NONE)
            {
                skill[ul] = js;
            }
        }

        idle.insert(unit_labor::PLANT);
        idle.insert(unit_labor::HERBALIST);
        idle.insert(unit_labor::FISH);
        idle.insert(unit_labor::DETAIL);

        medical.insert(unit_labor::DIAGNOSE);
        medical.insert(unit_labor::SURGERY);
        medical.insert(unit_labor::BONE_SETTING);
        medical.insert(unit_labor::SUTURING);
        medical.insert(unit_labor::DRESSING_WOUNDS);
        medical.insert(unit_labor::FEED_WATER_CIVILIANS);

        stocks[unit_labor::PLANT].insert("food");
        stocks[unit_labor::PLANT].insert("drink");
        stocks[unit_labor::PLANT].insert("cloth");
        stocks[unit_labor::HERBALIST].insert("food");
        stocks[unit_labor::HERBALIST].insert("drink");
        stocks[unit_labor::HERBALIST].insert("cloth");
        stocks[unit_labor::FISH].insert("food");

        hauling.insert(unit_labor::FEED_WATER_CIVILIANS);
        hauling.insert(unit_labor::RECOVER_WOUNDED);

        wont_work_job.insert(job_type::AttendParty);
        wont_work_job.insert(job_type::Rest);
        wont_work_job.insert(job_type::UpdateStockpileRecords);

        min[unit_labor::DETAIL] = 4;
        min[unit_labor::PLANT] = 4;
        min[unit_labor::HERBALIST] = 1;

        max[unit_labor::FISH] = 1;

        min_pct[unit_labor::DETAIL] = 5;
        min_pct[unit_labor::PLANT] = 30;
        min_pct[unit_labor::FISH] = 1;
        min_pct[unit_labor::HERBALIST] = 10;

        max_pct[unit_labor::DETAIL] = 60;
        max_pct[unit_labor::PLANT] = 80;
        max_pct[unit_labor::FISH] = 10;
        max_pct[unit_labor::HERBALIST] = 30;

        for (auto it = hauling.begin(); it != hauling.end(); it++)
        {
            min_pct[*it] = 30;
            max_pct[*it] = 100;
        }
    }
} labors;

Population::Population(AI *ai) :
    ai(ai),
    citizen(),
    military(),
    pet(),
    visitor(),
    resident(),
    update_counter(0),
    onupdate_handle(nullptr),
    medic(),
    workers(),
    seen_badwork()
{
}

Population::~Population()
{
}

command_result Population::startup(color_ostream &)
{
    *standing_orders_forbid_used_ammo = 0;
    return CR_OK;
}

command_result Population::onupdate_register(color_ostream &)
{
    onupdate_handle = events.onupdate_register("df-ai pop", 360, 10, [this](color_ostream & out) { update(out); });
    return CR_OK;
}

command_result Population::onupdate_unregister(color_ostream &)
{
    events.onupdate_unregister(onupdate_handle);
    return CR_OK;
}

void Population::update(color_ostream & out)
{
    update_counter++;
    switch (update_counter % 10)
    {
        case 1:
            update_citizenlist(out);
            break;
        case 2:
            update_nobles(out);
            break;
        case 3:
            update_jobs(out);
            break;
        case 4:
            update_military(out);
            break;
        case 5:
            update_pets(out);
            break;
        case 6:
            update_deads(out);
            break;
        case 7:
            update_caged(out);
            break;
        case 8:
            update_locations(out);
            break;
        case 9:
            if (ai->eventsJson.is_open())
            {
                Json::Value payload(Json::objectValue);
                payload["citizen"] = citizen.size();
                payload["military"] = military.size();
                payload["pet"] = pet.size();
                payload["visitor"] = visitor.size();
                payload["resident"] = resident.size();
                ai->event("population", payload);
            }
            break;
    }
}

void Population::new_citizen(color_ostream & out, int32_t id)
{
    citizen.insert(id);
    ai->plan->new_citizen(out, id);
}

void Population::del_citizen(color_ostream & out, int32_t id)
{
    citizen.erase(id);
    military.erase(id);
    ai->plan->del_citizen(out, id);
}

void Population::update_citizenlist(color_ostream & out)
{
    std::set<int32_t> old = citizen;

    visitor.clear();
    resident.clear();

    // add new fort citizen to our list
    for (auto it = world->units.active.begin(); it != world->units.active.end(); it++)
    {
        df::unit *u = *it;
        if (Units::isCitizen(u) && !Units::isBaby(u))
        {
            if (old.count(u->id))
            {
                old.erase(u->id);
            }
            else
            {
                new_citizen(out, u->id);

                if (ai->eventsJson.is_open())
                {
                    Json::Value payload(Json::objectValue);
                    payload["id"] = u->id;
                    payload["name"] = DF2UTF(AI::describe_name(u->name, false));
                    payload["name_english"] = DF2UTF(AI::describe_name(u->name, true));
                    payload["birth_year"] = u->relations.birth_year;
                    payload["birth_time"] = u->relations.birth_time;
                    if (df::creature_raw *race = df::creature_raw::find(u->race))
                    {
                        payload["race"] = race->creature_id;
                        payload["caste"] = race->caste[u->caste]->caste_id;
                    }
                    payload["sex"] = u->sex == 0 ? "female" : u->sex == 1 ? "male" : "unknown";
                    ai->event("new citizen", payload);
                }
            }
        }
        else if (u->flags1.bits.dead || u->flags1.bits.merchant || u->flags1.bits.forest || u->flags2.bits.slaughter)
        {
            // ignore
        }
        else if (u->flags2.bits.visitor)
        {
            visitor.insert(u->id);
        }
        else if (Units::isOwnCiv(u) && !Units::isOwnGroup(u) && u->cultural_identity != -1)
        {
            resident.insert(u->id);
        }
    }

    // del those who are no longer here
    for (auto it = old.begin(); it != old.end(); it++)
    {
        // u.counters.death_tg.flags.discovered dead/missing
        del_citizen(out, *it);

        if (ai->eventsJson.is_open())
        {
            Json::Value payload(Json::objectValue);
            payload["id"] = *it;
            if (df::unit *u = df::unit::find(*it))
            {
                payload["name"] = DF2UTF(AI::describe_name(u->name, false));
                payload["name_english"] = DF2UTF(AI::describe_name(u->name, true));
                payload["birth_year"] = u->relations.birth_year;
                payload["birth_time"] = u->relations.birth_time;
                if (df::incident *i = df::incident::find(u->counters.death_id))
                {
                    payload["death_year"] = i->event_year;
                    payload["death_time"] = i->event_time;
                    payload["death_cause"] = ENUM_KEY_STR(death_type, i->death_cause);
                }
                if (df::creature_raw *race = df::creature_raw::find(u->race))
                {
                    payload["race"] = race->creature_id;
                    payload["caste"] = race->caste[u->caste]->caste_id;
                }
                payload["sex"] = u->sex == 0 ? "female" : u->sex == 1 ? "male" : "unknown";
            }
            ai->event("del citizen", payload);
        }
    }
}

void Population::update_jobs(color_ostream &)
{
    for (auto j = world->job_list.next; j; j = j->next)
    {
        if (j->item->flags.bits.suspend && !j->item->flags.bits.repeat)
        {
            j->item->flags.bits.suspend = 0;
        }
    }
}

void Population::update_deads(color_ostream & out)
{
    for (auto it = world->units.all.begin(); it != world->units.all.end(); it++)
    {
        df::unit *u = *it;
        if (u->flags3.bits.ghostly)
        {
            ai->stocks->queue_slab(out, u->hist_figure_id);
        }
    }
}

void Population::update_caged(color_ostream & out)
{
    int32_t count = 0;
    for (auto it = world->items.other[items_other_id::CAGE].begin(); it != world->items.other[items_other_id::CAGE].end(); it++)
    {
        df::item *cage = *it;
        if (!cage->flags.bits.on_ground)
        {
            continue;
        }
        for (auto ref = cage->general_refs.begin(); ref != cage->general_refs.end(); ref++)
        {
            if (virtual_cast<df::general_ref_contains_itemst>(*ref))
            {
                df::item *i = (*ref)->getItem();
                if (i->flags.bits.dump)
                {
                    continue;
                }
                count++;
                i->flags.bits.dump = 1;
            }
            else if (virtual_cast<df::general_ref_contains_unitst>(*ref))
            {
                df::unit *u = (*ref)->getUnit();
                if (Units::isOwnCiv(u))
                {
                    // TODO rescue caged dwarves
                }
                else
                {
                    for (auto ii = u->inventory.begin(); ii != u->inventory.end(); ii++)
                    {
                        if ((*ii)->item->flags.bits.dump)
                        {
                            continue;
                        }
                        count++;
                        (*ii)->item->flags.bits.dump = 1;
                    }

                    if (u->inventory.empty())
                    {
                        room *r = ai->plan->find_room(room_type::pitcage, [](room *r) -> bool { return r->dfbuilding(); });
                        if (r && ai->plan->spiral_search(r->pos(), 1, 1, [cage](df::coord t) -> bool { return t == cage->pos; }).isValid())
                        {
                            assign_unit_to_zone(u, virtual_cast<df::building_civzonest>(r->dfbuilding()));
                            ai->debug(out, "pop: marked " + AI::describe_unit(u) + " for pitting");
                            military_random_squad_attack_unit(out, u);
                        }
                    }
                }
            }
        }
    }
    if (count > 0)
    {
        ai->debug(out, stl_sprintf("pop: dumped %d items from cages", count));
    }
}

void Population::update_military(color_ostream & out)
{
    // check for new soldiers, allocate barracks
    std::vector<int32_t> newsoldiers;

    for (auto it = world->units.active.begin(); it != world->units.active.end(); it++)
    {
        df::unit *u = *it;
        if (Units::isCitizen(u))
        {
            if (u->military.squad_id == -1)
            {
                if (military.erase(u->id))
                {
                    ai->plan->freesoldierbarrack(out, u->id);
                }
            }
            else
            {
                if (!military.count(u->id))
                {
                    military[u->id] = u->military.squad_id;
                    newsoldiers.push_back(u->id);
                }
            }
        }
    }

    // enlist new soldiers if needed
    std::vector<df::unit *> maydraft;
    for (auto it = world->units.active.begin(); it != world->units.active.end(); it++)
    {
        df::unit *u = *it;
        if (Units::isCitizen(u) && !Units::isChild(u) && !Units::isBaby(u) && u->mood == mood_type::None)
        {
            bool hasTool = false;
            for (auto it = labors.tool.begin(); it != labors.tool.end(); it++)
            {
                if (u->status.labors[*it])
                {
                    hasTool = true;
                    break;
                }
            }
            if (hasTool)
            {
                continue;
            }
            maydraft.push_back(u);
        }
    }
    while (military.size() < maydraft.size() / 5)
    {
        df::unit *ns = military_find_new_soldier(out, maydraft);
        if (!ns)
        {
            break;
        }
        military[ns->id] = ns->military.squad_id;
        newsoldiers.push_back(ns->id);
    }

    for (auto it = newsoldiers.begin(); it != newsoldiers.end(); it++)
    {
        ai->plan->getsoldierbarrack(out, *it);
    }
}

// with a population of 200:
const static int32_t wanted_tavern_keeper = 4;
const static int32_t wanted_tavern_keeper_min = 1;
const static int32_t wanted_tavern_performer = 8;
const static int32_t wanted_tavern_performer_min = 0;
const static int32_t wanted_library_scholar = 16;
const static int32_t wanted_library_scholar_min = 0;
const static int32_t wanted_library_scribe = 2;
const static int32_t wanted_library_scribe_min = 0;
const static int32_t wanted_temple_performer = 4;
const static int32_t wanted_temple_performer_min = 0;

void Population::update_locations(color_ostream & out)
{
    // not urgent, wait for next cycle.
    if (!AI::is_dwarfmode_viewscreen())
        return;

    // accept all petitions
    while (!ui->petitions.empty())
    {
        AI::feed_key(interface_key::D_PETITIONS);
        AI::feed_key(interface_key::OPTION1);
        AI::feed_key(interface_key::LEAVESCREEN);
    }

#define INIT_NEED(name) int32_t need_##name = std::max(wanted_##name * int32_t(citizen.size()) / 200, wanted_##name##_min)
    INIT_NEED(tavern_keeper);
    INIT_NEED(tavern_performer);
    INIT_NEED(library_scholar);
    INIT_NEED(library_scribe);
    INIT_NEED(temple_performer);
#undef INIT_NEED

    if (room *tavern = ai->plan->find_room(room_type::location, [](room *r) -> bool { return r->subtype == "tavern" && r->dfbuilding(); }))
    {
        df::building *bld = tavern->dfbuilding();
        if (auto loc = virtual_cast<df::abstract_building_inn_tavernst>(binsearch_in_vector(df::world_site::find(bld->site_id)->buildings, bld->location_id)))
        {
            for (auto it = loc->occupations.begin(); it != loc->occupations.end(); it++)
            {
                if ((*it)->unit_id != -1)
                {
                    if ((*it)->type == occupation_type::TAVERN_KEEPER)
                    {
                        need_tavern_keeper--;
                    }
                    else if ((*it)->type == occupation_type::PERFORMER)
                    {
                        need_tavern_performer--;
                    }
                }
            }
            if (need_tavern_keeper > 0)
            {
                assign_occupation(out, bld, loc, occupation_type::TAVERN_KEEPER);
            }
            if (need_tavern_performer > 0)
            {
                assign_occupation(out, bld, loc, occupation_type::PERFORMER);
            }
        }
    }

    if (room *library = ai->plan->find_room(room_type::location, [](room *r) -> bool { return r->subtype == "library" && r->dfbuilding(); }))
    {
        df::building *bld = library->dfbuilding();
        if (auto loc = virtual_cast<df::abstract_building_libraryst>(binsearch_in_vector(df::world_site::find(bld->site_id)->buildings, bld->location_id)))
        {
            for (auto it = loc->occupations.begin(); it != loc->occupations.end(); it++)
            {
                if ((*it)->unit_id != -1)
                {
                    if ((*it)->type == occupation_type::SCHOLAR)
                    {
                        need_library_scholar--;
                    }
                    else if ((*it)->type == occupation_type::SCRIBE)
                    {
                        need_library_scribe--;
                    }
                }
            }
            if (need_library_scholar > 0)
            {
                assign_occupation(out, bld, loc, occupation_type::SCHOLAR);
            }
            if (need_library_scribe > 0)
            {
                assign_occupation(out, bld, loc, occupation_type::SCRIBE);
            }
        }
    }

    if (room *temple = ai->plan->find_room(room_type::location, [](room *r) -> bool { return r->subtype == "temple" && r->dfbuilding(); }))
    {
        df::building *bld = temple->dfbuilding();
        if (auto loc = virtual_cast<df::abstract_building_templest>(binsearch_in_vector(df::world_site::find(bld->site_id)->buildings, bld->location_id)))
        {
            for (auto it = loc->occupations.begin(); it != loc->occupations.end(); it++)
            {
                if ((*it)->unit_id != -1)
                {
                    if ((*it)->type == occupation_type::PERFORMER)
                    {
                        need_temple_performer--;
                    }
                }
            }
            if (need_temple_performer > 0)
            {
                assign_occupation(out, bld, loc, occupation_type::PERFORMER);
            }
        }
    }
}

void Population::assign_occupation(color_ostream & out, df::building *, df::abstract_building *loc, df::occupation_type occ)
{
    AI::feed_key(interface_key::D_LOCATIONS);

    auto view = strict_virtual_cast<df::viewscreen_locationsst>(Gui::getCurViewscreen(true));
    if (!view)
    {
        ai->debug(out, "[ERROR] expected viewscreen_locationsst");
        return;
    }

    while (view->locations[view->location_idx] != loc)
    {
        AI::feed_key(interface_key::STANDARDSCROLL_DOWN);
    }

    AI::feed_key(interface_key::STANDARDSCROLL_RIGHT);

    while (true)
    {
        if (view->occupations[view->occupation_idx]->unit_id == -1 &&
                view->occupations[view->occupation_idx]->type == occ)
        {
            break;
        }
        AI::feed_key(interface_key::STANDARDSCROLL_DOWN);
    }

    AI::feed_key(interface_key::SELECT);

    df::unit *chosen = nullptr;
    int32_t best = std::numeric_limits<int32_t>::max();
    for (auto it = view->units.begin(); it != view->units.end(); it++)
    {
        df::unit *u = *it;

        if (!u || u->military.squad_id != -1)
        {
            continue;
        }

        std::vector<Units::NoblePosition> positions;
        Units::getNoblePositions(&positions, u);
        if (!positions.empty())
        {
            continue;
        }

        bool has_occupation = false;
        for (auto o = world->occupations.all.begin(); o != world->occupations.all.end(); o++)
        {
            if ((*o)->unit_id == u->id)
            {
                has_occupation = true;
                break;
            }
        }

        if (has_occupation)
        {
            continue;
        }

        int32_t score = unit_totalxp(u);
        if (!chosen || score < best)
        {
            chosen = u;
            best = score;
        }
    }

    if (!chosen)
    {
        ai->debug(out, "pop: could not find unit for occupation " + ENUM_KEY_STR(occupation_type, occ) + " at " + AI::describe_name(*loc->getName(), true));

        AI::feed_key(interface_key::LEAVESCREEN);

        AI::feed_key(interface_key::LEAVESCREEN);

        return;
    }

    ai->debug(out, "pop: assigning occupation " + ENUM_KEY_STR(occupation_type, occ) + " at " + AI::describe_name(*loc->getName(), true) + " to " + AI::describe_unit(chosen));

    while (true)
    {
        if (view->units[view->unit_idx] == chosen)
        {
            break;
        }
        AI::feed_key(interface_key::STANDARDSCROLL_DOWN);
    }

    AI::feed_key(interface_key::SELECT);

    AI::feed_key(interface_key::LEAVESCREEN);
}

void Population::military_random_squad_attack_unit(color_ostream & out, df::unit *u)
{
    df::squad *squad = nullptr;
    int32_t best = std::numeric_limits<int32_t>::min();
    for (auto sqid = ui->main.fortress_entity->squads.begin(); sqid != ui->main.fortress_entity->squads.end(); sqid++)
    {
        df::squad *sq = df::squad::find(*sqid);

        int32_t score = 0;
        for (auto sp = sq->positions.begin(); sp != sq->positions.end(); sp++)
        {
            if ((*sp)->occupant != -1)
            {
                score++;
            }
        }
        score -= sq->orders.size();

        if (!squad || best < score)
        {
            squad = sq;
            best = score;
        }
    }
    if (!squad)
    {
        return;
    }

    military_squad_attack_unit(out, squad, u);
}

bool Population::military_all_squads_attack_unit(color_ostream & out, df::unit *u)
{
    bool any = false;
    for (auto sqid = ui->main.fortress_entity->squads.begin(); sqid != ui->main.fortress_entity->squads.end(); sqid++)
    {
        if (military_squad_attack_unit(out, df::squad::find(*sqid), u))
            any = true;
    }
    return any;
}

bool Population::military_squad_attack_unit(color_ostream & out, df::squad *squad, df::unit *u)
{
    for (auto it = squad->orders.begin(); it != squad->orders.end(); it++)
    {
        if (auto so = strict_virtual_cast<df::squad_order_kill_listst>(*it))
        {
            if (std::find(so->units.begin(), so->units.end(), u->id) != so->units.end())
            {
                return false;
            }
        }
    }

    df::squad_order_kill_listst *so = df::allocate<df::squad_order_kill_listst>();
    so->units.push_back(u->id);
    so->title = AI::describe_unit(u);
    squad->orders.push_back(so);
    ai->debug(out, "sending " + AI::describe_name(squad->name, true) + " to attack " + AI::describe_unit(u));
    return true;
}

std::string Population::military_find_commander_pos()
{
    for (auto it = ui->main.fortress_entity->entity_raw->positions.begin(); it != ui->main.fortress_entity->entity_raw->positions.end(); it++)
    {
        if ((*it)->responsibilities[entity_position_responsibility::MILITARY_STRATEGY] && (*it)->flags.is_set(entity_position_raw_flags::SITE))
        {
            return (*it)->code;
        }
    }
    return "";
}

std::string Population::military_find_captain_pos()
{
    for (auto it = ui->main.fortress_entity->entity_raw->positions.begin(); it != ui->main.fortress_entity->entity_raw->positions.end(); it++)
    {
        if ((*it)->flags.is_set(entity_position_raw_flags::MILITARY_SCREEN_ONLY) && (*it)->flags.is_set(entity_position_raw_flags::SITE))
        {
            return (*it)->code;
        }
    }
    return "";
}

// returns an unit newly assigned to a military squad
df::unit *Population::military_find_new_soldier(color_ostream & out, const std::vector<df::unit *> & unitlist)
{
    df::unit *ns = nullptr;
    int32_t best = std::numeric_limits<int32_t>::max();
    for (auto it = unitlist.begin(); it != unitlist.end(); it++)
    {
        df::unit *u = *it;
        if (u->military.squad_id == -1)
        {
            std::vector<Units::NoblePosition> positions;
            Units::getNoblePositions(&positions, u);
            int32_t score = unit_totalxp(u) + 5000 * positions.size();
            if (!ns || score < best)
            {
                ns = u;
                best = score;
            }
        }
    }
    if (!ns)
    {
        return nullptr;
    }

    int32_t squad_id = military_find_free_squad();
    df::squad *squad = df::squad::find(squad_id);
    auto pos = std::find_if(squad->positions.begin(), squad->positions.end(), [](df::squad_position *p) -> bool { return p->occupant == -1; });
    if (pos == squad->positions.end())
    {
        return nullptr;
    }

    (*pos)->occupant = ns->hist_figure_id;
    ns->military.squad_id = squad_id;
    ns->military.squad_position = pos - squad->positions.begin();

    for (auto it = ui->main.fortress_entity->positions.assignments.begin(); it != ui->main.fortress_entity->positions.assignments.end(); it++)
    {
        if ((*it)->squad_id == squad_id)
        {
            return ns;
        }
    }

    if (ui->main.fortress_entity->assignments_by_type[entity_position_responsibility::MILITARY_STRATEGY].empty())
    {
        assign_new_noble(out, military_find_commander_pos(), ns)->squad_id = squad_id;
    }
    else
    {
        assign_new_noble(out, military_find_captain_pos(), ns)->squad_id = squad_id;
    }

    return ns;
}

// return a squad index with an empty slot
int32_t Population::military_find_free_squad()
{
    int32_t squad_sz = 8;
    if (military.size() < 4 * 6)
        squad_sz = 6;
    if (military.size() < 3 * 4)
        squad_sz = 4;

    for (auto sqid = ui->main.fortress_entity->squads.begin(); sqid != ui->main.fortress_entity->squads.end(); sqid++)
    {
        int32_t count = 0;
        for (auto it = military.begin(); it != military.end(); it++)
        {
            if (it->second == *sqid)
            {
                count++;
            }
        }
        if (count < squad_sz)
        {
            return *sqid;
        }
    }

    // create a new squad using the UI
    AI::feed_key(interface_key::D_MILITARY);
    AI::feed_key(interface_key::D_MILITARY_CREATE_SQUAD);
    AI::feed_key(interface_key::STANDARDSCROLL_UP);
    AI::feed_key(interface_key::SELECT);
    AI::feed_key(interface_key::LEAVESCREEN);

    // get the squad and its id
    int32_t squad_id = ui->main.fortress_entity->squads.back();
    df::squad *squad = df::squad::find(squad_id);

    squad->cur_alert_idx = 1; // train
    squad->uniform_priority = 2;
    squad->carry_food = 2;
    squad->carry_water = 2;

    const static struct uniform_category_item_type
    {
        std::vector<std::pair<df::uniform_category, df::item_type>> vec;
        uniform_category_item_type()
        {
            vec.push_back(std::make_pair(uniform_category::body, item_type::ARMOR));
            vec.push_back(std::make_pair(uniform_category::head, item_type::HELM));
            vec.push_back(std::make_pair(uniform_category::pants, item_type::PANTS));
            vec.push_back(std::make_pair(uniform_category::gloves, item_type::GLOVES));
            vec.push_back(std::make_pair(uniform_category::shoes, item_type::SHOES));
            vec.push_back(std::make_pair(uniform_category::shield, item_type::SHIELD));
            vec.push_back(std::make_pair(uniform_category::weapon, item_type::WEAPON));
        }
    } item_type;
    // uniform
    for (auto pos = squad->positions.begin(); pos != squad->positions.end(); pos++)
    {
        for (auto it = item_type.vec.begin(); it != item_type.vec.end(); it++)
        {
            if ((*pos)->uniform[it->first].empty())
            {
                df::squad_uniform_spec *sus = df::allocate<df::squad_uniform_spec>();
                sus->color = -1;
                sus->item_filter.item_type = it->second;
                sus->item_filter.material_class = it->first == uniform_category::weapon ? entity_material_category::None : entity_material_category::Armor;
                sus->item_filter.mattype = -1;
                sus->item_filter.matindex = -1;
                (*pos)->uniform[it->first].push_back(sus);
            }
        }
        (*pos)->flags.bits.exact_matches = 1;
    }

    if (ui->main.fortress_entity->squads.size() % 3 == 0)
    {
        // ranged squad
        for (auto pos = squad->positions.begin(); pos != squad->positions.end(); pos++)
        {
            (*pos)->uniform[uniform_category::weapon][0]->indiv_choice.bits.ranged = 1;
        }
        df::squad_ammo_spec *sas = df::allocate<df::squad_ammo_spec>();
        sas->item_filter.item_type = item_type::AMMO;
        sas->item_filter.item_subtype = 0; // XXX bolts
        sas->item_filter.material_class = entity_material_category::None;
        sas->amount = 500;
        sas->flags.bits.use_combat = 1;
        sas->flags.bits.use_training = 1;
        squad->ammunition.push_back(sas);
    }
    else
    {
        // we don't want all the axes being used up by the military.
        std::vector<int32_t> weapons;
        for (auto it = ui->main.fortress_entity->entity_raw->equipment.weapon_id.begin(); it != ui->main.fortress_entity->entity_raw->equipment.weapon_id.end(); it++)
        {
            df::itemdef_weaponst *idef = df::itemdef_weaponst::find(*it);
            if (idef->skill_melee != job_skill::MINING && idef->skill_melee != job_skill::AXE && idef->skill_ranged == job_skill::NONE && !idef->flags.is_set(weapon_flags::TRAINING))
            {
                weapons.push_back(*it);
            }
        }
        if (weapons.empty())
        {
            for (auto pos = squad->positions.begin(); pos != squad->positions.end(); pos++)
            {
                (*pos)->uniform[uniform_category::weapon][0]->indiv_choice.bits.melee = 1;
            }
        }
        else
        {
            int32_t n = ui->main.fortress_entity->squads.size();
            n -= n / 3 + 1;
            n *= 10;
            for (auto pos = squad->positions.begin(); pos != squad->positions.end(); pos++)
            {
                (*pos)->uniform[uniform_category::weapon][0]->item_filter.item_subtype = weapons[n % weapons.size()];
                n++;
            }
        }
    }

    return squad_id;
}

void Population::set_up_trading(bool should_be_trading)
{
    room *r = ai->plan->find_room(room_type::workshop, [](room *r) -> bool { return r->subtype == "TradeDepot"; });
    if (!r)
    {
        return;
    }
    df::building_tradedepotst *bld = virtual_cast<df::building_tradedepotst>(r->dfbuilding());
    if (!bld || bld->getBuildStage() < bld->getMaxBuildStage())
    {
        return;
    }
    if (bld->trade_flags.bits.trader_requested == should_be_trading)
    {
        return;
    }

    if (AI::is_dwarfmode_viewscreen())
    {
        AI::feed_key(interface_key::D_BUILDJOB);

        df::coord pos = r->pos();
        Gui::revealInDwarfmodeMap(pos, true);
        Gui::setCursorCoords(pos.x, pos.y, pos.z);

        AI::feed_key(interface_key::CURSOR_LEFT);
        AI::feed_key(interface_key::BUILDJOB_DEPOT_REQUEST_TRADER);
        AI::feed_key(interface_key::LEAVESCREEN);
    }
}

bool Population::unit_hasmilitaryduty(df::unit *u)
{
    if (u->military.squad_id == -1)
    {
        return false;
    }
    df::squad *squad = df::squad::find(u->military.squad_id);
    std::vector<df::squad_schedule_order *> & curmonth = squad->schedule[squad->cur_alert_idx][*cur_year_tick / 28 / 1200]->orders;
    return !curmonth.empty() && (curmonth.size() != 1 || curmonth[0]->min_count != 0);
}

int32_t Population::unit_totalxp(df::unit *u)
{
    int32_t t = 0;
    for (auto sk = u->status.current_soul->skills.begin(); sk != u->status.current_soul->skills.end(); sk++)
    {
        int32_t rat = (*sk)->rating;
        t += 400 * rat + 100 * rat * (rat + 1) / 2 + (*sk)->experience;
    }
    return t;
}

std::string Population::positionCode(df::entity_position_responsibility responsibility)
{
    for (auto it = ui->main.fortress_entity->entity_raw->positions.begin(); it != ui->main.fortress_entity->entity_raw->positions.end(); it++)
    {
        if ((*it)->responsibilities[responsibility])
        {
            return (*it)->code;
        }
    }
    return "";
}

void Population::update_nobles(color_ostream & out)
{
    std::vector<df::unit *> cz;
    for (auto it = world->units.active.begin(); it != world->units.active.end(); it++)
    {
        df::unit *u = *it;
        std::vector<Units::NoblePosition> positions;
        if (Units::isCitizen(u) && !Units::isBaby(u) && !Units::isChild(u) && u->military.squad_id == -1 && !Units::getNoblePositions(&positions, u))
        {
            cz.push_back(u);
        }
    }
    std::sort(cz.begin(), cz.end(), [this](df::unit *a, df::unit *b) -> bool
            {
                return unit_totalxp(a) > unit_totalxp(b);
            });
    df::historical_entity *ent = ui->main.fortress_entity;

    if (ent->assignments_by_type[entity_position_responsibility::MANAGE_PRODUCTION].empty() && !cz.empty())
    {
        ai->debug(out, "assigning new manager: " + AI::describe_unit(cz.back()));
        // TODO do check population caps, ...
        assign_new_noble(out, positionCode(entity_position_responsibility::MANAGE_PRODUCTION), cz.back());
        cz.pop_back();
    }

    if (ent->assignments_by_type[entity_position_responsibility::ACCOUNTING].empty() && !cz.empty())
    {
        for (auto it = cz.rbegin(); it != cz.rend(); it++)
        {
            if (!(*it)->status.labors[unit_labor::MINE])
            {
                ai->debug(out, "assigning new bookkeeper: " + AI::describe_unit(*it));
                assign_new_noble(out, positionCode(entity_position_responsibility::ACCOUNTING), *it);
                ui->bookkeeper_settings = 4;
                cz.erase(it.base() - 1);
                break;
            }
        }
    }

    if (ent->assignments_by_type[entity_position_responsibility::HEALTH_MANAGEMENT].empty() && ai->plan->find_room(room_type::infirmary, [](room *r) -> bool { return r->status != room_status::plan; }) && !cz.empty())
    {
        ai->debug(out, "assigning new chief medical dwarf: " + AI::describe_unit(cz.back()));
        assign_new_noble(out, positionCode(entity_position_responsibility::HEALTH_MANAGEMENT), cz.back());
        cz.pop_back();
    }

    if (!ent->assignments_by_type[entity_position_responsibility::HEALTH_MANAGEMENT].empty())
    {
        df::entity_position_assignment *asn = ent->assignments_by_type[entity_position_responsibility::HEALTH_MANAGEMENT].at(0);
        df::historical_figure *hf = df::historical_figure::find(asn->histfig);
        df::unit *doctor = df::unit::find(hf->unit_id);
        // doc => healthcare
        for (auto it = labors.medical.begin(); it != labors.medical.end(); it++)
        {
            doctor->status.labors[*it] = true;
        }
    }

    if (ent->assignments_by_type[entity_position_responsibility::TRADE].empty() && !cz.empty())
    {
        ai->debug(out, "assigning new broker: " + AI::describe_unit(cz.back()));
        assign_new_noble(out, positionCode(entity_position_responsibility::TRADE), cz.back());
        cz.pop_back();
    }

    check_noble_appartments(out);
}

void Population::check_noble_appartments(color_ostream & out)
{
    std::set<int32_t> noble_ids;

    for (auto asn = ui->main.fortress_entity->positions.assignments.begin(); asn != ui->main.fortress_entity->positions.assignments.end(); asn++)
    {
        df::entity_position *pos = binsearch_in_vector(ui->main.fortress_entity->positions.own, (*asn)->position_id);
        if (pos->required_office > 0 || pos->required_dining > 0 || pos->required_tomb > 0)
        {
            if (df::historical_figure *hf = df::historical_figure::find((*asn)->histfig))
            {
                noble_ids.insert(hf->unit_id);
            }
        }
    }

    ai->plan->attribute_noblerooms(out, noble_ids);
}

df::entity_position_assignment *Population::assign_new_noble(color_ostream &, std::string pos_code, df::unit *unit)
{
    df::historical_entity *ent = ui->main.fortress_entity;

    df::entity_position *pos = nullptr;
    for (auto p = ent->positions.own.begin(); p != ent->positions.own.end(); p++)
    {
        if ((*p)->code == pos_code)
        {
            pos = *p;
            break;
        }
    }

    df::entity_position_assignment *assign = nullptr;
    for (auto a = ent->positions.assignments.begin(); a != ent->positions.assignments.end(); a++)
    {
        if ((*a)->position_id == pos->id && (*a)->histfig == -1)
        {
            assign = *a;
            break;
        }
    }
    if (!assign)
    {
        int32_t a_id = ent->positions.next_assignment_id;
        ent->positions.next_assignment_id++;
        assign = df::allocate<df::entity_position_assignment>();
        assign->id = a_id;
        assign->position_id = pos->id;
        assign->flags.resize(ent->positions.assignments[0]->flags.size); // XXX
        assign->flags.set(0, true); // XXX
        ent->positions.assignments.push_back(assign);
    }

    df::histfig_entity_link_positionst *poslink = df::allocate<df::histfig_entity_link_positionst>();
    poslink->link_strength = 100;
    poslink->start_year = *cur_year;
    poslink->entity_id = ent->id;
    poslink->assignment_id = assign->id;

    df::historical_figure::find(unit->hist_figure_id)->entity_links.push_back(poslink);
    assign->histfig = unit->hist_figure_id;

    FOR_ENUM_ITEMS(entity_position_responsibility, r)
    {
        if (pos->responsibilities[r])
        {
            ent->assignments_by_type[r].push_back(assign);
        }
    }

    return assign;
}

void Population::update_pets(color_ostream & out)
{
    int32_t needmilk = 0;
    int32_t needshear = 0;
    for (auto mo = world->manager_orders.begin(); mo != world->manager_orders.end(); mo++)
    {
        if ((*mo)->job_type == job_type::MilkCreature)
        {
            needmilk -= (*mo)->amount_left;
        }
        else if ((*mo)->job_type == job_type::ShearCreature)
        {
            needshear -= (*mo)->amount_left;
        }
    }

    std::map<df::caste_raw *, std::set<std::pair<int32_t, df::unit *>>> forSlaughter;

    std::map<int32_t, pet_flags> np = pet;
    for (auto it = world->units.active.begin(); it != world->units.active.end(); it++)
    {
        df::unit *u = *it;
        if (!Units::isOwnCiv(u) || Units::isOwnGroup(u) || Units::isOwnRace(u) || u->cultural_identity != -1)
        {
            continue;
        }
        if (u->flags1.bits.dead || u->flags1.bits.merchant || u->flags1.bits.forest || u->flags2.bits.visitor || u->flags2.bits.slaughter)
        {
            continue;
        }

        df::creature_raw *race = df::creature_raw::find(u->race);
        df::caste_raw *cst = race->caste[u->caste];

        int32_t age = (*cur_year - u->relations.birth_year) * 12 * 28 + (*cur_year_tick - u->relations.birth_time) / 1200; // days

        if (pet.count(u->id))
        {

            if (!cst->flags.is_set(caste_raw_flags::CAN_LEARN) && // morally acceptable to slaughter
                    cst->body_size_2.back() <= age && // full grown
                    u->profession != profession::TRAINED_HUNTER && // not trained
                    u->profession != profession::TRAINED_WAR && // not trained
                    u->relations.pet_owner_id == -1) // not owned
            {

                if (std::find_if(u->body.wounds.begin(), u->body.wounds.end(), [](df::unit_wound *w) -> bool { return std::find_if(w->parts.begin(), w->parts.end(), [](df::unit_wound::T_parts *p) -> bool { return p->flags2.bits.gelded; }) != w->parts.end(); }) != u->body.wounds.end() || cst->gender == -1)
                {
                    // animal can't reproduce, can't work, and will provide maximum butchering reward. kill it.
                    u->flags2.bits.slaughter = true;
                    ai->debug(out, stl_sprintf("marked %dy%dd old %s:%s for slaughter (can't reproduce)", age / 12 / 28, age % (12 * 28), race->creature_id.c_str(), cst->caste_id.c_str()));
                    continue;
                }

                forSlaughter[cst].insert(std::make_pair(age, u));
            }

            if (pet.at(u->id).bits.milkable && !Units::isBaby(u) && !Units::isChild(u))
            {
                bool have = false;
                for (auto mt = u->status.misc_traits.begin(); mt != u->status.misc_traits.end(); mt++)
                {
                    if ((*mt)->id == misc_trait_type::MilkCounter)
                    {
                        have = true;
                        break;
                    }
                }
                if (!have)
                {
                    needmilk++;
                }
            }

            if (pet.at(u->id).bits.shearable && !Units::isBaby(u) && !Units::isChild(u))
            {
                bool found = false;
                for (auto stl = cst->shearable_tissue_layer.begin(); stl != cst->shearable_tissue_layer.end(); stl++)
                {
                    for (auto bpi = (*stl)->bp_modifiers_idx.begin(); bpi != (*stl)->bp_modifiers_idx.end(); bpi++)
                    {
                        if (u->appearance.bp_modifiers[*bpi] >= (*stl)->length)
                        {
                            needshear++;
                            found = true;
                            break;
                        }
                    }
                    if (found)
                        break;
                }
            }

            np.erase(u->id);
            continue;
        }

        pet_flags flags;

        if (cst->flags.is_set(caste_raw_flags::MILKABLE))
        {
            flags.bits.milkable = 1;
        }

        if (!cst->shearable_tissue_layer.empty())
        {
            flags.bits.shearable = 1;
        }

        if (cst->flags.is_set(caste_raw_flags::HUNTS_VERMIN))
        {
            flags.bits.hunts_vermin = 1;
        }

        if (cst->flags.is_set(caste_raw_flags::GRAZER))
        {
            flags.bits.grazer = 1;

            if (df::building_civzonest *bld = virtual_cast<df::building_civzonest>(ai->plan->getpasture(out, u->id)))
            {
                assign_unit_to_zone(u, bld);
                // TODO monitor grass levels
            }
            else if (u->relations.pet_owner_id == -1 && !cst->flags.is_set(caste_raw_flags::CAN_LEARN))
            {
                // TODO slaughter best candidate, keep this one
                u->flags2.bits.slaughter = 1;
                ai->debug(out, stl_sprintf("marked %dy%dd old %s:%s for slaughter (no pasture)", age / 12 / 28, age % (12 * 28), race->creature_id.c_str(), cst->caste_id.c_str()));
                continue;
            }
        }

        pet[u->id] = flags;
    }

    for (auto p = np.begin(); p != np.end(); p++)
    {
        ai->plan->freepasture(out, p->first);
        pet.erase(p->first);
    }

    for (auto cst = forSlaughter.begin(); cst != forSlaughter.end(); cst++)
    {
        // we have reproductively viable animals, but there are more than 5 of
        // this sex (full-grown). kill the oldest ones for meat/leather/bones.

        if (cst->second.size() > 5)
        {
            // remove the youngest 5
            auto it = cst->second.begin();
            std::advance(it, 5);
            cst->second.erase(cst->second.begin(), it);

            for (auto it = cst->second.begin(); it != cst->second.end(); it++)
            {
                int32_t age = it->first;
                df::unit *u = it->second;
                df::creature_raw *race = df::creature_raw::find(u->race);
                u->flags2.bits.slaughter = 1;
                ai->debug(out, stl_sprintf("marked %dy%dd old %s:%s for slaughter (too many adults)", age / 12 / 28, age % (12 * 28), race->creature_id.c_str(), cst->first->caste_id.c_str()));
            }
        }
    }

    if (needmilk > 30)
        needmilk = 30;
    if (needmilk > 0)
        ai->stocks->add_manager_order(out, "MilkCreature", needmilk);

    if (needshear > 30)
        needshear = 30;
    if (needshear > 0)
        ai->stocks->add_manager_order(out, "ShearCreature", needshear);
}

void Population::assign_unit_to_zone(df::unit *u, df::building_civzonest *bld)
{
    // remove existing zone assignments
    // TODO remove existing chains/cages ?
    u->general_refs.erase(std::remove_if(u->general_refs.begin(), u->general_refs.end(), [u](df::general_ref *ref) -> bool
            {
                if (!virtual_cast<df::general_ref_building_civzone_assignedst>(ref))
                {
                    return false;
                }
                df::building_civzonest *bld = virtual_cast<df::building_civzonest>(ref->getBuilding());
                bld->assigned_units.erase(std::remove(bld->assigned_units.begin(), bld->assigned_units.end(), u->id), bld->assigned_units.end());
                delete ref;
                return true;
            }), u->general_refs.end());

    df::general_ref_building_civzone_assignedst *ref = df::allocate<df::general_ref_building_civzone_assignedst>();
    ref->building_id = bld->id;
    u->general_refs.push_back(ref);
    bld->assigned_units.push_back(u->id);
}

std::string Population::status()
{
    return stl_sprintf("%d citizen, %d military, %d pet, %d visitor, %d resident", citizen.size(), military.size(), pet.size(), visitor.size(), resident.size());
}

// vim: et:sw=4:ts=4

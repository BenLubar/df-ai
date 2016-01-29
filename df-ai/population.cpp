#include "ai.h"
#include "population.h"
#include "plan.h"
#include "stocks.h"

#include "modules/Gui.h"
#include "modules/Units.h"

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
#include "df/itemdef_weaponst.h"
#include "df/job.h"
#include "df/manager_order.h"
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
#include "df/viewscreen_dwarfmodest.h"
#include "df/world.h"

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

        stocks[unit_labor::PLANT] = {"food", "drink", "cloth"};
        stocks[unit_labor::HERBALIST] = {"food", "drink", "cloth"};
        stocks[unit_labor::FISH] = {"food"};

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

        for (df::unit_labor ul : hauling)
        {
            min_pct[ul] = 30;
            max_pct[ul] = 100;
        }
    }
} labors;

Population::Population(AI *ai) :
    ai(ai),
    citizen(),
    military(),
    idlers(),
    pet(),
    update_counter(0),
    onupdate_handle(nullptr),
    labor_worker(),
    worker_labor(),
    labor_needmore(),
    medic(),
    workers(),
    seen_badwork(),
    last_idle_year(-1)
{
}

Population::~Population()
{
}

command_result Population::startup(color_ostream & out)
{
    *standing_orders_forbid_used_ammo = 0;
    return CR_OK;
}

command_result Population::onupdate_register(color_ostream & out)
{
    onupdate_handle = events.onupdate_register("df-ai pop", 36, 1, [this](color_ostream & out) { update(out); });
    return CR_OK;
}

command_result Population::onupdate_unregister(color_ostream & out)
{
    events.onupdate_unregister(onupdate_handle);
    return CR_OK;
}

void Population::update(color_ostream & out)
{
    update_counter++;
    switch (update_counter % 100)
    {
        case 10:
            update_citizenlist(out);
            break;
        case 20:
            update_nobles(out);
            break;
        case 30:
            update_jobs(out);
            break;
        case 40:
            update_military(out);
            break;
        case 50:
            update_pets(out);
            break;
        case 60:
            update_deads(out);
            break;
        case 70:
            update_caged(out);
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

    // add new fort citizen to our list
    for (df::unit *u : world->units.active)
    {
        if (Units::isCitizen(u) && !Units::isBaby(u))
        {
            if (old.count(u->id))
            {
                old.erase(u->id);
            }
            else
            {
                new_citizen(out, u->id);
            }
        }
    }

    // del those who are no longer here
    for (int32_t id : old)
    {
        // u.counters.death_tg.flags.discovered dead/missing
        del_citizen(out, id);
    }
}

void Population::update_jobs(color_ostream & out)
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
    for (df::unit *u : world->units.all)
    {
        if (u->flags3.bits.ghostly)
        {
            ai->stocks->queue_slab(out, u->hist_figure_id);
        }
    }
}

void Population::update_caged(color_ostream & out)
{
    int32_t count = 0;
    for (df::item *cage : world->items.other[items_other_id::CAGE])
    {
        if (!cage->flags.bits.on_ground)
        {
            continue;
        }
        for (auto ref : cage->general_refs)
        {
            if (virtual_cast<df::general_ref_contains_itemst>(ref))
            {
                df::item *i = ref->getItem();
                if (i->flags.bits.dump)
                {
                    continue;
                }
                count++;
                i->flags.bits.dump = 1;
            }
            else if (virtual_cast<df::general_ref_contains_unitst>(ref))
            {
                df::unit *u = ref->getUnit();
                if (Units::isOwnCiv(u))
                {
                    // TODO rescue caged dwarves
                }
                else
                {
                    for (auto it : u->inventory)
                    {
                        if (it->item->flags.bits.dump)
                        {
                            continue;
                        }
                        count++;
                        it->item->flags.bits.dump = 1;
                    }

                    if (u->inventory.empty())
                    {
                        room *r = ai->plan->find_room("pitcage", [](room *r) -> bool { return r->dfbuilding(); });
                        if (r && ai->plan->spiral_search(r->pos(), 1, 1, [cage](df::coord t) -> bool { return t == cage->pos; }).isValid())
                        {
                            assign_unit_to_zone(u, virtual_cast<df::building_civzonest>(r->dfbuilding()));
                            military_random_squad_attack_unit(u);
                            ai->debug(out, "pop: marked " + AI::describe_unit(u) + " for pitting");
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

    for (df::unit *u : world->units.active)
    {
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
    for (df::unit *u : world->units.active)
    {
        if (Units::isCitizen(u) && !Units::isChild(u) && !Units::isBaby(u) && u->mood == mood_type::None)
        {
            bool hasTool = false;
            for (df::unit_labor lb : labors.tool)
            {
                if (u->status.labors[lb])
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

    for (int32_t uid : newsoldiers)
    {
        ai->plan->getsoldierbarrack(out, uid);
    }

    /*
    for (int32_t sqid : ui->main.fortress_entity->squads)
    {
        df::squad *sq = df::squad::find(sqid);
        int32_t soldier_count = 0;
        for (auto sp : sq->positions)
        {
            if (sp->occupant != -1)
            {
                soldier_count++;
            }
        }
        for (int32_t month = 0; month < 12; month++)
        {
            for (df::squad_schedule_order *so : sq->schedule[1][month]->orders)
            {
                df::squad_order_trainst *sot = virtual_cast<df::squad_order_trainst>(so->order);
                if (!sot)
                {
                    continue;
                }
                so->min_count = soldier_count > 3 ? soldier_count - 1 : soldier_count;
                room *r = ai->plan->find_room("barracks", [sqid](room *r) -> bool { return r->squad_id == sqid; });
                if (r && r->status != "finished")
                {
                    so->min_count = 0;
                }
            }
        }
    }
    */
}

void Population::military_random_squad_attack_unit(df::unit *u)
{
    df::squad *squad = nullptr;
    int32_t best;
    for (int32_t sqid : ui->main.fortress_entity->squads)
    {
        df::squad *sq = df::squad::find(sqid);

        int32_t score = 0;
        for (auto sp : sq->positions)
        {
            if (sp->occupant != -1)
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

    df::squad_order_kill_listst *so = df::allocate<df::squad_order_kill_listst>();
    so->units.push_back(u->id);
    so->title = AI::describe_unit(u);
    squad->orders.push_back(so);
}

std::string Population::military_find_commander_pos()
{
    for (df::entity_position_raw *a : ui->main.fortress_entity->entity_raw->positions)
    {
        if (a->responsibilities[entity_position_responsibility::MILITARY_STRATEGY] && a->flags.is_set(entity_position_raw_flags::SITE))
        {
            return a->code;
        }
    }
    return "";
}

std::string Population::military_find_captain_pos()
{
    for (df::entity_position_raw *a : ui->main.fortress_entity->entity_raw->positions)
    {
        if (a->flags.is_set(entity_position_raw_flags::MILITARY_SCREEN_ONLY) && a->flags.is_set(entity_position_raw_flags::SITE))
        {
            return a->code;
        }
    }
    return "";
}

// returns an unit newly assigned to a military squad
df::unit *Population::military_find_new_soldier(color_ostream & out, const std::vector<df::unit *> & unitlist)
{
    df::unit *ns = nullptr;
    int32_t best;
    for (df::unit *u : unitlist)
    {
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

    for (df::entity_position_assignment *a : ui->main.fortress_entity->positions.assignments)
    {
        if (a->squad_id == squad_id)
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

    for (int32_t sqid : ui->main.fortress_entity->squads)
    {
        int32_t count = 0;
        for (auto u : military)
        {
            if (u.second == sqid)
            {
                count++;
            }
        }
        if (count < squad_sz)
        {
            return sqid;
        }
    }

    // create a new squad using the UI
    std::set<df::interface_key> keys;
    keys.insert(interface_key::D_MILITARY);
    Gui::getCurViewscreen()->feed(&keys);

    keys.clear();
    keys.insert(interface_key::D_MILITARY_CREATE_SQUAD);
    Gui::getCurViewscreen()->feed(&keys);

    keys.clear();
    keys.insert(interface_key::STANDARDSCROLL_UP);
    Gui::getCurViewscreen()->feed(&keys);

    keys.clear();
    keys.insert(interface_key::SELECT);
    Gui::getCurViewscreen()->feed(&keys);

    keys.clear();
    keys.insert(interface_key::LEAVESCREEN);
    Gui::getCurViewscreen()->feed(&keys);

    // get the squad and its id
    int32_t squad_id = ui->main.fortress_entity->squads.back();
    df::squad *squad = df::squad::find(squad_id);

    squad->cur_alert_idx = 1; // train
    squad->uniform_priority = 2;
    squad->carry_food = 2;
    squad->carry_water = 2;

    const static std::vector<std::pair<df::uniform_category, df::item_type>> item_type
    {
        {uniform_category::body, item_type::ARMOR},
        {uniform_category::head, item_type::HELM},
        {uniform_category::pants, item_type::PANTS},
        {uniform_category::gloves, item_type::GLOVES},
        {uniform_category::shoes, item_type::SHOES},
        {uniform_category::shield, item_type::SHIELD},
        {uniform_category::weapon, item_type::WEAPON},
    };
    // uniform
    for (df::squad_position *pos : squad->positions)
    {
        for (auto it : item_type)
        {
            if (pos->uniform[it.first].empty())
            {
                df::squad_uniform_spec *sus = df::allocate<df::squad_uniform_spec>();
                sus->color = -1;
                sus->item_filter.item_type = it.second;
                sus->item_filter.material_class = it.first == uniform_category::weapon ? entity_material_category::None : entity_material_category::Armor;
                sus->item_filter.mattype = -1;
                sus->item_filter.matindex = -1;
                pos->uniform[it.first].push_back(sus);
            }
        }
        pos->flags.bits.exact_matches = 1;
    }

    if (ui->main.fortress_entity->squads.size() % 3 == 0)
    {
        // ranged squad
        for (df::squad_position *pos : squad->positions)
        {
            pos->uniform[uniform_category::weapon][0]->indiv_choice.bits.ranged = 1;
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
        for (int32_t id : ui->main.fortress_entity->entity_raw->equipment.weapon_id)
        {
            df::itemdef_weaponst *idef = df::itemdef_weaponst::find(id);
            if (idef->skill_melee != job_skill::MINING && idef->skill_melee != job_skill::AXE && idef->skill_ranged == job_skill::NONE && !idef->flags.is_set(weapon_flags::TRAINING))
            {
                weapons.push_back(id);
            }
        }
        if (weapons.empty())
        {
            for (df::squad_position *pos : squad->positions)
            {
                pos->uniform[uniform_category::weapon][0]->indiv_choice.bits.melee = 1;
            }
        }
        else
        {
            int32_t n = ui->main.fortress_entity->squads.size();
            n -= n / 3 + 1;
            n *= 10;
            for (df::squad_position *pos : squad->positions)
            {
                pos->uniform[uniform_category::weapon][0]->item_filter.item_subtype = weapons[n % weapons.size()];
                n++;
            }
        }
    }

    return squad_id;
}

void Population::autolabors(color_ostream & out, size_t step)
{
    switch (step)
    {
        case 1:
            {
                workers.clear();
                idlers.clear();
                labor_needmore.clear();
                std::vector<std::tuple<int32_t, std::string, bool>> nonworkers;
                medic.clear();
                for (df::entity_position_assignment *n : ui->main.fortress_entity->assignments_by_type[entity_position_responsibility::HEALTH_MANAGEMENT])
                {
                    if (df::historical_figure *hf = df::historical_figure::find(n->histfig))
                    {
                        medic.insert(hf->unit_id);
                    }
                }

                bool merchant = false;
                for (df::unit *u : world->units.all)
                {
                    if (u->flags1.bits.merchant && !u->flags1.bits.dead)
                    {
                        merchant = true;
                        break;
                    }
                }

                set_up_trading(merchant);

                for (int32_t c : citizen)
                {
                    df::unit *u = df::unit::find(c);
                    std::vector<Units::NoblePosition> positions;
                    if (!u)
                    {
                        continue;
                    }

                    if (u->mood != mood_type::None)
                    {
                        nonworkers.push_back(std::make_tuple(c, "has mood: " + ENUM_KEY_STR(mood_type, u->mood), false));
                    }
                    else if (Units::isChild(u))
                    {
                        nonworkers.push_back(std::make_tuple(c, "is a child", true));
                    }
                    else if (Units::isBaby(u))
                    {
                        nonworkers.push_back(std::make_tuple(c, "is a baby", true));
                    }
                    else if (unit_hasmilitaryduty(u))
                    {
                        nonworkers.push_back(std::make_tuple(c, "has military duty", false));
                    }
                    else if (u->flags1.bits.caged)
                    {
                        nonworkers.push_back(std::make_tuple(c, "caged", false));
                    }
                    else if (citizen.size() >= 20 && !world->manager_orders.empty() && !world->manager_orders.back()->is_validated && Units::getNoblePositions(&positions, u) && std::find_if(positions.begin(), positions.end(), [](const Units::NoblePosition & pos) -> bool { return pos.position->responsibilities[entity_position_responsibility::MANAGE_PRODUCTION]; }) != positions.end())
                    {
                        nonworkers.push_back(std::make_tuple(c, "validating work orders", false));
                    }
                    else if (merchant && Units::getNoblePositions(&positions, u) && std::find_if(positions.begin(), positions.end(), [](const Units::NoblePosition & pos) -> bool { return pos.position->responsibilities[entity_position_responsibility::TRADE]; }) != positions.end())
                    {
                        nonworkers.push_back(std::make_tuple(c, "trading", false));
                    }
                    else if (u->job.current_job && labors.wont_work_job.count(u->job.current_job->job_type))
                    {
                        nonworkers.push_back(std::make_tuple(c, ENUM_ATTR(job_type, caption, u->job.current_job->job_type), false));
                    }
                    else if (std::find_if(u->status.misc_traits.begin(), u->status.misc_traits.end(), [](df::unit_misc_trait *mt) -> bool { return mt->id == misc_trait_type::OnBreak; }) != u->status.misc_traits.end())
                    {
                        nonworkers.push_back(std::make_tuple(c, "on break", false));
                    }
                    else if (std::find_if(u->specific_refs.begin(), u->specific_refs.end(), [](df::specific_ref *sr) -> bool { return sr->type == specific_ref_type::ACTIVITY; }) != u->specific_refs.end())
                    {
                        nonworkers.push_back(std::make_tuple(c, "has activity", false));
                    }
                    else
                    {
                        // TODO filter nobles that will not work
                        workers.push_back(c);
                        if (!u->job.current_job)
                        {
                            idlers.push_back(c);
                        }
                    }
                }

                // free non-workers
                for (auto c : nonworkers)
                {
                    df::unit *u = df::unit::find(std::get<0>(c));
                    auto & ul = u->status.labors;
                    for (df::unit_labor lb : labors.list)
                    {
                        if (labors.hauling.count(lb))
                        {
                            if (!ul[lb] && !std::get<2>(c))
                            {
                                ul[lb] = true;
                                if (labors.tool.count(lb))
                                {
                                    u->military.pickup_flags.bits.update = 1;
                                }
                                ai->debug(out, "assigning labor " + ENUM_KEY_STR(unit_labor, lb) + " to " + AI::describe_unit(u) + " (non-worker: " + std::get<1>(c) + ")");
                            }
                        }
                        else if (ul[lb])
                        {
                            if (labors.medical.count(lb) && medic.count(u->id))
                            {
                                continue;
                            }
                            ul[lb] = false;
                            if (labors.tool.count(lb))
                            {
                                u->military.pickup_flags.bits.update = 1;
                            }
                            ai->debug(out, "unassigning labor " + ENUM_KEY_STR(unit_labor, lb) + " from " + AI::describe_unit(u) + " (non-worker: " + std::get<1>(c) + ")");
                        }
                    }
                }
            }
            break;

        case 2:
            {
                std::set<int32_t> seen_workshop;
                for (auto job = world->job_list.next; job; job = job->next)
                {
                    df::general_ref_building_holderst *ref_bld = nullptr;
                    for (df::general_ref *ref : job->item->general_refs)
                    {
                        ref_bld = virtual_cast<df::general_ref_building_holderst>(ref);
                        if (ref_bld)
                        {
                            break;
                        }
                    }
                    df::general_ref_unit_workerst *ref_wrk = nullptr;
                    for (df::general_ref *ref : job->item->general_refs)
                    {
                        ref_wrk = virtual_cast<df::general_ref_unit_workerst>(ref);
                        if (ref_wrk)
                        {
                            break;
                        }
                    }
                    if (ref_bld && seen_workshop.count(ref_bld->building_id))
                    {
                        continue;
                    }

                    if (!ref_wrk)
                    {
                        df::unit_labor job_labor = ENUM_ATTR(job_type, labor, job->item->job_type);
                        if (job_labor == unit_labor::NONE)
                            job_labor = ENUM_ATTR(job_skill, labor, ENUM_ATTR(job_type, skill, job->item->job_type));
                        if (job_labor != unit_labor::NONE)
                        {
                            labor_needmore[job_labor]++;
                        }
                        else
                        {
                            switch (job->item->job_type)
                            {
                                case job_type::ConstructBuilding:
                                case job_type::DestroyBuilding:
                                    {
                                        // TODO
                                        // labor_needmore[UNKNOWN_BUILDING_LABOR_PLACEHOLDER]++;
                                    }
                                    break;

                                case job_type::CustomReaction:
                                    {
                                        df::reaction *reac = nullptr;
                                        for (df::reaction *r : world->raws.reactions)
                                        {
                                            if (r->code == job->item->reaction_name)
                                            {
                                                reac = r;
                                                break;
                                            }
                                        }
                                        if (reac && (job_labor = ENUM_ATTR(job_skill, labor, reac->skill)) != unit_labor::NONE)
                                        {
                                            labor_needmore[job_labor]++;
                                        }
                                    }
                                    break;
                                case job_type::PenSmallAnimal:
                                case job_type::PenLargeAnimal:
                                    {
                                        labor_needmore[unit_labor::HAUL_ANIMALS]++;
                                    }
                                    break;
                                case job_type::StoreItemInStockpile:
                                case job_type::StoreItemInBag:
                                case job_type::StoreItemInHospital:
                                case job_type::StoreWeapon:
                                case job_type::StoreArmor:
                                case job_type::StoreItemInBarrel:
                                case job_type::StoreItemInBin:
                                case job_type::StoreItemInVehicle:
                                    {
                                        for (df::unit_labor lb : labors.hauling)
                                        {
                                            labor_needmore[lb]++;
                                        }
                                    }
                                    break;
                                default:
                                    {
                                        if (job->item->material_category.bits.wood)
                                        {
                                            labor_needmore[unit_labor::CARPENTER]++;
                                        }
                                        else if (job->item->material_category.bits.bone)
                                        {
                                            labor_needmore[unit_labor::BONE_CARVE]++;
                                        }
                                        else if (job->item->material_category.bits.shell)
                                        {
                                            labor_needmore[unit_labor::BONE_CARVE]++;
                                        }
                                        else if (job->item->material_category.bits.cloth)
                                        {
                                            labor_needmore[unit_labor::CLOTHESMAKER]++;
                                        }
                                        else if (job->item->material_category.bits.leather)
                                        {
                                            labor_needmore[unit_labor::LEATHER]++;
                                        }
                                        else if (job->item->mat_type == 0)
                                        {
                                            // XXX metalcraft ?
                                            labor_needmore[unit_labor::MASON]++;
                                        }
                                        else
                                        {
                                            if (seen_badwork.insert(job->item->job_type).second)
                                            {
                                                ai->debug(out, "unknown labor for " + ENUM_KEY_STR(job_type, job->item->job_type));
                                            }
                                        }
                                    }
                                    break;
                            }
                        }
                    }

                    if (ref_bld)
                    {
                        df::building *bld = ref_bld->getBuilding();
                        if (virtual_cast<df::building_farmplotst>(bld) || virtual_cast<df::building_stockpilest>(bld))
                        {
                            // parallel work allowed
                        }
                        else
                        {
                            seen_workshop.insert(ref_bld->building_id);
                        }
                    }
                }
            }
            break;

        case 3:
            {
                // count active labors
                labor_worker.clear();
                worker_labor.clear();
                for (df::unit_labor lb : labors.list)
                {
                    labor_worker[lb] = std::set<int32_t>();
                }
                for (int32_t c : workers)
                {
                    worker_labor[c] = std::set<df::unit_labor>();
                    auto & ul = df::unit::find(c)->status.labors;
                    for (df::unit_labor lb : labors.list)
                    {
                        if (ul[lb])
                        {
                            labor_worker[lb].insert(c);
                            worker_labor[c].insert(lb);
                        }
                    }
                }

                if (workers.size() > 15)
                {
                    // if one has too many labors, free him up (one per round)
                    const int32_t lim = labors.list.size() / 2;
                    for (auto & wl : worker_labor)
                    {
                        if (wl.second.size() > lim)
                        {
                            int32_t c = wl.first;
                            df::unit *u = df::unit::find(c);
                            auto & ul = u->status.labors;

                            for (df::unit_labor lb : labors.list)
                            {
                                if (ul[lb])
                                {
                                    if (labors.medical.count(lb) && medic.count(u->id))
                                    {
                                        continue;
                                    }
                                    worker_labor[c].erase(lb);
                                    labor_worker[lb].erase(c);
                                    ul[lb] = false;
                                    if (labors.tool.count(lb))
                                    {
                                        u->military.pickup_flags.bits.update = 1;
                                    }
                                }
                            }
                            ai->debug(out, "unassigned all labors from " + AI::describe_unit(u) + " (too many labors)");
                            break;
                        }
                    }
                }
            }
            break;

        case 4:
            {
                std::map<df::unit_labor, int32_t> labormin = labors.min;
                std::map<df::unit_labor, int32_t> labormax = labors.max;
                std::map<df::unit_labor, int32_t> laborminpct = labors.min_pct;
                std::map<df::unit_labor, int32_t> labormaxpct = labors.max_pct;

                for (df::unit_labor lb : labors.list)
                {
                    int32_t max = labormax.at(lb);
                    int32_t maxpc = labormaxpct.at(lb) * workers.size() / 100;
                    if (max < maxpc)
                        max = maxpc;
                    if (labors.stocks.count(lb))
                    {
                        for (std::string s : labors.stocks.at(lb))
                        {
                            if (!ai->stocks->need_more(s))
                            {
                                max = 0;
                                break;
                            }
                        }
                    }
                    if (lb == unit_labor::FISH && !ai->plan->find_room("workshop", [](room *r) -> bool { return r->subtype == "Fishery" && r->status != "plan"; }))
                    {
                        max = 0;
                    }
                    labormax[lb] = max;

                    if (labor_worker.at(lb).size() >= max)
                    {
                        labor_needmore.erase(lb);
                    }
                }

                if (labor_needmore.empty() && !idlers.empty() && ai->plan->past_initial_phase && last_idle_year != *cur_year)
                {
                    ai->plan->idleidle(out);
                    last_idle_year = *cur_year;
                }

                // handle low-number of workers + tool labors
                int32_t mintool = 0;
                for (df::unit_labor lb : labors.tool)
                {
                    int32_t min = labormin.at(lb);
                    int32_t minpc = laborminpct.at(lb) * workers.size() / 100;
                    if (min < minpc)
                    {
                        mintool += minpc;
                    }
                    else
                    {
                        mintool += min;
                    }
                }
                if (workers.size() < mintool)
                {
                    for (df::unit_labor lb : labors.tool)
                    {
                        labormax[lb] = 0;
                    }
                    if (workers.size() == 0)
                    {
                        // meh
                    }
                    else if (workers.size() == 1)
                    {
                        // switch mine or cutwood based on time (1/2 dwarf month each)
                        if (*cur_year_tick / (1200 * 28 / 2) % 2 == 0)
                        {
                            labormax[unit_labor::MINE]++;
                        }
                        else
                        {
                            labormax[unit_labor::CUTWOOD]++;
                        }
                    }
                    else
                    {
                        for (int32_t i = 0; i < workers.size(); i++)
                        {
                            // divide equally between labors, with priority to
                            // mine, then wood, then hunt
                            // XXX new labortools ?
                            switch (i % 3)
                            {
                                case 0:
                                    labormax[unit_labor::MINE]++;
                                    break;
                                case 1:
                                    labormax[unit_labor::CUTWOOD]++;
                                    break;
                                case 2:
                                    labormax[unit_labor::HUNT]++;
                                    break;
                            }
                        }
                    }

                    // list of dwarves with an exclusive labor
                    std::map<int32_t, df::unit_labor> exclusive;
                    for (auto lbtest : std::vector<std::pair<df::unit_labor, bool>>
                            {
                                {unit_labor::CARPENTER, ai->plan->find_room("workshop", [](room *r) -> bool { df::building_workshopst *bld = virtual_cast<df::building_workshopst>(r->dfbuilding()); return r->subtype == "Carpenters" && bld && !bld->jobs.empty(); })},
                                {unit_labor::MINE, ai->plan->is_digging()},
                                {unit_labor::MASON, ai->plan->find_room("workshop", [](room *r) -> bool { df::building_workshopst *bld = virtual_cast<df::building_workshopst>(r->dfbuilding()); return r->subtype == "Masons" && bld && !bld->jobs.empty(); })},
                                {unit_labor::CUTWOOD, ai->stocks->is_cutting_trees()},
                                {unit_labor::DETAIL, ai->plan->find_room("cistern", [](room *r) -> bool { return r->subtype == "well" && !r->channeled; })},
                            })
                    {
                        if (workers.size() > exclusive.size() + 2 && labor_needmore.count(lbtest.first) && lbtest.second)
                        {
                            // keep last run's choice
                            auto c = std::min_element(labor_worker.at(lbtest.first).begin(), labor_worker.at(lbtest.first).end(), [this](int32_t a, int32_t b) -> bool { return worker_labor.at(a).size() < worker_labor.at(b).size(); });
                            if (c == labor_worker.at(lbtest.first).end())
                            {
                                continue;
                            }
                            int32_t cid = *c;

                            exclusive[cid] = lbtest.first;
                            idlers.erase(std::remove(idlers.begin(), idlers.end(), cid), idlers.end());
                            std::set<df::unit_labor> labors_copy = worker_labor.at(cid);
                            for (df::unit_labor llb : labors_copy)
                            {
                                if (llb == lbtest.first)
                                    continue;
                                if (labors.tool.count(llb))
                                    continue;
                                autolabor_unsetlabor(out, cid, llb, "has exclusive labor: " + ENUM_KEY_STR(unit_labor, lbtest.first));
                            }
                        }
                    }

                    // autolabor!
                    for (df::unit_labor lb : labors.list)
                    {
                        int32_t min = labormin.at(lb);
                        int32_t max = labormax.at(lb);
                        int32_t minpc = laborminpct.at(lb) * workers.size() / 100;
                        if (min < minpc)
                            min = minpc;
                        if (max != 0 && labor_needmore.empty() && labors.idle.count(lb))
                            max = workers.size();
                        if (min > max)
                            min = max;
                        if (min > workers.size())
                            min = workers.size();

                        int32_t cnt = labor_worker.at(lb).size();
                        if (cnt > max)
                        {
                            if (labor_needmore.empty())
                            {
                                continue;
                            }
                            std::vector<int32_t> least_skilled(labor_worker.at(lb).begin(), labor_worker.at(lb).end());
                            if (labors.skill.count(lb))
                            {
                                df::job_skill sk = labors.skill.at(lb);
                                auto skill_rating = [sk](df::unit *u) -> int32_t
                                {
                                    for (auto usk : u->status.current_soul->skills)
                                    {
                                        if (usk->id == sk)
                                        {
                                            return int32_t(usk->rating);
                                        }
                                    }
                                    return 0;
                                };
                                std::sort(least_skilled.begin(), least_skilled.end(), [skill_rating](int32_t a, int32_t b) -> bool
                                        {
                                            return skill_rating(df::unit::find(a)) < skill_rating(df::unit::find(b));
                                        });
                            }
                            else
                            {
                                std::shuffle(least_skilled.begin(), least_skilled.end(), ai->rng);
                            }
                            while (least_skilled.size() > max)
                            {
                                autolabor_unsetlabor(out, least_skilled.back(), lb, "too many dwarves");
                                least_skilled.pop_back();
                            }
                        }
                        else if (cnt < min)
                        {
                            if (min < max && !labors.tool.count(lb))
                            {
                                min++;
                            }
                            std::function<int32_t(df::unit *)> skill_score = [](df::unit *) -> int32_t { return 0; };
                            if (labors.skill.count(lb))
                            {
                                df::job_skill sk = labors.skill.at(lb);
                                skill_score = [sk](df::unit *u) -> int32_t
                                {
                                    for (auto usk : u->status.current_soul->skills)
                                    {
                                        if (usk->id == sk)
                                        {
                                            return int32_t(usk->rating) * 4;
                                        }
                                    }
                                    return 0;
                                };
                            }
                            for (; cnt < min; cnt++)
                            {
                                int32_t c = -1;
                                int32_t best;
                                for (int32_t _c : workers)
                                {
                                    if (exclusive.count(_c))
                                    {
                                        continue;
                                    }
                                    if (labors.tool.count(lb))
                                    {
                                        bool found = false;
                                        for (df::unit_labor _lb : labors.tool)
                                        {
                                            if (worker_labor.at(_c).count(_lb))
                                            {
                                                found = true;
                                                break;
                                            }
                                        }
                                        if (found)
                                        {
                                            continue;
                                        }
                                    }
                                    if (worker_labor.at(_c).count(lb))
                                    {
                                        continue;
                                    }
                                    df::unit *u = df::unit::find(_c);
                                    int32_t score = worker_labor.at(_c).size() * 10;
                                    std::vector<Units::NoblePosition> positions;
                                    Units::getNoblePositions(&positions, u);
                                    score += positions.size() * 40;
                                    score -= skill_score(u);

                                    if (c == -1 || score < best)
                                    {
                                        c = _c;
                                        best = score;
                                    }
                                }

                                if (c == -1)
                                {
                                    break;
                                }

                                autolabor_setlabor(out, c, lb, "not enough dwarves");
                            }
                        }
                        else if (!idlers.empty() && labor_needmore.empty())
                        {
                            for (int32_t i = 0; i < max - cnt; i++)
                            {
                                autolabor_setlabor(out, idlers[std::uniform_int_distribution<size_t>(0, idlers.size() - 1)(ai->rng)], lb, "idleidle");
                            }
                        }
                        else if (!idlers.empty() && labor_needmore.count(lb))
                        {
                            for (int32_t i = 0; i < std::min(labor_needmore.at(lb), max - cnt); i++)
                            {
                                autolabor_setlabor(out, idlers[std::uniform_int_distribution<size_t>(0, idlers.size() - 1)(ai->rng)], lb, "idle");
                            }
                        }
                    }
                }
            }
            break;
    }
}

void Population::autolabor_setlabor(color_ostream & out, int32_t c, df::unit_labor lb, std::string reason)
{
    if (worker_labor.count(c) && worker_labor.at(c).count(lb))
    {
        return;
    }
    labor_worker[lb].insert(c);
    worker_labor[c].insert(lb);
    df::unit *u = df::unit::find(c);
    if (labors.tool.count(lb))
    {
        for (df::unit_labor _lb : labors.tool)
        {
            autolabor_unsetlabor(out, c, _lb, "conflicts with " + ENUM_KEY_STR(unit_labor, lb) + " (" + reason + ")");
        }
    }
    u->status.labors[lb] = true;
    if (labors.tool.count(lb))
    {
        u->military.pickup_flags.bits.update = 1;
    }
    ai->debug(out, "assigning labor " + ENUM_KEY_STR(unit_labor, lb) + " to " + AI::describe_unit(u) + " (" + reason + ")");
}

void Population::autolabor_unsetlabor(color_ostream & out, int32_t c, df::unit_labor lb, std::string reason)
{
    if (!worker_labor.count(c) || !worker_labor.at(c).count(lb))
    {
        return;
    }
    df::unit *u = df::unit::find(c);
    if (labors.medical.count(lb) && medic.count(c))
    {
        return;
    }
    labor_worker[lb].erase(c);
    worker_labor[c].erase(lb);
    u->status.labors[lb] = false;
    if (labors.tool.count(lb))
    {
        u->military.pickup_flags.bits.update = 1;
    }
    ai->debug(out, "unassigning labor " + ENUM_KEY_STR(unit_labor, lb) + " from " + AI::describe_unit(u) + " (" + reason + ")");
}

void Population::set_up_trading(bool should_be_trading)
{
    room *r = ai->plan->find_room("workshop", [](room *r) -> bool { return r->subtype == "TradeDepot"; });
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
    df::viewscreen *view = Gui::getCurViewscreen();
    if (!view || !strict_virtual_cast<df::viewscreen_dwarfmodest>(view))
    {
        return;
    }

    std::set<df::interface_key> keys;
    keys.insert(interface_key::D_BUILDJOB);
    view->feed(&keys);

    df::coord pos = r->pos();
    Gui::revealInDwarfmodeMap(pos, true);
    Gui::setCursorCoords(pos.x, pos.y, pos.z);

    keys.clear();
    keys.insert(interface_key::CURSOR_LEFT);
    view->feed(&keys);

    keys.clear();
    keys.insert(interface_key::BUILDJOB_DEPOT_REQUEST_TRADER);
    view->feed(&keys);

    keys.clear();
    keys.insert(interface_key::LEAVESCREEN);
    view->feed(&keys);
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
    for (auto sk : u->status.current_soul->skills)
    {
        int32_t rat = sk->rating;
        t += 400 * rat + 100 * rat * (rat + 1) / 2 + sk->experience;
    }
    return t;
}

std::string Population::positionCode(df::entity_position_responsibility responsibility)
{
    for (df::entity_position_raw *a : ui->main.fortress_entity->entity_raw->positions)
    {
        if (a->responsibilities[responsibility])
        {
            return a->code;
        }
    }
    return "";
}

void Population::update_nobles(color_ostream & out)
{
    std::vector<df::unit *> cz;
    for (df::unit *u : world->units.active)
    {
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

    if (ent->assignments_by_type[entity_position_responsibility::HEALTH_MANAGEMENT].empty() && ai->plan->find_room("infirmary", [](room *r) -> bool { return r->status != "plan"; }) && !cz.empty())
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
        for (df::unit_labor lb : labors.medical)
        {
            doctor->status.labors[lb] = true;
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

    for (df::entity_position_assignment *asn : ui->main.fortress_entity->positions.assignments)
    {
        df::entity_position *pos = binsearch_in_vector(ui->main.fortress_entity->positions.own, asn->position_id);
        if (pos->required_office > 0 || pos->required_dining > 0 || pos->required_tomb > 0)
        {
            if (df::historical_figure *hf = df::historical_figure::find(asn->histfig))
            {
                noble_ids.insert(hf->unit_id);
            }
        }
    }

    ai->plan->attribute_noblerooms(out, noble_ids);
}

df::entity_position_assignment *Population::assign_new_noble(color_ostream & out, std::string pos_code, df::unit *unit)
{
    df::historical_entity *ent = ui->main.fortress_entity;

    df::entity_position *pos = nullptr;
    for (df::entity_position *p : ent->positions.own)
    {
        if (p->code == pos_code)
        {
            pos = p;
            break;
        }
    }
    assert(pos);

    df::entity_position_assignment *assign = nullptr;
    for (df::entity_position_assignment *a : ent->positions.assignments)
    {
        if (a->position_id == pos->id && a->histfig == -1)
        {
            assign = a;
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
    for (df::manager_order *mo : world->manager_orders)
    {
        if (mo->job_type == job_type::MilkCreature)
        {
            needmilk -= mo->amount_left;
        }
        else if (mo->job_type == job_type::ShearCreature)
        {
            needshear -= mo->amount_left;
        }
    }

    std::map<df::caste_raw *, std::set<std::pair<int32_t, df::unit *>>> forSlaughter;

    std::map<int32_t, pet_flags> np = pet;
    for (df::unit *u : world->units.active)
    {
        if (!Units::isOwnCiv(u) || Units::isOwnRace(u))
        {
            continue;
        }
        if (u->flags1.bits.dead || u->flags1.bits.merchant || u->flags1.bits.forest || u->flags2.bits.slaughter)
        {
            continue;
        }

        df::creature_raw *race = df::creature_raw::find(u->race);
        df::caste_raw *cst = race->caste[u->caste];
        int32_t age = (*cur_year - u->relations.birth_year) * 12 * 28 + (*cur_year_tick - u->relations.birth_time) / 1200; // days

        if (pet.count(u->id))
        {
            if (cst->body_size_2.back() <= age && // full grown
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
                for (auto mt : u->status.misc_traits)
                {
                    if (mt->id == misc_trait_type::MilkCounter)
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
                for (auto stl : cst->shearable_tissue_layer)
                {
                    for (auto bpi : stl->bp_modifiers_idx)
                    {
                        if (u->appearance.bp_modifiers[bpi] >= stl->length)
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
            else
            {
                // TODO slaughter best candidate, keep this one
                // also avoid killing named pets
                u->flags2.bits.slaughter = 1;
                ai->debug(out, stl_sprintf("marked %dy%dd old %s:%s for slaughter (no pasture)", age / 12 / 28, age % (12 * 28), race->creature_id.c_str(), cst->caste_id.c_str()));
                continue;
            }
        }

        pet[u->id] = flags;
    }

    for (auto p : np)
    {
        ai->plan->freepasture(out, p.first);
        pet.erase(p.first);
    }

    for (auto cst : forSlaughter)
    {
        // we have reproductively viable animals, but there are more than 5 of
        // this sex (full-grown). kill the oldest ones for meat/leather/bones.

        if (cst.second.size() > 5)
        {
            // remove the youngest 5
            auto it = cst.second.begin();
            std::advance(it, 5);
            cst.second.erase(cst.second.begin(), it);

            for (auto candidate : cst.second)
            {
                int32_t age = candidate.first;
                df::unit *u = candidate.second;
                df::creature_raw *race = df::creature_raw::find(u->race);
                u->flags2.bits.slaughter = 1;
                ai->debug(out, stl_sprintf("marked %dy%dd old %s:%s for slaughter (too many adults)", age / 12 / 28, age % (12 * 28), race->creature_id.c_str(), cst.first->caste_id.c_str()));
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
    return stl_sprintf("%d citizen, %d military, %d idle, %d pets", citizen.size(), military.size(), idlers.size(), pet.size());
}

// vim: et:sw=4:ts=4

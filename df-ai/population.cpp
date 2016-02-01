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
    int32_t best = std::numeric_limits<int32_t>::min();
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
    int32_t best = std::numeric_limits<int32_t>::max();
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

    if (auto view = strict_virtual_cast<df::viewscreen_dwarfmodest>(Gui::getCurViewscreen()))
    {
        AI::feed_key(view, interface_key::D_BUILDJOB);

        df::coord pos = r->pos();
        Gui::revealInDwarfmodeMap(pos, true);
        Gui::setCursorCoords(pos.x, pos.y, pos.z);

        AI::feed_key(view, interface_key::CURSOR_LEFT);
        AI::feed_key(view, interface_key::BUILDJOB_DEPOT_REQUEST_TRADER);
        AI::feed_key(view, interface_key::LEAVESCREEN);
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

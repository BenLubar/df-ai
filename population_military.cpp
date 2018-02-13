#include "ai.h"
#include "population.h"
#include "plan.h"

#include "modules/Units.h"

#include "df/entity_material_category.h"
#include "df/entity_position_assignment.h"
#include "df/historical_entity.h"
#include "df/item_weaponst.h"
#include "df/itemdef_weaponst.h"
#include "df/squad.h"
#include "df/squad_ammo_spec.h"
#include "df/squad_order_kill_listst.h"
#include "df/squad_position.h"
#include "df/squad_uniform_spec.h"
#include "df/ui.h"
#include "df/uniform_category.h"
#include "df/world.h"

REQUIRE_GLOBAL(ui);
REQUIRE_GLOBAL(world);

void Population::update_military(color_ostream & out)
{
    // check for new soldiers, allocate barracks

    for (auto u : world->units.active)
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
                    ai->plan->getsoldierbarrack(out, u->id);
                }
            }
        }
    }

    // enlist new soldiers if needed
    std::vector<df::unit *> maydraft;
    for (auto u : world->units.active)
    {
        std::vector<Units::NoblePosition> positions;
        if (Units::isCitizen(u) && !Units::isChild(u) && !Units::isBaby(u) && u->mood == mood_type::None && u->military.squad_id == -1 && !Units::getNoblePositions(&positions, u))
        {
            if (u->status.labors[unit_labor::MINE] || u->status.labors[unit_labor::CUTWOOD] || u->status.labors[unit_labor::HUNT])
            {
                continue;
            }
            maydraft.push_back(u);
        }
    }

    size_t axes = 0, picks = 0;
    for (auto item : world->items.other[items_other_id::WEAPON])
    {
        df::item_weaponst *weapon = virtual_cast<df::item_weaponst>(item);
        if (!weapon || !weapon->subtype || !weapon->subtype->flags.is_set(weapon_flags::HAS_EDGE_ATTACK))
        {
            continue;
        }

        if (weapon->getMeleeSkill() == job_skill::AXE)
        {
            axes++;
        }
        else if (weapon->getMeleeSkill() == job_skill::MINING)
        {
            picks++;
        }
    }
    while (military.size() < citizen.size() / 4 && military.size() + 1 < axes && military.size() + 1 < picks)
    {
        df::unit *ns = military_find_new_soldier(out, maydraft);
        if (!ns)
        {
            break;
        }
        military[ns->id] = ns->military.squad_id;
        ai->plan->getsoldierbarrack(out, ns->id);
    }

    // Check barracks construction status.
    ai->find_room(room_type::barracks, [](room *r) -> bool
    {
        if (r->status < room_status::dug)
        {
            return false;
        }

        df::squad *squad = df::squad::find(r->squad_id);
        if (!squad)
        {
            return false;
        }

        df::building *training_equipment = df::building::find(r->bld_id);
        if (!training_equipment)
        {
            return false;
        }

        if (training_equipment->getBuildStage() < training_equipment->getMaxBuildStage())
        {
            return false;
        }

        // Barracks is ready to train soldiers. Send the squad to training.
        if (!squad->cur_alert_idx)
        {
            squad->cur_alert_idx = 1; // training
        }

        return false;
    });
}

bool Population::military_random_squad_attack_unit(color_ostream & out, df::unit *u)
{
    df::squad *squad = nullptr;
    int32_t best = std::numeric_limits<int32_t>::min();
    for (auto sqid : ui->main.fortress_entity->squads)
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
        score -= int32_t(sq->orders.size());

        for (auto it : sq->orders)
        {
            if (auto so = strict_virtual_cast<df::squad_order_kill_listst>(it))
            {
                if (std::find(so->units.begin(), so->units.end(), u->id) != so->units.end())
                {
                    score -= 10000;
                }
            }
        }

        if (!squad || best < score)
        {
            squad = sq;
            best = score;
        }
    }
    if (!squad)
    {
        return false;
    }

    return military_squad_attack_unit(out, squad, u);
}

bool Population::military_all_squads_attack_unit(color_ostream & out, df::unit *u)
{
    bool any = false;
    for (auto sqid : ui->main.fortress_entity->squads)
    {
        if (military_squad_attack_unit(out, df::squad::find(sqid), u))
            any = true;
    }
    return any;
}

bool Population::military_squad_attack_unit(color_ostream & out, df::squad *squad, df::unit *u)
{
    if (Units::isOwnCiv(u))
    {
        return false;
    }

    for (auto it : squad->orders)
    {
        if (auto so = strict_virtual_cast<df::squad_order_kill_listst>(it))
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

bool Population::military_cancel_attack_order(color_ostream & out, df::unit *u)
{
    bool any = false;
    for (auto sqid : ui->main.fortress_entity->squads)
    {
        if (auto squad = df::squad::find(sqid))
        {
            squad->orders.erase(std::remove_if(squad->orders.begin(), squad->orders.end(), [this, &out, u, &any, squad](df::squad_order *order) -> bool
            {
                if (auto kill = strict_virtual_cast<df::squad_order_kill_listst>(order))
                {
                    auto it = std::find(kill->units.begin(), kill->units.end(), u->id);
                    if (it != kill->units.end())
                    {
                        ai->debug(out, "pop: Cancelling squad order for " + AI::describe_name(squad->name, true) + " to attack " + AI::describe_unit(u) + ".");
                        any = true;
                        kill->units.erase(it);
                        if (kill->units.empty())
                        {
                            delete kill;
                            return true;
                        }
                    }
                }
                return false;
            }), squad->orders.end());
        }
    }
    return any;
}

// returns an unit newly assigned to a military squad
df::unit *Population::military_find_new_soldier(color_ostream & out, const std::vector<df::unit *> & unitlist)
{
    df::unit *ns = nullptr;
    int32_t best = std::numeric_limits<int32_t>::max();
    for (auto u : unitlist)
    {
        if (u->military.squad_id == -1)
        {
            int32_t score = unit_totalxp(u);
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
    if (!squad)
    {
        return nullptr;
    }
    auto pos = std::find_if(squad->positions.begin(), squad->positions.end(), [](df::squad_position *p) -> bool { return p->occupant == -1; });
    if (pos == squad->positions.end())
    {
        return nullptr;
    }

    (*pos)->occupant = ns->hist_figure_id;
    ns->military.squad_id = squad_id;
    ns->military.squad_position = pos - squad->positions.begin();

    for (auto it : ui->main.fortress_entity->positions.assignments)
    {
        if (it->squad_id == squad_id)
        {
            return ns;
        }
    }

    if (ui->main.fortress_entity->assignments_by_type[entity_position_responsibility::MILITARY_STRATEGY].empty())
    {
        assign_new_noble(out, entity_position_responsibility::MILITARY_STRATEGY, ns, squad_id);
    }
    else
    {
        assign_new_noble(out, [](df::entity_position *pos) -> bool
        {
            return pos->flags.is_set(entity_position_flags::MILITARY_SCREEN_ONLY);
        }, ns, "squad captain", squad_id);
    }

    return ns;
}

// return a squad index with an empty slot
int32_t Population::military_find_free_squad()
{
    int32_t squad_sz = 10;
    if (military.size() < 4 * 8)
        squad_sz = 8;
    if (military.size() < 4 * 6)
        squad_sz = 6;
    if (military.size() < 3 * 4)
        squad_sz = 4;

    for (auto sqid : ui->main.fortress_entity->squads)
    {
        int32_t count = 0;
        for (auto it : military)
        {
            if (it.second == sqid)
            {
                count++;
            }
        }
        if (count < squad_sz)
        {
            return sqid;
        }
    }

    size_t barracks_count = 0;
    ai->find_room(room_type::barracks, [&barracks_count](room *) -> bool
    {
        barracks_count++;
        return false;
    });

    if (ui->main.fortress_entity->squads.size() >= barracks_count)
    {
        return -1;
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

    squad->cur_alert_idx = 0; // off-duty, will be changed when the barracks is ready
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
    for (auto pos : squad->positions)
    {
        for (auto it : item_type.vec)
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

    if (ui->main.fortress_entity->squads.size() % 3 == 1)
    {
        // ranged squad
        for (auto pos : squad->positions)
        {
            pos->uniform[uniform_category::weapon][0]->indiv_choice.bits.ranged = 1;
        }
        df::squad_ammo_spec *sas = df::allocate<df::squad_ammo_spec>();
        sas->item_filter.item_type = item_type::AMMO;
        sas->item_filter.item_subtype = ai->stocks->manager_subtype.at(stock_item::bone_bolts);
        sas->item_filter.material_class = entity_material_category::None;
        sas->amount = 500;
        sas->flags.bits.use_combat = 1;
        sas->flags.bits.use_training = 1;
        squad->ammunition.push_back(sas);
    }
    else
    {
        for (auto pos : squad->positions)
        {
            pos->uniform[uniform_category::weapon][0]->indiv_choice.bits.melee = 1;
        }
    }

    return squad_id;
}

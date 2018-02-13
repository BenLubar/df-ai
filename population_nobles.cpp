#include "ai.h"
#include "population.h"
#include "plan.h"

#include "modules/Gui.h"
#include "modules/Units.h"

#include "df/entity_position_assignment.h"
#include "df/historical_entity.h"
#include "df/historical_figure.h"
#include "df/squad.h"
#include "df/squad_schedule_order.h"
#include "df/ui.h"
#include "df/unit_skill.h"
#include "df/unit_soul.h"
#include "df/viewscreen_layer_noblelistst.h"
#include "df/world.h"

REQUIRE_GLOBAL(cur_year_tick);
REQUIRE_GLOBAL(ui);
REQUIRE_GLOBAL(world);

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

void Population::update_nobles(color_ostream & out)
{
    if (!config.manage_nobles)
    {
        check_noble_appartments(out);
        return;
    }

    std::vector<df::unit *> cz;
    for (auto u : world->units.active)
    {
        std::vector<Units::NoblePosition> positions;
        if (Units::isCitizen(u) && !Units::isChild(u) && !Units::isBaby(u) && u->mood == mood_type::None && u->military.squad_id == -1 && !Units::getNoblePositions(&positions, u))
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
        assign_new_noble(out, entity_position_responsibility::MANAGE_PRODUCTION, cz.back());
        cz.pop_back();
    }

    if (ent->assignments_by_type[entity_position_responsibility::ACCOUNTING].empty() && !cz.empty())
    {
        for (auto it = cz.rbegin(); it != cz.rend(); it++)
        {
            if (!(*it)->status.labors[unit_labor::MINE])
            {
                ai->debug(out, "assigning new bookkeeper: " + AI::describe_unit(*it));
                assign_new_noble(out, entity_position_responsibility::ACCOUNTING, *it);
                ui->bookkeeper_settings = 4;
                cz.erase(it.base() - 1);
                break;
            }
        }
    }

    if (ent->assignments_by_type[entity_position_responsibility::HEALTH_MANAGEMENT].empty() && ai->plan->find_room(room_type::infirmary, [](room *r) -> bool { return r->status != room_status::plan; }) && !cz.empty())
    {
        ai->debug(out, "assigning new chief medical dwarf: " + AI::describe_unit(cz.back()));
        assign_new_noble(out, entity_position_responsibility::HEALTH_MANAGEMENT, cz.back());
        cz.pop_back();
    }

    if (!ent->assignments_by_type[entity_position_responsibility::HEALTH_MANAGEMENT].empty())
    {
        df::entity_position_assignment *asn = ent->assignments_by_type[entity_position_responsibility::HEALTH_MANAGEMENT].at(0);
        df::historical_figure *hf = df::historical_figure::find(asn->histfig);
        df::unit *doctor = df::unit::find(hf->unit_id);
        // doc => healthcare
        doctor->status.labors[unit_labor::DIAGNOSE] = true;
        doctor->status.labors[unit_labor::SURGERY] = true;
        doctor->status.labors[unit_labor::BONE_SETTING] = true;
        doctor->status.labors[unit_labor::SUTURING] = true;
        doctor->status.labors[unit_labor::DRESSING_WOUNDS] = true;
        doctor->status.labors[unit_labor::FEED_WATER_CIVILIANS] = true;
    }

    if (ent->assignments_by_type[entity_position_responsibility::TRADE].empty() && !cz.empty())
    {
        ai->debug(out, "assigning new broker: " + AI::describe_unit(cz.back()));
        assign_new_noble(out, entity_position_responsibility::TRADE, cz.back());
        cz.pop_back();
    }

    if (ent->assignments_by_type[entity_position_responsibility::LAW_ENFORCEMENT].empty() && !cz.empty())
    {
        ai->debug(out, "assigning new sheriff: " + AI::describe_unit(cz.back()));
        assign_new_noble(out, entity_position_responsibility::LAW_ENFORCEMENT, cz.back());
        cz.pop_back();
    }

    if (ent->assignments_by_type[entity_position_responsibility::EXECUTIONS].empty() && !cz.empty())
    {
        ai->debug(out, "assigning new hammerer: " + AI::describe_unit(cz.back()));
        assign_new_noble(out, entity_position_responsibility::EXECUTIONS, cz.back());
        cz.pop_back();
    }

    check_noble_appartments(out);
}

void Population::check_noble_appartments(color_ostream & out)
{
    std::set<int32_t> noble_ids;

    for (auto asn : ui->main.fortress_entity->positions.assignments)
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

df::entity_position_assignment *Population::assign_new_noble(color_ostream & out, std::function<bool(df::entity_position *)> filter, df::unit *unit, const std::string & description, int32_t squad_id)
{
    if (!AI::is_dwarfmode_viewscreen())
    {
        ai->debug(out, "[ERROR] cannot assign " + AI::describe_unit(unit) + " as " + description + ": not on dwarfmode viewscreen");
        return nullptr;
    }
    AI::feed_key(interface_key::D_NOBLES);
    if (auto view = strict_virtual_cast<df::viewscreen_layer_noblelistst>(Gui::getCurViewscreen(true)))
    {
        for (auto assign : view->assignments)
        {
            auto pos = assign ? binsearch_in_vector(ui->main.fortress_entity->positions.own, assign->position_id) : nullptr;
            if (pos && filter(pos) && (assign->histfig == -1 || !df::historical_figure::find(assign->histfig) || df::historical_figure::find(assign->histfig)->died_year != -1) && assign->squad_id == squad_id)
            {
                AI::feed_key(interface_key::SELECT);
                for (auto c : view->candidates)
                {
                    if (c->unit == unit)
                    {
                        AI::feed_key(interface_key::SELECT);
                        AI::feed_key(interface_key::LEAVESCREEN);
                        return assign;
                    }
                    AI::feed_key(interface_key::STANDARDSCROLL_DOWN);
                }
                AI::feed_key(interface_key::LEAVESCREEN);
                AI::feed_key(interface_key::LEAVESCREEN);
                ai->debug(out, "[ERROR] cannot assign " + AI::describe_unit(unit) + " as " + pos->code + ": unit is not candidate");
                return nullptr;
            }
            AI::feed_key(interface_key::STANDARDSCROLL_DOWN);
        }
        AI::feed_key(interface_key::LEAVESCREEN);
        ai->debug(out, "[ERROR] cannot assign " + AI::describe_unit(unit) + " as " + description + ": could not find position");
        return nullptr;
    }
    ai->debug(out, "[ERROR] cannot assign " + AI::describe_unit(unit) + " as " + description + ": nobles screen did not appear");
    return nullptr;
}

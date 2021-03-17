#include "ai.h"
#include "population.h"

#include "df/activity_entry.h"
#include "df/activity_event_conflictst.h"
#include "df/creature_raw.h"
#include "df/historical_entity.h"
#include "df/job.h"
#include "df/squad.h"
#include "df/squad_order_kill_listst.h"
#include "df/ui.h"
#include "df/world.h"

#include "modules/Maps.h"
#include "modules/Units.h"

REQUIRE_GLOBAL(ui);
REQUIRE_GLOBAL(world);

bool AI::tag_enemies(color_ostream & out)
{
    bool found = false;
    for (auto id : ui->main.fortress_entity->squads)
    {
        auto squad = df::squad::find(id);
        for (auto order : squad->orders)
        {
            if (auto kill = virtual_cast<df::squad_order_kill_listst>(order))
            {
                for (auto unit_id : kill->units)
                {
                    auto unit = df::unit::find(unit_id);
                    if (unit && std::find(world->units.active.begin(), world->units.active.end(), unit) == world->units.active.end())
                    {
                        found = pop.military_cancel_attack_order(out, unit, "unit no longer active on map") || found;
                    }
                }
            }
        }
    }
    for (auto it = world->units.active.rbegin(); it != world->units.active.rend(); it++)
    {
        df::unit *u = *it;
        df::creature_raw *race = df::creature_raw::find(u->race);
        if (!Units::isDead(u) && Units::getPosition(u).isValid() &&
            !Units::isOwnCiv(u) && Units::getContainer(u) == nullptr &&
            !Maps::getTileDesignation(Units::getPosition(u))->bits.hidden)
        {
            if (race && race->flags.is_set(creature_raw_flags::HAS_ANY_MEGABEAST))
            {
                found = pop.military_all_squads_attack_unit(out, u, "primary antagonist: megabeast") || found;
            }
            else if (race && race->flags.is_set(creature_raw_flags::HAS_ANY_SEMIMEGABEAST))
            {
                found = pop.military_all_squads_attack_unit(out, u, "primary antagonist: semi-megabeast") || found;
            }
            else if (race && race->flags.is_set(creature_raw_flags::HAS_ANY_FEATURE_BEAST))
            {
                found = pop.military_all_squads_attack_unit(out, u, "primary antagonist: forgotten beast") || found;
            }
            else if (race && race->flags.is_set(creature_raw_flags::HAS_ANY_TITAN))
            {
                found = pop.military_all_squads_attack_unit(out, u, "primary antagonist: titan") || found;
            }
            else if (race && race->flags.is_set(creature_raw_flags::HAS_ANY_UNIQUE_DEMON))
            {
                found = pop.military_all_squads_attack_unit(out, u, "primary antagonist: demon") || found;
            }
            else if (race && race->flags.is_set(creature_raw_flags::HAS_ANY_DEMON))
            {
                found = pop.military_all_squads_attack_unit(out, u, "antagonist: demon") || found;
            }
            else if (race && race->flags.is_set(creature_raw_flags::HAS_ANY_NIGHT_CREATURE))
            {
                found = pop.military_all_squads_attack_unit(out, u, "antagonist: night creature") || found;
            }
            else if (Units::isOpposedToLife(u))
            {
                found = pop.military_random_squad_attack_unit(out, u, "undead") || found;
            }
            else if (u->flags1.bits.active_invader)
            {
                found = pop.military_random_squad_attack_unit(out, u, "active invader") || found;
            }
            else if (u->flags1.bits.marauder)
            {
                found = pop.military_random_squad_attack_unit(out, u, "marauder") || found;
            }
            else if (u->flags2.bits.underworld)
            {
                found = pop.military_random_squad_attack_unit(out, u, "underworld creature") || found;
            }
            else if (u->flags2.bits.visitor_uninvited)
            {
                found = pop.military_random_squad_attack_unit(out, u, "uninvited visitor") || found;
            }
            else if (auto hunter = is_hunting_target(u))
            {
                found = pop.military_cancel_attack_order(out, u, "hunting target of " + AI::describe_unit(hunter)) || found;
            }
            else if (auto citizen = u->flags2.bits.roaming_wilderness_population_source ? is_attacking_citizen(u) : nullptr)
            {
                found = pop.military_random_squad_attack_unit(out, u, "attacking citizen: " + AI::describe_unit(citizen)) || found;
            }
        }
    }
    return found;
}

df::unit *AI::is_attacking_citizen(df::unit *u)
{
    df::unit *citizen = nullptr;

    is_in_conflict(u, [u, &citizen](df::activity_event_conflictst *conflict) -> bool
    {
        auto unit_side = std::find_if(conflict->sides.begin(), conflict->sides.end(), [u](const df::activity_event_conflictst::T_sides * side) -> bool
        {
            return std::find(side->unit_ids.begin(), side->unit_ids.end(), u->id) != side->unit_ids.end();
        });
        if (unit_side == conflict->sides.end())
        {
            // Not in their own fight?
            return false;
        }

        for (auto enemy_side : (*unit_side)->enemies)
        {
            if (enemy_side->conflict_level >= conflict_level::Lethal)
            {
                auto side = conflict->sides.at(enemy_side->id);
                for (auto enemy_id : side->unit_ids)
                {
                    if (auto enemy = df::unit::find(enemy_id))
                    {
                        if (Units::isSane(enemy) && Units::isCitizen(enemy))
                        {
                            citizen = enemy;
                            return true;
                        }
                    }
                }
            }
        }

        return false;
    });

    return citizen;
}

df::unit *AI::is_hunting_target(df::unit *u)
{
    for (auto c : world->units.active)
    {
        if (Units::isSane(c) && Units::isCitizen(c) && u->job.current_job && u->job.current_job->job_type == job_type::Hunt && c->job.hunt_target == u)
        {
            return c;
        }
    }

    return nullptr;
}

bool AI::is_in_conflict(df::unit *u, std::function<bool(df::activity_event_conflictst *)> filter)
{
    for (auto id : u->activities)
    {
        if (auto act = df::activity_entry::find(id))
        {
            for (auto event : act->events)
            {
                if (auto conflict = virtual_cast<df::activity_event_conflictst>(event))
                {
                    if (filter(conflict))
                    {
                        return true;
                    }
                }
            }
        }
    }

    return false;
}

#include "ai.h"
#include "population.h"

#include "df/activity_entry.h"
#include "df/activity_event_conflictst.h"
#include "df/creature_raw.h"
#include "df/world.h"

#include "modules/Maps.h"
#include "modules/Units.h"

REQUIRE_GLOBAL(world);

bool AI::tag_enemies(color_ostream & out)
{
    bool found = false;
    for (auto it = world->units.active.rbegin(); it != world->units.active.rend(); it++)
    {
        df::unit *u = *it;
        df::creature_raw *race = df::creature_raw::find(u->race);
        if (!Units::isDead(u) && Units::getPosition(u).isValid() &&
            !Units::isOwnCiv(u) && Units::getContainer(u) == nullptr &&
            !Maps::getTileDesignation(Units::getPosition(u))->bits.hidden)
        {
            if (race &&
                (race->flags.is_set(creature_raw_flags::CASTE_MEGABEAST) ||
                    race->flags.is_set(creature_raw_flags::CASTE_SEMIMEGABEAST) ||
                    race->flags.is_set(creature_raw_flags::CASTE_FEATURE_BEAST) ||
                    race->flags.is_set(creature_raw_flags::CASTE_TITAN) ||
                    race->flags.is_set(creature_raw_flags::CASTE_UNIQUE_DEMON) ||
                    race->flags.is_set(creature_raw_flags::CASTE_DEMON) ||
                    race->flags.is_set(creature_raw_flags::CASTE_NIGHT_CREATURE_ANY)))
            {
                if (pop->military_all_squads_attack_unit(out, u))
                {
                    found = true;
                }
            }
            else if (Units::isUndead(u) ||
                u->flags1.bits.marauder ||
                u->flags1.bits.active_invader ||
                u->flags2.bits.underworld ||
                u->flags2.bits.visitor_uninvited ||
                is_attacking_citizen(u))
            {
                if (pop->military_random_squad_attack_unit(out, u))
                {
                    found = true;
                }
            }
        }
    }
    return found;
}

bool AI::is_attacking_citizen(df::unit *u)
{
    return is_in_conflict(u, [u](df::activity_event_conflictst *conflict) -> bool
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
                            return true;
                        }
                    }
                }
            }
        }

        return false;
    });
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

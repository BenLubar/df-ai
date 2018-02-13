#include "ai.h"
#include "population.h"
#include "plan.h"

#include "modules/Gui.h"
#include "modules/Units.h"

#include "df/abstract_building_inn_tavernst.h"
#include "df/abstract_building_libraryst.h"
#include "df/abstract_building_templest.h"
#include "df/building.h"
#include "df/occupation.h"
#include "df/ui.h"
#include "df/viewscreen_locationsst.h"
#include "df/world.h"
#include "df/world_site.h"

REQUIRE_GLOBAL(ui);
REQUIRE_GLOBAL(world);

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
        size_t petitions_before = ui->petitions.size();
        AI::feed_key(interface_key::D_PETITIONS);
        AI::feed_key(interface_key::OPTION1);
        AI::feed_key(interface_key::LEAVESCREEN);
        if (petitions_before <= ui->petitions.size())
        {
            // FIXME
            break;
        }
    }

#define INIT_NEED(name) int32_t need_##name = std::max(wanted_##name * int32_t(citizen.size()) / 200, wanted_##name##_min)
    INIT_NEED(tavern_keeper);
    INIT_NEED(tavern_performer);
    INIT_NEED(library_scholar);
    INIT_NEED(library_scribe);
    INIT_NEED(temple_performer);
#undef INIT_NEED

    if (room *tavern = ai->plan->find_room(room_type::location, [](room *r) -> bool { return r->location_type == location_type::tavern && r->dfbuilding(); }))
    {
        df::building *bld = tavern->dfbuilding();
        if (auto loc = virtual_cast<df::abstract_building_inn_tavernst>(binsearch_in_vector(df::world_site::find(bld->site_id)->buildings, bld->location_id)))
        {
            for (auto occ : loc->occupations)
            {
                if (occ->unit_id != -1)
                {
                    if (occ->type == occupation_type::TAVERN_KEEPER)
                    {
                        need_tavern_keeper--;
                    }
                    else if (occ->type == occupation_type::PERFORMER)
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

    if (room *library = ai->plan->find_room(room_type::location, [](room *r) -> bool { return r->location_type == location_type::library && r->dfbuilding(); }))
    {
        df::building *bld = library->dfbuilding();
        if (auto loc = virtual_cast<df::abstract_building_libraryst>(binsearch_in_vector(df::world_site::find(bld->site_id)->buildings, bld->location_id)))
        {
            for (auto occ : loc->occupations)
            {
                if (occ->unit_id != -1)
                {
                    if (occ->type == occupation_type::SCHOLAR)
                    {
                        need_library_scholar--;
                    }
                    else if (occ->type == occupation_type::SCRIBE)
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

    if (room *temple = ai->plan->find_room(room_type::location, [](room *r) -> bool { return r->location_type == location_type::temple && r->dfbuilding(); }))
    {
        df::building *bld = temple->dfbuilding();
        if (auto loc = virtual_cast<df::abstract_building_templest>(binsearch_in_vector(df::world_site::find(bld->site_id)->buildings, bld->location_id)))
        {
            for (auto occ : loc->occupations)
            {
                if (occ->unit_id != -1)
                {
                    if (occ->type == occupation_type::PERFORMER)
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
    for (auto u : view->units)
    {
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
        for (auto o : world->occupations.all)
        {
            if (o->unit_id == u->id)
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

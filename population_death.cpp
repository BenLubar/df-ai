#include "ai.h"
#include "population.h"
#include "plan.h"
#include "stocks.h"

#include "modules/Units.h"

#include "df/building_coffinst.h"
#include "df/buildings_other_id.h"
#include "df/history_event_hist_figure_diedst.h"
#include "df/ui.h"
#include "df/world.h"

REQUIRE_GLOBAL(ui);
REQUIRE_GLOBAL(world);

void Population::deathwatch(color_ostream & out)
{
    if (world->history.events_death.size() == seen_death)
    {
        return;
    }

    auto it = world->history.events_death.begin();
    std::advance(it, seen_death);
    for (; it != world->history.events_death.end(); it++)
    {
        auto d = virtual_cast<df::history_event_hist_figure_diedst>(*it);

        if (!d || d->site != ui->site_id)
        {
            continue;
        }

        ai.debug(out, "[RIP] " + AI::describe_event(d));
    }

    seen_death = world->history.events_death.size();
}

void Population::update_deads(color_ostream & out)
{
    int32_t want_coffin = 3;
    int32_t want_pet_coffin = 1;

    for (auto u : world->units.all)
    {
        if (u->flags3.bits.ghostly)
        {
            ai.stocks.queue_slab(out, u->hist_figure_id);
        }
        else if (Units::isCitizen(u) && Units::isDead(u) && std::find_if(u->owned_buildings.begin(), u->owned_buildings.end(),
            [](df::building *bld) -> bool { return bld->getType() == building_type::Coffin; }) != u->owned_buildings.end())
        {
            want_coffin++;
        }
    }

    for (auto bld : world->buildings.other[buildings_other_id::COFFIN])
    {
        if (!bld->owner)
        {
            want_coffin--;

            df::building_coffinst *coffin = virtual_cast<df::building_coffinst>(bld);
            if (!coffin->burial_mode.bits.no_pets)
            {
                want_pet_coffin--;
            }
        }
    }

    if (want_coffin > 0)
    {
        // dont dig too early
        if (!ai.find_room(room_type::cemetery, [](room *r) -> bool { return r->status != room_status::plan; }))
        {
            want_coffin = 0;
        }

        // count actually allocated (plan wise) coffin buildings
        ai.find_room(room_type::cemetery, [&want_coffin](room *r) -> bool
        {
            for (auto f : r->layout)
            {
                if (f->type == layout_type::coffin && f->bld_id == -1 && !f->ignore)
                    want_coffin--;
            }
            return want_coffin <= 0;
        });

        for (int32_t i = 0; i < want_coffin; i++)
        {
            ai.plan.getcoffin(out);
        }
    }
    else if (want_pet_coffin > 0)
    {
        for (auto bld : world->buildings.other[buildings_other_id::COFFIN])
        {
            df::building_coffinst *coffin = virtual_cast<df::building_coffinst>(bld);
            if (!coffin->owner && coffin->burial_mode.bits.no_pets)
            {
                coffin->burial_mode.bits.no_pets = 0;

                // convert at most one per cycle
                break;
            }
        }
    }
}

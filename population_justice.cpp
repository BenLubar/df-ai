#include "ai.h"
#include "population.h"

#include "modules/Gui.h"
#include "modules/Units.h"

#include "df/building_civzonest.h"
#include "df/crime.h"
#include "df/general_ref_contains_itemst.h"
#include "df/general_ref_contains_unitst.h"
#include "df/item.h"
#include "df/items_other_id.h"
#include "df/punishment.h"
#include "df/ui.h"
#include "df/viewscreen_justicest.h"
#include "df/viewscreen_overallstatusst.h"
#include "df/world.h"

REQUIRE_GLOBAL(cur_year);
REQUIRE_GLOBAL(cur_year_tick);
REQUIRE_GLOBAL(ui);
REQUIRE_GLOBAL(world);

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
                if (i->flags.bits.dump && !i->flags.bits.forbid)
                {
                    continue;
                }
                count++;
                i->flags.bits.dump = 1;
                i->flags.bits.forbid = 0;
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
                    size_t waiting_items = 0;

                    for (auto ii = u->inventory.begin(); ii != u->inventory.end(); ii++)
                    {
                        if (auto owner = Items::getOwner((*ii)->item))
                        {
                            ai->debug(out, "pop: cannot strip item " + AI::describe_item((*ii)->item) + " owned by " + AI::describe_unit(owner));
                            continue;
                        }
                        waiting_items++;
                        if ((*ii)->item->flags.bits.dump && !(*ii)->item->flags.bits.forbid)
                        {
                            continue;
                        }
                        count++;
                        (*ii)->item->flags.bits.dump = 1;
                        (*ii)->item->flags.bits.forbid = 0;
                        ai->debug(out, "pop: marked item " + AI::describe_item((*ii)->item) + " for dumping");
                    }

                    if (!waiting_items)
                    {
                        room *r = ai->find_room(room_type::pitcage, [](room *r) -> bool { return r->dfbuilding(); });
                        if (r && AI::spiral_search(r->pos(), 1, 1, [cage](df::coord t) -> bool { return t == cage->pos; }).isValid())
                        {
                            assign_unit_to_zone(u, virtual_cast<df::building_civzonest>(r->dfbuilding()));
                            ai->debug(out, "pop: marked " + AI::describe_unit(u) + " for pitting");
                            military_random_squad_attack_unit(out, u);
                        }
                        else
                        {
                            military_cancel_attack_order(out, u);
                        }
                    }
                    else
                    {
                        ai->debug(out, stl_sprintf("pop: waiting for %s to be stripped for pitting (%d items remain)", AI::describe_unit(u).c_str(), waiting_items));
                        military_cancel_attack_order(out, u);
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

void Population::update_crimes(color_ostream & out)
{
    // check for criminals, log justice updates

    int32_t not_before_year = last_checked_crime_year;
    int32_t not_before_tick = last_checked_crime_tick;
    last_checked_crime_year = *cur_year;
    last_checked_crime_tick = *cur_year_tick;

    for (auto crime : world->crimes.all)
    {
        if (!crime->flags.bits.discovered || crime->site != ui->site_id)
        {
            continue;
        }

        std::string accusation;
        switch (crime->mode)
        {
        case df::crime::ProductionOrderViolation:
            accusation = "violation of a production order";
            break;
        case df::crime::ExportViolation:
            accusation = "violation of an export ban";
            break;
        case df::crime::JobOrderViolation:
            accusation = "violation of a job order";
            break;
        case df::crime::ConspiracyToSlowLabor:
            accusation = "conspiracy to slow labor";
            break;
        case df::crime::Murder:
            accusation = "murder";
            break;
        case df::crime::DisorderlyBehavior:
            accusation = "disorderly conduct";
            break;
        case df::crime::BuildingDestruction:
            accusation = "building destruction";
            break;
        case df::crime::Vandalism:
            accusation = "vandalism";
            break;
        case df::crime::Theft:
            accusation = "theft";
            break;
        case df::crime::Robbery:
            accusation = "robbery";
            break;
        case df::crime::BloodDrinking:
            accusation = "vampirism";
            break;
        }
        if (accusation.empty())
        {
            accusation = enum_item_key(crime->mode);
        }

        df::unit *criminal = df::unit::find(crime->criminal);
        df::unit *convicted = df::unit::find(crime->convicted);
        df::unit *victim = df::unit::find(crime->victim);

        std::string with_victim;
        if (victim)
        {
            with_victim = " with " + AI::describe_unit(victim) + " as the victim";
        }

        if (crime->discovered_year > not_before_year || (crime->discovered_year == not_before_year && crime->discovered_time >= not_before_tick))
        {
            ai->debug(out, "[Crime] New crime discovered: " + AI::describe_unit(criminal) + " is accused of " + accusation + with_victim + ".");
        }

        for (auto report : crime->reports)
        {
            if (report->report_year > not_before_year || (report->report_year == not_before_year && report->report_time >= not_before_tick))
            {
                // TODO: report->unk1

                if (report->unk1)
                {
                    df::unit *witness = df::unit::find(report->witness);
                    ai->debug(out, "[Crime] " + AI::describe_unit(witness) + " found evidence of " + accusation + with_victim + ".");
                }
                else
                {
                    df::unit *witness = df::unit::find(report->witness);
                    df::unit *accuses = df::unit::find(report->accuses);
                    ai->debug(out, "[Crime] " + AI::describe_unit(witness) + " accuses " + AI::describe_unit(accuses) + " of " + accusation + with_victim + ".");
                    if (accuses == convicted)
                    {
                        ai->debug(out, "[Crime] The accused has already been convicted.");
                    }
                    else if (accuses != criminal)
                    {
                        ai->debug(out, "[Crime] However, they are lying. " + AI::describe_unit(criminal) + " committed the crime.");
                    }
                }
            }
        }

        if (crime->flags.bits.needs_trial && criminal && !convicted && AI::is_dwarfmode_viewscreen())
        {
            ai->debug(out, "[Crime] Convicting " + AI::describe_unit(criminal) + " of " + accusation + with_victim + ".");
            AI::feed_key(interface_key::D_STATUS);
            if (auto screen = virtual_cast<df::viewscreen_overallstatusst>(Gui::getCurViewscreen(true)))
            {
                auto page = std::find(screen->visible_pages.begin(), screen->visible_pages.end(), df::viewscreen_overallstatusst::Justice);
                if (page == screen->visible_pages.end())
                {
                    ai->debug(out, "[Crime] [ERROR] Could not find justice tab on status screen.");
                }
                else
                {
                    while (screen->visible_pages.at(screen->page_cursor) != df::viewscreen_overallstatusst::Justice)
                    {
                        AI::feed_key(interface_key::STANDARDSCROLL_RIGHT);
                    }
                    AI::feed_key(interface_key::SELECT);
                    if (auto justice = virtual_cast<df::viewscreen_justicest>(Gui::getCurViewscreen(true)))
                    {
                        auto it = std::find(justice->cases.begin(), justice->cases.end(), crime);
                        if (it == justice->cases.end())
                        {
                            ai->debug(out, "[Crime] Could not find case. Checking " + std::string(justice->cold_cases ? "recent crimes" : "cold cases"));
                            AI::feed_key(interface_key::CHANGETAB);
                            it = std::find(justice->cases.begin(), justice->cases.end(), crime);
                        }
                        if (it == justice->cases.end())
                        {
                            ai->debug(out, "[Crime] [ERROR] Could not find case.");
                        }
                        else
                        {
                            while (justice->cases.size() <= size_t(justice->sel_idx_current) || justice->cases.at(size_t(justice->sel_idx_current)) != crime)
                            {
                                AI::feed_key(interface_key::STANDARDSCROLL_DOWN);
                            }
                            AI::feed_key(interface_key::SELECT);
                            auto convict = std::find(justice->convict_choices.begin(), justice->convict_choices.end(), criminal);
                            if (convict == justice->convict_choices.end())
                            {
                                ai->debug(out, "[Crime] [ERROR] Criminal is not on list of suspects.");
                                AI::feed_key(interface_key::LEAVESCREEN);
                            }
                            else
                            {
                                while (justice->convict_choices.at(justice->cursor_right) != criminal)
                                {
                                    AI::feed_key(interface_key::STANDARDSCROLL_DOWN);
                                }
                                AI::feed_key(interface_key::SELECT);
                            }
                        }
                    }
                    else
                    {
                        ai->debug(out, "[Crime] [ERROR] Could not open justice tab on status screen.");
                    }
                    AI::feed_key(interface_key::LEAVESCREEN);
                }
            }
            else
            {
                ai->debug(out, "[Crime] [ERROR] Could not open status screen.");
            }
            AI::feed_key(interface_key::LEAVESCREEN);
        }

        if (convicted && !crime->flags.bits.sentenced)
        {
            ai->debug(out, "[Crime] Waiting for sentencing for " + AI::describe_unit(convicted) + ", who was convicted of the crime of " + accusation + with_victim + ".");
        }
    }

    for (auto p : ui->punishments)
    {
        if (!p->beating && !p->hammer_strikes && !p->prison_counter)
        {
            continue;
        }
        std::string message("[Crime] Waiting for punishment: ");
        message += AI::describe_unit(p->criminal);
        message += "\n        Officer: ";
        message += AI::describe_unit(p->officer);
        if (p->beating)
        {
            message += "\n        Awaiting beating";
        }
        if (p->hammer_strikes)
        {
            message += "\n        Remaining hammer strikes: " + stl_sprintf("%d", p->hammer_strikes);
        }
        if (p->prison_counter)
        {
            int32_t days_remaining = (p->prison_counter + 120) / 120;
            message += "\n        Remaining jail time: ";
            message += stl_sprintf("%d", days_remaining);
            if (days_remaining == 1)
            {
                message += " day";
            }
            else
            {
                message += " days";
            }
        }
        ai->debug(out, message);
    }
}

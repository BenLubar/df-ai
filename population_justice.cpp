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
            bool ignore = true;
            if (cage->flags.bits.forbid && !cage->flags.bits.trader)
            {
                for (auto ref : cage->general_refs)
                {
                    auto bld = ref->getBuilding();
                    if (ref->getType() == general_ref_type::BUILDING_HOLDER && bld && bld->getType() == building_type::TradeDepot)
                    {
                        ignore = false;
                        break;
                    }
                }
            }
            if (ignore)
            {
                continue;
            }
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
                    room *r = ai.find_room(room_type::releasecage, [](room *r) -> bool { return r->status >= room_status::finished; });
                    if (r)
                    {
                        for (auto f : r->layout)
                        {
                            if (f->type == layout_type::lever && f->bld_id != -1)
                            {
                                ai.plan.add_task(task_type::rescue_caged, r, f, cage->id);
                                break;
                            }
                        }
                    }
                }
                else
                {
                    size_t waiting_items = 0;

                    for (auto ii : u->inventory)
                    {
                        if (Items::getOwner(ii->item))
                        {
                            continue;
                        }
                        waiting_items++;
                        if (ii->item->flags.bits.dump && !ii->item->flags.bits.forbid)
                        {
                            continue;
                        }
                        count++;
                        ii->item->flags.bits.dump = 1;
                        ii->item->flags.bits.forbid = 0;
                        ai.debug(out, "pop: marked item " + AI::describe_item(ii->item) + " for dumping");
                    }

                    if (!waiting_items)
                    {
                        room *r = ai.find_room(room_type::pitcage, [](room *r) -> bool { return r->dfbuilding(); });
                        if (r && AI::spiral_search(r->pos(), 1, 1, [cage](df::coord t) -> bool { return t == cage->pos; }).isValid())
                        {
                            assign_unit_to_zone(u, virtual_cast<df::building_civzonest>(r->dfbuilding()));
                            ai.debug(out, "pop: marked " + AI::describe_unit(u) + " for pitting");
                            military_random_squad_attack_unit(out, u, "just in case pitting fails");
                        }
                        else
                        {
                            military_cancel_attack_order(out, u, "caged, but not in place for pitting");
                        }
                    }
                    else
                    {
                        ai.debug(out, stl_sprintf("pop: waiting for %s to be stripped for pitting (%zu items remain)", AI::describe_unit(u).c_str(), waiting_items));
                        military_cancel_attack_order(out, u, "caged, but not ready for pitting");
                    }
                }
            }
        }
    }
    if (count > 0)
    {
        ai.debug(out, stl_sprintf("pop: dumped %d items from cages", count));
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
        case df::crime::Embezzlement:
            accusation = "embezzlement";
            break;
        case df::crime::AttemptedMurder:
            accusation = "attempted murder";
            break;
        case df::crime::Kidnapping:
            accusation = "kidnapping";
            break;
        case df::crime::AttemptedKidnapping:
            accusation = "attempted kidnapping";
            break;
        case df::crime::AttemptedTheft:
            accusation = "attempted theft";
            break;
        case df::crime::Treason:
            accusation = "treason";
            break;
        case df::crime::Espionage:
            accusation = "espionage";
            break;
        case df::crime::Bribery:
            accusation = "bribery";
            break;
        }
        if (accusation.empty())
        {
            accusation = enum_item_key(crime->mode);
        }

        df::unit *criminal = df::unit::find(crime->criminal);
        df::unit *convicted = df::unit::find(crime->convict_data.convicted);
        df::unit *victim = df::unit::find(crime->victim_data.victim);

        std::string with_victim;
        if (victim)
        {
            with_victim = " with " + AI::describe_unit(victim) + " as the victim";
        }

        if (crime->discovered_year > not_before_year || (crime->discovered_year == not_before_year && crime->discovered_time >= not_before_tick))
        {
            ai.debug(out, "[Crime] New crime discovered: " + AI::describe_unit(criminal) + " is accused of " + accusation + with_victim + ".");
        }

        if (crime->flags.bits.needs_trial && criminal && !convicted && AI::is_dwarfmode_viewscreen())
        {
            // FIXME: this should be an ExclusiveCallback
            ai.debug(out, "[Crime] Convicting " + AI::describe_unit(criminal) + " of " + accusation + with_victim + ".");
            Gui::getCurViewscreen(true)->feed_key(interface_key::D_STATUS);
            if (auto screen = virtual_cast<df::viewscreen_overallstatusst>(Gui::getCurViewscreen(true)))
            {
                auto page = std::find(screen->visible_pages.begin(), screen->visible_pages.end(), df::viewscreen_overallstatusst::Justice);
                if (page == screen->visible_pages.end())
                {
                    ai.debug(out, "[Crime] [ERROR] Could not find justice tab on status screen.");
                }
                else
                {
                    while (screen->visible_pages.at(screen->page_cursor) != df::viewscreen_overallstatusst::Justice)
                    {
                        Gui::getCurViewscreen(true)->feed_key(interface_key::STANDARDSCROLL_RIGHT);
                    }
                    Gui::getCurViewscreen(true)->feed_key(interface_key::SELECT);
                    if (auto justice = virtual_cast<df::viewscreen_justicest>(Gui::getCurViewscreen(true)))
                    {
                        auto it = std::find(justice->cases.begin(), justice->cases.end(), crime);
                        if (it == justice->cases.end())
                        {
                            ai.debug(out, "[Crime] Could not find case. Checking " + std::string(justice->cold_cases ? "recent crimes" : "cold cases"));
                            Gui::getCurViewscreen(true)->feed_key(interface_key::CHANGETAB);
                            it = std::find(justice->cases.begin(), justice->cases.end(), crime);
                        }
                        if (it == justice->cases.end())
                        {
                            ai.debug(out, "[Crime] [ERROR] Could not find case.");
                        }
                        else
                        {
                            while (justice->cases.size() <= size_t(justice->sel_idx_current) || justice->cases.at(size_t(justice->sel_idx_current)) != crime)
                            {
                                Gui::getCurViewscreen(true)->feed_key(interface_key::STANDARDSCROLL_DOWN);
                            }
                            Gui::getCurViewscreen(true)->feed_key(interface_key::SELECT);
                            auto convict = std::find(justice->convict_choices.begin(), justice->convict_choices.end(), criminal);
                            if (convict == justice->convict_choices.end())
                            {
                                ai.debug(out, "[Crime] [ERROR] Criminal is not on list of suspects.");
                                Gui::getCurViewscreen(true)->feed_key(interface_key::LEAVESCREEN);
                            }
                            else
                            {
                                while (justice->convict_choices.at(justice->cursor_right) != criminal)
                                {
                                    Gui::getCurViewscreen(true)->feed_key(interface_key::STANDARDSCROLL_DOWN);
                                }
                                Gui::getCurViewscreen(true)->feed_key(interface_key::SELECT);
                            }
                        }
                    }
                    else
                    {
                        ai.debug(out, "[Crime] [ERROR] Could not open justice tab on status screen.");
                    }
                    Gui::getCurViewscreen(true)->feed_key(interface_key::LEAVESCREEN);
                }
            }
            else
            {
                ai.debug(out, "[Crime] [ERROR] Could not open status screen.");
            }
            Gui::getCurViewscreen(true)->feed_key(interface_key::LEAVESCREEN);
        }

        if (convicted && !crime->flags.bits.sentenced)
        {
            ai.debug(out, "[Crime] Waiting for sentencing for " + AI::describe_unit(convicted) + ", who was convicted of the crime of " + accusation + with_victim + ".");
        }
    }
}

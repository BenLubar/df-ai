#include "ai.h"
#include "population.h"
#include "plan.h"

#include "modules/Buildings.h"
#include "modules/Gui.h"
#include "modules/Units.h"

#include "df/building_civzonest.h"
#include "df/building_nest_boxst.h"
#include "df/caste_raw.h"
#include "df/creature_raw.h"
#include "df/item_eggst.h"
#include "df/manager_order.h"
#include "df/manager_order_template.h"
#include "df/ui.h"
#include "df/ui_sidebar_menus.h"
#include "df/unit_misc_trait.h"
#include "df/unit_relationship_type.h"
#include "df/unit_wound.h"
#include "df/viewscreen.h"
#include "df/world.h"

REQUIRE_GLOBAL(ui);
REQUIRE_GLOBAL(ui_building_assign_units);
REQUIRE_GLOBAL(ui_building_item_cursor);
REQUIRE_GLOBAL(ui_sidebar_menus);
REQUIRE_GLOBAL(world);

void Population::update_pets(color_ostream & out)
{
    if (!ai.plan.pastures_ready(out))
    {
        // will check next time
        return;
    }

    int32_t needmilk = 0;
    int32_t needshear = 0;
    for (auto mo : world->manager_orders)
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
    for (auto it : pet_check)
    {
        np[it]; // make sure existing pasture assignments are checked
    }
    pet_check.clear();
    for (auto u : world->units.active)
    {
        if (!Units::isOwnCiv(u) || Units::isOwnGroup(u) || Units::isOwnRace(u) || u->cultural_identity != -1)
        {
            continue;
        }
        if (u->flags1.bits.inactive || u->flags1.bits.merchant || u->flags1.bits.forest || u->flags2.bits.visitor || u->flags2.bits.slaughter)
        {
            continue;
        }

        df::creature_raw *race = df::creature_raw::find(u->race);
        df::caste_raw *cst = race->caste[u->caste];

        if (cst->flags.is_set(caste_raw_flags::CAN_LEARN))
        {
            continue;
        }

        int32_t age = days_since(u->birth_year, u->birth_time);

        if (pet.count(u->id))
        {
            if (cst->body_size_2.back() <= age && // full grown
                u->profession != profession::TRAINED_HUNTER && // not trained
                u->profession != profession::TRAINED_WAR && // not trained
                u->relationship_ids[unit_relationship_type::Pet] == -1) // not owned
            {
                if (std::find_if(u->body.wounds.begin(), u->body.wounds.end(), [](df::unit_wound *w) -> bool { return std::find_if(w->parts.begin(), w->parts.end(), [](df::unit_wound::T_parts *p) -> bool { return p->flags2.bits.gelded; }) != w->parts.end(); }) != u->body.wounds.end() || cst->sex == pronoun_type::it)
                {
                    // animal can't reproduce, can't work, and will provide maximum butchering reward. kill it.
                    u->flags2.bits.slaughter = true;
                    ai.debug(out, stl_sprintf("marked %dy%dm%dd old %s:%s for slaughter (can't reproduce)", age / 12 / 28, (age / 28) % 12, age % 28, race->creature_id.c_str(), cst->caste_id.c_str()));
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
        flags.bits.milkable = 0;
        flags.bits.shearable = 0;
        flags.bits.hunts_vermin = 0;
        flags.bits.grazer = 0;

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

            if (auto bld = virtual_cast<df::building_civzonest>(ai.plan.getpasture(out, u->id)))
            {
                assign_unit_to_zone(u, bld);
                // TODO monitor grass levels
            }
            else if (u->relationship_ids[df::unit_relationship_type::Pet] == -1 && !cst->flags.is_set(caste_raw_flags::CAN_LEARN))
            {
                // TODO slaughter best candidate, keep this one
                u->flags2.bits.slaughter = 1;
                ai.debug(out, stl_sprintf("marked %dy%dm%dd old %s:%s for slaughter (no pasture)", age / 12 / 28, (age / 28) % 12, age % 28, race->creature_id.c_str(), cst->caste_id.c_str()));
            }
        }

        pet[u->id] = flags;
    }

    for (auto p : np)
    {
        ai.plan.freepasture(out, p.first);
        pet.erase(p.first);
    }

    for (auto & cst : forSlaughter)
    {
        // we have reproductively viable animals, but there are more than 3 of
        // this sex (full-grown). kill the oldest ones for meat/leather/bones.

        if (cst.second.size() > 3)
        {
            // remove the youngest 3
            auto it = cst.second.begin();
            std::advance(it, 3);
            cst.second.erase(cst.second.begin(), it);

            for (auto it : cst.second)
            {
                int32_t age = it.first;
                df::unit *u = it.second;
                df::creature_raw *race = df::creature_raw::find(u->race);
                u->flags2.bits.slaughter = 1;
                ai.debug(out, stl_sprintf("marked %dy%dm%dd old %s:%s for slaughter (too many adults)", age / 12 / 28, (age / 28) % 12, age % 28, race->creature_id.c_str(), cst.first->caste_id.c_str()));
            }
        }
    }

    if (needmilk > 0)
    {
        df::manager_order_template tmpl;
        tmpl.job_type = job_type::MilkCreature;
        tmpl.mat_index = -1;

        ai.stocks.add_manager_order(out, tmpl, std::min(needmilk, 30));
    }

    if (needshear > 0)
    {
        df::manager_order_template tmpl;
        tmpl.job_type = job_type::ShearCreature;
        tmpl.mat_index = -1;

        ai.stocks.add_manager_order(out, tmpl, std::min(needshear, 30));
    }

    for (auto bld : world->buildings.other[buildings_other_id::NEST_BOX])
    {
        auto box = virtual_cast<df::building_nest_boxst>(bld);
        if (!box || box->getBuildStage() != box->getMaxBuildStage())
        {
            continue;
        }

        if (box->claimed_by == -1)
        {
            continue;
        }

        for (auto item : box->contained_items)
        {
            if (auto egg = virtual_cast<df::item_eggst>(item->item))
            {
                if (egg->egg_flags.bits.fertile)
                {
                    // baby chicks are preferable over cooked eggs.
                    egg->flags.bits.forbid = true;
                }
            }
        }
    }
}

void Population::assign_unit_to_zone(df::unit *u, df::building_civzonest *bld)
{
    // FIXME: this should be an ExclusiveCallback

    if (auto ref = Units::getGeneralRef(u, general_ref_type::BUILDING_CIVZONE_ASSIGNED))
    {
        if (ref->getBuilding() == bld)
        {
            // already assigned to the correct zone
            return;
        }
    }

    int32_t start_x, start_y, start_z;
    Gui::getViewCoords(start_x, start_y, start_z);
    Gui::getCurViewscreen(true)->feed_key(interface_key::D_CIVZONE);
    if (ui->main.mode != ui_sidebar_mode::Zones)
    {
        // we probably aren't on the main dwarf fortress screen
        return;
    }
    Gui::revealInDwarfmodeMap(df::coord(bld->x1 + 1, bld->y1, bld->z), true);
    Gui::setCursorCoords(bld->x1 + 1, bld->y1, bld->z);
    Gui::getCurViewscreen(true)->feed_key(interface_key::CURSOR_LEFT);
    while (ui_sidebar_menus->zone.selected != bld)
    {
        Gui::getCurViewscreen(true)->feed_key(interface_key::CIVZONE_NEXT);
    }
    if (Buildings::isPitPond(bld))
    {
        Gui::getCurViewscreen(true)->feed_key(interface_key::CIVZONE_POND_OPTIONS);
    }
    else if (Buildings::isPenPasture(bld))
    {
        Gui::getCurViewscreen(true)->feed_key(interface_key::CIVZONE_PEN_OPTIONS);
    }
    if (std::find(ui_building_assign_units->begin(), ui_building_assign_units->end(), u) != ui_building_assign_units->end())
    {
        while (ui_building_assign_units->at(*ui_building_item_cursor) != u)
        {
            Gui::getCurViewscreen(true)->feed_key(interface_key::SECONDSCROLL_DOWN);
        }
        Gui::getCurViewscreen(true)->feed_key(interface_key::SELECT);
    }
    Gui::getCurViewscreen(true)->feed_key(interface_key::LEAVESCREEN);
    Gui::getCurViewscreen(true)->feed_key(interface_key::LEAVESCREEN);
    ai.ignore_pause(start_x, start_y, start_z);
}

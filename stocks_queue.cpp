#include "ai.h"
#include "stocks.h"
#include "plan.h"

#include "Error.h"

#include "modules/Units.h"

#include "df/building_furnacest.h"
#include "df/building_workshopst.h"
#include "df/buildings_other_id.h"
#include "df/caste_raw.h"
#include "df/creature_raw.h"
#include "df/itemdef_trapcompst.h"
#include "df/manager_order.h"
#include "df/manager_order_template.h"
#include "df/unit_relationship_type.h"
#include "df/viewscreen_dwarfmodest.h"
#include "df/world.h"

REQUIRE_GLOBAL(cur_year);
REQUIRE_GLOBAL(world);

class MasonChairJobExclusive : public ExclusiveCallback
{
    AI & ai;
    int32_t wanted_amount;

public:
    MasonChairJobExclusive(AI& ai, int32_t amount) :
        ExclusiveCallback("assign chair construction at mason's workshop", 2),
        ai(ai),
        wanted_amount(amount)
    {
    }

protected:
    void Run(color_ostream & out)
    {
        int32_t start_x, start_y, start_z;
        Gui::getViewCoords(start_x, start_y, start_z);

        if (auto workshop = ai.find_room(room_type::workshop, [](room* r) -> bool
            {
                if (r->workshop_type != workshop_type::Masons)
                {
                    return false;
                }

                auto bld = r->dfbuilding();
                return bld && bld->getBuildStage() == bld->getMaxBuildStage();
            }))
        {
            auto bld = virtual_cast<df::building_workshopst>(workshop->dfbuilding());
            int32_t wanted = min(max(wanted_amount - int(bld->jobs.size()), 0), 10 - int(bld->jobs.size()));

            if (wanted > 0)
            {
                Key(interface_key::D_BUILDJOB);
                Gui::setCursorCoords(workshop->min.x, workshop->min.y, workshop->min.z);
                Key(interface_key::CURSOR_DOWNRIGHT);

                ai.debug(out, stl_sprintf("queueing %d chairs directly at ", wanted) + ai.describe_room(workshop));
                while (wanted > 0)
                {
                    Key(interface_key::BUILDJOB_ADD);
                    Key(interface_key::HOTKEY_MASON_CHAIR);
                    wanted--;
                }

                Key(interface_key::LEAVESCREEN);
            }
        }
        else
        {
            ai.debug(out, "could not find mason's workshop");
        }

        ai.ignore_pause(start_x, start_y, start_z);
    }
};

// make it so the stocks of 'what' rises by 'amount'
void Stocks::queue_need(color_ostream & out, stock_item::item what, int32_t amount, std::ostream & reason)
{
    if (amount <= 0)
    {
        reason << "have enough " << what;
        return;
    }

    df::manager_order_template tmpl;
    tmpl.mat_index = -1;
    std::vector<stock_item::item> input;

    switch (what)
    {
    case stock_item::ammo_combat:
    {
        queue_need_ammo(out, reason);
        return;
    }
    case stock_item::ammo_training:
    {
        amount = (amount + 24) / 25;
        tmpl.job_type = job_type::MakeAmmo;
        tmpl.item_subtype = min_subtype_for_item(stock_item::ammo_training);
        tmpl.material_category.bits.wood = true;
        break;
    }
    case stock_item::anvil:
    {
        queue_need_anvil(out, reason);
        return;
    }
    case stock_item::armor_feet:
    case stock_item::armor_hands:
    case stock_item::armor_head:
    case stock_item::armor_legs:
    case stock_item::armor_shield:
    case stock_item::armor_torso:
    {
        queue_need_armor(out, what, reason);
        return;
    }
    case stock_item::armor_stand:
    {
        tmpl.job_type = job_type::ConstructArmorStand;
        tmpl.mat_type = 0;
        break;
    }
    case stock_item::ash:
    {
        tmpl.job_type = job_type::MakeAsh;
        input.push_back(stock_item::wood);
        break;
    }
    case stock_item::axe:
    {
        queue_need_weapon(out, what, num_needed(what), reason, job_skill::AXE);
        return;
    }
    case stock_item::backpack:
    {
        tmpl.job_type = job_type::MakeBackpack;
        tmpl.material_category.bits.leather = 1;
        break;
    }
    case stock_item::bag:
    {
        tmpl.job_type = job_type::ConstructChest;
        tmpl.material_category.bits.cloth = 1;
        break;
    }
    case stock_item::barrel:
    {
        tmpl.job_type = job_type::MakeBarrel;
        tmpl.material_category.bits.wood = 1;
        break;
    }
    case stock_item::bed:
    {
        tmpl.job_type = job_type::ConstructBed;
        tmpl.material_category.bits.wood = 1;
        break;
    }
    case stock_item::bin:
    {
        tmpl.job_type = job_type::ConstructBin;
        tmpl.material_category.bits.wood = 1;
        break;
    }
    case stock_item::block:
    {
        tmpl.job_type = job_type::ConstructBlocks;
        amount = (amount + 3) / 4;

        // no stone => make wooden blocks (needed for pumps for aquifer handling)
        if (!count_free.count(stock_item::stone) || !count_free.at(stock_item::stone))
        {
            if (amount > 2)
            {
                amount = 2;
            }
            tmpl.material_category.bits.wood = 1;
        }
        else
        {
            tmpl.mat_type = 0;
        }
        break;
    }
    case stock_item::bone:
    {
        for (auto u : world->units.active)
        {
            if (u->flags2.bits.slaughter)
            {
                reason << "marked for slaughter: " << AI::describe_unit(u);
                return;
            }
        }

        df::unit *biggest_pet = nullptr;
        int32_t biggest_pet_size = 0;
        bool biggest_pet_useful = true;
        for (auto & pet : ai.pop.pet)
        {
            if (auto u = df::unit::find(pet.first))
            {
                if (u->relationship_ids[unit_relationship_type::Pet] != -1)
                {
                    continue;
                }

                int32_t pet_size = u->body.size_info.size_cur;
                if (pet.second.bits.hunts_vermin || pet.second.bits.trainable || pet.second.bits.lays_eggs || pet.second.bits.milkable || pet.second.bits.shearable)
                {
                    if (!biggest_pet_useful)
                    {
                        continue;
                    }

                    if (biggest_pet_size < pet_size)
                    {
                        biggest_pet = u;
                        biggest_pet_size = pet_size;
                    }
                }
                else
                {
                    if (biggest_pet_size < pet_size || biggest_pet_useful)
                    {
                        biggest_pet = u;
                        biggest_pet_size = pet_size;
                        biggest_pet_useful = false;
                    }
                }
            }
        }
        if (biggest_pet)
        {
            biggest_pet->flags2.bits.slaughter = true;
            reason << "marked for slaughter: " << AI::describe_unit(biggest_pet);
            int32_t age = ai.pop.days_since(biggest_pet->birth_year, biggest_pet->birth_time);
            auto race = df::creature_raw::find(biggest_pet->race);
            auto cst = race->caste.at(biggest_pet->caste);
            ai.debug(out, stl_sprintf("marked %dy%dm%dd old %s:%s for slaughter (need bones)", age / 12 / 28, (age / 28) % 12, age % 28, race->creature_id.c_str(), cst->caste_id.c_str()));
        }
        return;
    }
    case stock_item::book_binding:
    {
        tmpl.job_type = job_type::MakeTool;
        tmpl.item_subtype = min_subtype_for_item(stock_item::book_binding);
        tmpl.mat_type = 0;
        break;
    }
    case stock_item::bookcase:
    {
        tmpl.job_type = job_type::MakeTool;
        tmpl.item_subtype = min_subtype_for_item(stock_item::bookcase);
        tmpl.mat_type = 0;
        break;
    }
    case stock_item::bucket:
    {
        tmpl.job_type = job_type::MakeBucket;
        tmpl.material_category.bits.wood = 1;
        break;
    }
    case stock_item::cabinet:
    {
        tmpl.job_type = job_type::ConstructCabinet;
        tmpl.mat_type = 0;
        break;
    }
    case stock_item::cage:
    {
        tmpl.job_type = job_type::MakeCage;
        tmpl.material_category.bits.wood = 1;
        break;
    }
    case stock_item::cage_metal:
    {
        queue_need_cage(out, reason);
        return;
    }
    case stock_item::chair:
    {
        tmpl.job_type = job_type::ConstructThrone;
        tmpl.mat_type = 0;
        if (ai.find_room(room_type::nobleroom, [](room* r) -> bool
            {
                if (r->nobleroom_type != nobleroom_type::office || r->dfbuilding())
                {
                    return false;
                }

                if (auto owner = df::unit::find(r->owner))
                {
                    std::vector<Units::NoblePosition> positions;
                    if (Units::getNoblePositions(&positions, owner))
                    {
                        for (auto pos : positions)
                        {
                            if (pos.position->responsibilities[entity_position_responsibility::MANAGE_PRODUCTION])
                            {
                                return true;
                            }
                        }
                    }
                }

                return false;
            }))
        {
            // the manager doesn't have an office, which requires a chair.
            // assign the job directly at the workshop.

            if (ai.find_room(room_type::workshop, [](room* r) -> bool
                {
                    if (r->workshop_type != workshop_type::Masons)
                    {
                        return false;
                    }

                    auto bld = r->dfbuilding();
                    return bld && bld->getBuildStage() == bld->getMaxBuildStage();
                }))
            {
                reason << "assigning job directly at mason's workshop as the manager has no office";
                events.queue_exclusive(std::make_unique<MasonChairJobExclusive>(ai, amount));
                return;
            }

            // we're screwed. for now, at least.
            reason << "manager does not have an office and no mason workshop available";
            return;
        }
        break;
    }
    case stock_item::chest:
    {
        tmpl.job_type = job_type::ConstructChest;
        tmpl.mat_type = 0;
        break;
    }
    case stock_item::clothes_feet:
    case stock_item::clothes_hands:
    case stock_item::clothes_head:
    case stock_item::clothes_legs:
    case stock_item::clothes_torso:
    {
        queue_need_clothes(out, what, reason);
        return;
    }
    case stock_item::coal:
    {
        tmpl.job_type = job_type::MakeCharcoal;
        // dont use wood -> charcoal if we have bituminous coal
        // (except for bootstraping)
        if (amount > 2 - count_free.at(stock_item::coal) && count_free.at(stock_item::raw_coke) > Watch.WatchStock.at(stock_item::raw_coke))
        {
            amount = 2 - count_free.at(stock_item::coal);
            if (amount <= 0)
            {
                reason << "using raw coke instead of making charcoal";
            }
        }
        break;
    }
    case stock_item::coffin:
    {
        tmpl.job_type = job_type::ConstructCoffin;
        tmpl.mat_type = 0;
        break;
    }
    case stock_item::crafts:
    {
        tmpl.job_type = job_type::MakeCrafts;
        if (count_free.at(stock_item::stone) > count_free.at(stock_item::wood))
        {
            tmpl.mat_type = 0;
        }
        else
        {
            tmpl.material_category.bits.wood = 1;
        }
        break;
    }
    case stock_item::crutch:
    {
        tmpl.job_type = job_type::ConstructCrutch;
        tmpl.material_category.bits.wood = 1;
        break;
    }
    case stock_item::die:
    {
        tmpl.job_type = job_type::MakeTool;
        tmpl.item_subtype = min_subtype_for_item(stock_item::die);
        tmpl.mat_type = 0;
        break;
    }
    case stock_item::door:
    {
        tmpl.job_type = job_type::ConstructDoor;
        tmpl.mat_type = 0;
        break;
    }
    case stock_item::drink:
    {
        std::map<stock_item::item, std::string> reactions;
        reactions[stock_item::drink_plant] = "BREW_DRINK_FROM_PLANT";
        reactions[stock_item::drink_fruit] = "BREW_DRINK_FROM_PLANT_GROWTH";
        reactions[stock_item::honey] = "MAKE_MEAD";
        auto score = [this, &out](std::pair<const stock_item::item, std::string> i) -> int32_t
        {
            int32_t c = count_free.at(i.first);
            df::manager_order_template count_tmpl;
            count_tmpl.job_type = job_type::CustomReaction;
            count_tmpl.reaction_name = i.second;
            c -= count_manager_orders(out, count_tmpl);
            return c;
        };
        auto max = std::max_element(reactions.begin(), reactions.end(), [score](std::pair<const stock_item::item, std::string> a, std::pair<const stock_item::item, std::string> b) -> bool { return score(a) < score(b); });
        tmpl.job_type = job_type::CustomReaction;
        tmpl.reaction_name = max->second;
        input.push_back(max->first);
        if (count_free[stock_item::barrel] > count_free[stock_item::food_storage])
        {
            input.push_back(stock_item::barrel);
        }
        else
        {
            input.push_back(stock_item::food_storage);
        }
        amount = (amount + 4) / 5; // accounts for brewer yield, but not for input stack size
        break;
    }
    case stock_item::dye:
    {
        tmpl.job_type = job_type::MillPlants;
        input.push_back(stock_item::dye_plant);
        input.push_back(stock_item::bag);
        break;
    }
    case stock_item::dye_seeds:
    {
        tmpl.job_type = job_type::MillPlants;
        input.push_back(stock_item::dye_plant);
        input.push_back(stock_item::bag);
        break;
    }
    case stock_item::flask:
    {
        tmpl.job_type = job_type::MakeFlask;
        tmpl.material_category.bits.leather = 1;
        break;
    }
    case stock_item::floodgate:
    {
        tmpl.job_type = job_type::ConstructFloodgate;
        tmpl.mat_type = 0;
        break;
    }
    case stock_item::food_storage:
    {
        tmpl.job_type = job_type::MakeTool;
        tmpl.item_subtype = min_subtype_for_item(stock_item::food_storage);
        tmpl.mat_type = 0;
        break;
    }
    case stock_item::goblet:
    {
        tmpl.job_type = job_type::MakeGoblet;
        tmpl.mat_type = 0;
        break;
    }
    case stock_item::gypsum:
    {
        if (ai.plan.should_search_for_metal)
        {
            for (auto & vein : ai.plan.map_veins)
            {
                if (is_gypsum(vein.first))
                {
                    if (ai.plan.dig_vein(out, vein.first, amount))
                    {
                        reason << "marked " << MaterialInfo(0, vein.first).toString() << " vein for excavation";
                        return;
                    }
                }
            }
        }
        reason << "could not find gypsum vein";
        return;
    }
    case stock_item::hatch_cover:
    {
        tmpl.job_type = job_type::ConstructHatchCover;
        tmpl.mat_type = 0;
        break;
    }
    case stock_item::honey:
    {
        tmpl.job_type = job_type::CustomReaction;
        tmpl.reaction_name = "PRESS_HONEYCOMB";
        input.push_back(stock_item::honeycomb);
        input.push_back(stock_item::jug);
        break;
    }
    case stock_item::hive:
    {
        tmpl.job_type = job_type::MakeTool;
        tmpl.item_subtype = min_subtype_for_item(stock_item::hive);
        tmpl.mat_type = 0;
        break;
    }
    case stock_item::jug:
    {
        tmpl.job_type = job_type::MakeTool;
        tmpl.item_subtype = min_subtype_for_item(stock_item::jug);
        tmpl.mat_type = 0;
        break;
    }
    case stock_item::lye:
    {
        tmpl.job_type = job_type::MakeLye;
        input.push_back(stock_item::ash);
        input.push_back(stock_item::bucket);
        break;
    }
    case stock_item::meal:
    {
        // XXX fish/hunt/cook ?
        if (last_warn_food_year != *cur_year)
        {
            ai.debug(out, stl_sprintf("need %d more food", amount));
            last_warn_food_year = *cur_year;
        }
        reason << "waiting for ingredients and cooks";
        return;
    }
    case stock_item::mechanism:
    {
        tmpl.job_type = job_type::ConstructMechanisms;
        tmpl.mat_type = 0;
        break;
    }
    case stock_item::minecart:
    {
        tmpl.job_type = job_type::MakeTool;
        tmpl.item_subtype = min_subtype_for_item(stock_item::minecart);
        tmpl.material_category.bits.wood = 1;
        break;
    }
    case stock_item::nest_box:
    {
        tmpl.job_type = job_type::MakeTool;
        tmpl.item_subtype = min_subtype_for_item(stock_item::nest_box);
        tmpl.mat_type = 0;
        break;
    }
    case stock_item::offering_place:
    {
        tmpl.job_type = job_type::MakeTool;
        tmpl.item_subtype = min_subtype_for_item(stock_item::offering_place);
        tmpl.mat_type = 0;
        break;
    }
    case stock_item::paper:
    {
        tmpl.job_type = job_type::CustomReaction;
        tmpl.reaction_name = "PRESS_PLANT_PAPER";
        input.push_back(stock_item::slurry);
        break;
    }
    case stock_item::pedestal:
    {
        tmpl.job_type = job_type::MakeTool;
        tmpl.item_subtype = min_subtype_for_item(stock_item::pedestal);
        tmpl.mat_type = 0;
        break;
    }
    case stock_item::pick:
    {
        queue_need_weapon(out, what, num_needed(what), reason, job_skill::MINING, false, false, true);
        return;
    }
    case stock_item::pipe_section:
    {
        tmpl.job_type = job_type::MakePipeSection;
        tmpl.material_category.bits.wood = 1;
        break;
    }
    case stock_item::plaster_powder:
    {
        tmpl.job_type = job_type::CustomReaction;
        tmpl.reaction_name = "MAKE_PLASTER_POWDER";
        input.push_back(stock_item::gypsum);
        input.push_back(stock_item::bag);
        break;
    }
    case stock_item::potash:
    {
        tmpl.job_type = job_type::MakePotashFromAsh;
        input.push_back(stock_item::ash);
        break;
    }
    case stock_item::quern:
    {
        tmpl.job_type = job_type::ConstructQuern;
        tmpl.mat_type = 0;
        break;
    }
    case stock_item::quire:
    {
        tmpl.job_type = job_type::CustomReaction;
        tmpl.reaction_name = "MAKE_QUIRE";
        input.push_back(stock_item::paper);
        break;
    }
    case stock_item::quiver:
    {
        tmpl.job_type = job_type::MakeQuiver;
        tmpl.material_category.bits.leather = 1;
        break;
    }
    case stock_item::raw_coke:
    {
        if (ai.plan.should_search_for_metal)
        {
            for (auto vein : ai.plan.map_veins)
            {
                if (!is_raw_coke(vein.first).empty())
                {
                    if (ai.plan.dig_vein(out, vein.first, amount))
                    {
                        reason << "marked " << MaterialInfo(0, vein.first).toString() << " vein for excavation";
                        return;
                    }
                }
            }
        }
        reason << "could not find raw coke vein";
        return;
    }
    case stock_item::rope:
    {
        tmpl.job_type = job_type::MakeChain;
        tmpl.material_category.bits.cloth = 1;
        break;
    }
    case stock_item::screw:
    {
        tmpl.job_type = job_type::MakeTrapComponent;
        tmpl.item_subtype = min_subtype_for_item(stock_item::screw);
        tmpl.material_category.bits.wood = 1;
        break;
    }
    case stock_item::slab:
    {
        tmpl.job_type = job_type::ConstructSlab;
        tmpl.mat_type = 0;
        break;
    }
    case stock_item::slurry:
    {
        tmpl.job_type = job_type::CustomReaction;
        tmpl.reaction_name = "MAKE_SLURRY_FROM_PLANT";
        input.push_back(stock_item::slurry_plant);
        break;
    }
    case stock_item::soap:
    {
        tmpl.job_type = job_type::CustomReaction;
        tmpl.reaction_name = "MAKE_SOAP_FROM_TALLOW";
        input.push_back(stock_item::lye);
        input.push_back(stock_item::tallow);
        break;
    }
    case stock_item::splint:
    {
        tmpl.job_type = job_type::ConstructSplint;
        tmpl.material_category.bits.wood = 1;
        break;
    }
    case stock_item::stepladder:
    {
        tmpl.job_type = job_type::MakeTool;
        tmpl.item_subtype = min_subtype_for_item(stock_item::stepladder);
        tmpl.material_category.bits.wood = 1;
        break;
    }
    case stock_item::table:
    {
        tmpl.job_type = job_type::ConstructTable;
        tmpl.mat_type = 0;
        break;
    }
    case stock_item::thread_seeds:
    {
        // only useful at game start, with low seeds stocks
        tmpl.job_type = job_type::ProcessPlants;
        input.push_back(stock_item::thread_plant);
        break;
    }
    case stock_item::toy:
    {
        tmpl.job_type = job_type::MakeToy;
        tmpl.mat_type = 0;
        break;
    }
    case stock_item::traction_bench:
    {
        tmpl.job_type = job_type::ConstructTractionBench;
        input.push_back(stock_item::table);
        input.push_back(stock_item::rope);
        input.push_back(stock_item::mechanism);
        break;
    }
    case stock_item::weapon_melee:
    {
        queue_need_weapon(out, what, num_needed(what), reason);
        return;
    }
    case stock_item::weapon_rack:
    {
        tmpl.job_type = job_type::ConstructWeaponRack;
        tmpl.mat_type = 0;
        break;
    }
    case stock_item::weapon_ranged:
    {
        queue_need_weapon(out, what, num_needed(what), reason, job_skill::NONE, false, true);
        return;
    }
    case stock_item::weapon_training:
    {
        queue_need_weapon(out, what, num_needed(what), reason, job_skill::NONE, true);
        return;
    }
    case stock_item::wheelbarrow:
    {
        tmpl.job_type = job_type::MakeTool;
        tmpl.item_subtype = min_subtype_for_item(stock_item::wheelbarrow);
        tmpl.material_category.bits.wood = 1;
        break;
    }
    case stock_item::wood:
    {
        amount *= 2;
        if (amount > 30)
            amount = 30;

        last_cutpos = cuttrees(out, amount / 6 + 1, reason);

        return;
    }
    default:
    {
        throw DFHack::Error::InvalidArgument();
    }
    }

    if (amount > 30)
        amount = 30;

    int32_t i_amount = amount;
    if (!input.empty())
    {
        for (auto i : input)
        {
            int32_t c = count_free.at(i);
            if (c < amount)
            {
                reason << "not enough " << i;
                i_amount = std::min(i_amount, c);
                if (Watch.Needed.count(i))
                {
                    reason << ": ";
                    queue_need(out, i, amount - c, reason);
                }
                reason << "\n";
            }
        }
    }

    if (tmpl.material_category.whole != 0)
    {
        stock_item::item matcat_item;
        if (tmpl.material_category.bits.wood)
        {
            matcat_item = stock_item::wood;
            if (what != stock_item::bed && need_more(stock_item::bed))
            {
                reason << "need more beds";
                return;
            }
        }
        else if (tmpl.material_category.bits.cloth)
        {
            matcat_item = stock_item::cloth;
        }
        else if (tmpl.material_category.bits.leather)
        {
            matcat_item = stock_item::leather;
        }
        else if (tmpl.material_category.bits.bone)
        {
            matcat_item = stock_item::bone;
        }
        else if (tmpl.material_category.bits.shell)
        {
            matcat_item = stock_item::shell;
        }
        else
        {
            throw DFHack::Error::InvalidArgument();
        }

        int32_t mat_amount = count_free.at(matcat_item) - count_manager_orders_matcat(tmpl.material_category, tmpl.job_type);
        if (mat_amount < amount)
        {
            reason << "not enough " << matcat_item;
            if (Watch.Needed.count(matcat_item))
            {
                reason << ": ";
                queue_need(out, matcat_item, amount - mat_amount, reason);
            }
            reason << "\n";
            i_amount = std::min(i_amount, mat_amount);
        }
    }

    amount = i_amount;

    add_manager_order(out, tmpl, amount, reason);
}

int16_t Stocks::min_subtype_for_item(stock_item::item what)
{
    const auto & count = count_subtype.at(what);
    auto min = std::min_element(count.begin(), count.end(), [](std::pair<const int16_t, std::pair<int32_t, int32_t>> a, std::pair<const int16_t, std::pair<int32_t, int32_t>> b) -> bool
    {
        return a.second.first < b.second.first;
    });

    return min == count.end() ? -1 : min->first;
}

// make it so the stocks of 'what' decrease by 'amount'
void Stocks::queue_use(color_ostream & out, stock_item::item what, int32_t amount, std::ostream & reason)
{
    if (amount <= 0)
    {
        reason << "amount not above threshold";
        return;
    }

    df::manager_order_template tmpl;
    tmpl.mat_index = -1;
    std::vector<stock_item::item> input;

    // stuff may rot/be brewed before we can process it
    auto may_rot = [&amount]()
    {
        if (amount > 10)
            amount /= 2;
        if (amount > 4)
            amount /= 2;
    };

    switch (what)
    {
    case stock_item::bag_plant:
    {
        tmpl.job_type = job_type::CustomReaction;
        tmpl.reaction_name = "PROCESS_PLANT_TO_BAG";
        may_rot();
        input.push_back(stock_item::bag);
        break;
    }
    case stock_item::bone:
    {
        if (need_more(stock_item::weapon_ranged))
        {
            int32_t need_crossbow = num_needed(stock_item::weapon_ranged) - count_free.at(stock_item::weapon_ranged);
            tmpl.job_type = job_type::MakeWeapon;
            tmpl.item_subtype = min_subtype_for_item(stock_item::weapon_ranged);
            tmpl.material_category.bits.bone = 1;
            if (amount > need_crossbow)
            {
                amount = need_crossbow;
            }
        }
        else
        {
            tmpl.job_type = job_type::MakeAmmo;
            tmpl.item_subtype = min_subtype_for_item(stock_item::ammo_training);
            tmpl.material_category.bits.bone = 1;
            may_rot();
        }
        break;
    }
    case stock_item::clay:
    {
        bool magma_kiln = false;
        for (auto kiln : world->buildings.other[buildings_other_id::FURNACE_KILN_ANY])
        {
            if (virtual_cast<df::building_furnacest>(kiln)->type == furnace_type::MagmaKiln)
            {
                magma_kiln = true;
                break;
            }
        }
        if (!magma_kiln)
        {
            input.push_back(stock_item::coal);
        }
        tmpl.job_type = job_type::CustomReaction;
        tmpl.reaction_name = "MAKE_CLAY_STATUE";
        break;
    }
    case stock_item::cloth_nodye:
    {
        tmpl.job_type = job_type::DyeCloth;
        input.push_back(stock_item::dye);
        may_rot();
        break;
    }
    case stock_item::drink_fruit:
    case stock_item::drink_plant:
    {
        tmpl.job_type = job_type::CustomReaction;
        tmpl.reaction_name = what == stock_item::drink_plant ? "BREW_DRINK_FROM_PLANT" : "BREW_DRINK_FROM_PLANT_GROWTH";
        may_rot();

        if (count_free[stock_item::barrel] > count_free[stock_item::food_storage])
        {
            input.push_back(stock_item::barrel);
        }
        else
        {
            input.push_back(stock_item::food_storage);
        }

        if (!need_more(stock_item::drink) && (need_more(stock_item::meal) || (need_more(stock_item::barrel) && need_more(stock_item::food_storage))))
        {
            reason << "have enough drinks";
            return;
        }
        break;
    }
    case stock_item::food_ingredients:
    {
        tmpl.job_type = job_type::PrepareMeal;
        tmpl.mat_type = 4; // roasts
        amount = (amount + 4) / 5;
        if (!need_more(stock_item::meal) && (need_more(stock_item::drink) || (need_more(stock_item::barrel) && need_more(stock_item::food_storage))))
        {
            reason << "have enough meals";
            return;
        }
        break;
    }
    case stock_item::goblinite:
    {
        // make coke from bituminous coal has priority if there isn't a magma smelter
        if (world->buildings.other[buildings_other_id::FURNACE_SMELTER_MAGMA].empty() &&
            count_free.at(stock_item::raw_coke) > Watch.WatchStock.at(stock_item::raw_coke) &&
            count_free.at(stock_item::coal) < 100)
        {
            reason << "making coal instead";
            return;
        }

        tmpl.job_type = job_type::MeltMetalObject;
        if (world->buildings.other[buildings_other_id::FURNACE_SMELTER_MAGMA].empty())
        {
            input.push_back(stock_item::coal);
        }
        break;
    }
    case stock_item::honey:
    {
        tmpl.job_type = job_type::CustomReaction;
        tmpl.reaction_name = "MAKE_MEAD";
        if (count_free[stock_item::barrel] > count_free[stock_item::food_storage])
        {
            input.push_back(stock_item::barrel);
        }
        else
        {
            input.push_back(stock_item::food_storage);
        }
        break;
    }
    case stock_item::honeycomb:
    {
        tmpl.job_type = job_type::CustomReaction;
        tmpl.reaction_name = "PRESS_HONEYCOMB";
        input.push_back(stock_item::jug);
        break;
    }
    case stock_item::metal_ore:
    {
        queue_use_metal_ore(out, amount, reason);
        return;
    }
    case stock_item::metal_strand:
    {
        auto candy = find_free_item(stock_item::metal_strand);
        if (!candy)
        {
            reason << "could not find metal";
            return;
        }
        tmpl.job_type = job_type::ExtractMetalStrands;
        tmpl.mat_type = 0;
        tmpl.mat_index = candy->getMaterialIndex();
        break;
    }
    case stock_item::milk:
    {
        tmpl.job_type = job_type::MakeCheese;
        break;
    }
    case stock_item::mill_plant:
    {
        tmpl.job_type = job_type::MillPlants;
        may_rot();
        input.push_back(stock_item::bag);
        break;
    }
    case stock_item::raw_coke:
    {
        queue_use_raw_coke(out, amount, reason);
        return;
    }
    case stock_item::raw_fish:
    {
        tmpl.job_type = job_type::PrepareRawFish;
        may_rot();
        break;
    }
    case stock_item::rough_gem:
    {
        queue_use_gems(out, amount, reason);
        return;
    }
    case stock_item::shell:
    {
        tmpl.job_type = job_type::DecorateWith;
        tmpl.material_category.bits.shell = 1;
        may_rot();
        break;
    }
    case stock_item::skull:
    {
        tmpl.job_type = job_type::MakeTotem;
        may_rot();
        break;
    }
    case stock_item::tallow:
    {
        tmpl.job_type = job_type::CustomReaction;
        tmpl.reaction_name = "MAKE_SOAP_FROM_TALLOW";
        input.push_back(stock_item::lye);
        if (!need_more(stock_item::soap))
        {
            reason << "have enough soap";
            return;
        }
        break;
    }
    case stock_item::thread_plant:
    {
        tmpl.job_type = job_type::ProcessPlants;
        may_rot();
        break;
    }
    case stock_item::wool:
    {
        tmpl.job_type = job_type::SpinThread;
        tmpl.material_category.bits.strand = 1;
        break;
    }
    case stock_item::written_on_quire:
    {
        tmpl.job_type = job_type::CustomReaction;
        tmpl.reaction_name = "BIND_BOOK";
        input.push_back(stock_item::book_binding);
        input.push_back(stock_item::thread);
        break;
    }
    default:
    {
        throw DFHack::Error::InvalidArgument();
    }
    }

    if (amount > 30)
        amount = 30;

    if (!input.empty())
    {
        int32_t i_amount = amount;
        for (auto i : input)
        {
            int32_t c = count_free.at(i);
            if (i_amount > c)
                i_amount = c;

            if (c < amount)
            {
                reason << "not enough " << i;
                if (Watch.Needed.count(i))
                {
                    reason << ": ";
                    queue_need(out, i, amount - c, reason);
                }
                reason << "\n";
            }
        }

        amount = i_amount;
    }

    add_manager_order(out, tmpl, amount, reason);
}

// cut gems
void Stocks::queue_use_gems(color_ostream & out, int32_t amount, std::ostream & reason)
{
    for (auto mo : world->manager_orders)
    {
        if (mo->job_type == job_type::CutGems)
        {
            reason << "there is already a manager order to cut gems: " << AI::describe_job(mo) << " (" << mo->amount_left << " remaining)";
            return;
        }
    }
    if (events.each_exclusive<ManagerOrderExclusive>([&reason](const ManagerOrderExclusive *excl) -> bool
    {
        if (excl->tmpl.job_type == job_type::CutGems)
        {
            reason << "there is already a manager order to cut gems: " << AI::describe_job(&excl->tmpl) << " (" << excl->amount << " remaining)";
            return true;
        }
        return false;
    }))
    {
        return;
    }

    df::item *base = nullptr;
    for (auto i : world->items.other[items_other_id::ROUGH])
    {
        if (i->getMaterial() == 0 && is_item_free(i))
        {
            base = i;
            break;
        }
    }
    if (!base)
    {
        reason << "could not find rough gems to cut";
        return;
    }

    int32_t this_amount = 0;
    for (auto i : world->items.other[items_other_id::ROUGH])
    {
        if (i->getMaterial() == base->getMaterial() && i->getMaterialIndex() == base->getMaterialIndex() && is_item_free(i))
        {
            this_amount++;
        }
    }

    if (amount > this_amount)
        amount = this_amount;
    if (amount >= 10)
        amount = amount * 3 / 4;
    if (amount > 30)
        amount = 30;

    df::manager_order_template tmpl;
    tmpl.job_type = job_type::CutGems;
    tmpl.item_type = item_type::NONE;
    tmpl.item_subtype = -1;
    tmpl.mat_type = base->getMaterial();
    tmpl.mat_index = base->getMaterialIndex();

    add_manager_order(out, tmpl, amount, reason);
}

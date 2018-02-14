#include "ai.h"
#include "stocks.h"
#include "plan.h"

#include "Error.h"

#include "modules/Units.h"

#include "df/manager_order.h"
#include "df/manager_order_template.h"
#include "df/world.h"

REQUIRE_GLOBAL(cur_year);
REQUIRE_GLOBAL(world);

// make it so the stocks of 'what' rises by 'amount'
void Stocks::queue_need(color_ostream & out, stock_item::item what, int32_t amount)
{
    if (amount <= 0)
        return;

    df::manager_order_template tmpl;
    tmpl.mat_index = -1;
    std::vector<stock_item::item> input;

    switch (what)
    {
    case stock_item::anvil:
    {
        queue_need_anvil(out);
        return;
    }
    case stock_item::armor_feet:
    {
        queue_need_armor(out, what, items_other_id::SHOES);
        return;
    }
    case stock_item::armor_hands:
    {
        queue_need_armor(out, what, items_other_id::GLOVES);
        return;
    }
    case stock_item::armor_head:
    {
        queue_need_armor(out, what, items_other_id::HELM);
        return;
    }
    case stock_item::armor_legs:
    {
        queue_need_armor(out, what, items_other_id::PANTS);
        return;
    }
    case stock_item::armor_shield:
    {
        queue_need_armor(out, what, items_other_id::SHIELD);
        return;
    }
    case stock_item::armor_stand:
    {
        tmpl.job_type = job_type::ConstructArmorStand;
        tmpl.mat_type = 0;
        break;
    }
    case stock_item::armor_torso:
    {
        queue_need_armor(out, what, items_other_id::ARMOR);
        return;
    }
    case stock_item::ash:
    {
        tmpl.job_type = job_type::MakeAsh;
        input.push_back(stock_item::wood);
        break;
    }
    case stock_item::axe:
    {
        queue_need_weapon(out, what, num_needed(what), job_skill::AXE);
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
        if (!count.count(stock_item::stone) || !count.at(stock_item::stone))
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
    case stock_item::book_binding:
    {
        tmpl.job_type = job_type::MakeTool;
        tmpl.item_subtype = manager_subtype.at(stock_item::book_binding);
        tmpl.mat_type = 0;
        break;
    }
    case stock_item::bookcase:
    {
        tmpl.job_type = job_type::MakeTool;
        tmpl.item_subtype = manager_subtype.at(stock_item::bookcase);
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
        queue_need_cage(out);
        return;
    }
    case stock_item::chair:
    {
        tmpl.job_type = job_type::ConstructThrone;
        tmpl.mat_type = 0;
        break;
    }
    case stock_item::chest:
    {
        tmpl.job_type = job_type::ConstructChest;
        tmpl.mat_type = 0;
        break;
    }
    case stock_item::clothes_feet:
    {
        queue_need_clothes(out, items_other_id::SHOES);
        return;
    }
    case stock_item::clothes_hands:
    {
        queue_need_clothes(out, items_other_id::GLOVES);
        return;
    }
    case stock_item::clothes_head:
    {
        queue_need_clothes(out, items_other_id::HELM);
        return;
    }
    case stock_item::clothes_legs:
    {
        queue_need_clothes(out, items_other_id::PANTS);
        return;
    }
    case stock_item::clothes_torso:
    {
        queue_need_clothes(out, items_other_id::ARMOR);
        return;
    }
    case stock_item::coal:
    {
        // dont use wood -> charcoal if we have bituminous coal
        // (except for bootstraping)
        if (amount > 2 - count.at(stock_item::coal) && count.at(stock_item::raw_coke) > Watch.WatchStock.at(stock_item::raw_coke))
        {
            amount = 2 - count.at(stock_item::coal);
        }
        break;
    }
    case stock_item::coffin:
    {
        tmpl.job_type = job_type::ConstructCoffin;
        tmpl.mat_type = 0;
        break;
    }
    case stock_item::crutch:
    {
        tmpl.job_type = job_type::ConstructCrutch;
        tmpl.material_category.bits.wood = 1;
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
            int32_t c = count.at(i.first);
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
        if (count[stock_item::barrel] > count[stock_item::rock_pot])
        {
            input.push_back(stock_item::barrel);
        }
        else
        {
            input.push_back(stock_item::rock_pot);
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
    case stock_item::giant_corkscrew:
    {
        tmpl.job_type = job_type::MakeTrapComponent;
        tmpl.item_subtype = manager_subtype.at(stock_item::giant_corkscrew);
        tmpl.material_category.bits.wood = 1;
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
        if (ai->plan->should_search_for_metal)
        {
            for (auto vein : ai->plan->map_veins)
            {
                if (is_gypsum(vein.first))
                {
                    ai->plan->dig_vein(out, vein.first, amount);
                    break;
                }
            }
        }
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
        tmpl.item_subtype = manager_subtype.at(stock_item::hive);
        tmpl.mat_type = 0;
        break;
    }
    case stock_item::jug:
    {
        tmpl.job_type = job_type::MakeTool;
        tmpl.item_subtype = manager_subtype.at(stock_item::jug);
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
            ai->debug(out, stl_sprintf("need %d more food", amount));
            last_warn_food_year = *cur_year;
        }
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
        tmpl.item_subtype = manager_subtype.at(stock_item::minecart);
        tmpl.material_category.bits.wood = 1;
        break;
    }
    case stock_item::nest_box:
    {
        tmpl.job_type = job_type::MakeTool;
        tmpl.item_subtype = manager_subtype.at(stock_item::nest_box);
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
    case stock_item::pick:
    {
        queue_need_weapon(out, what, num_needed(what), job_skill::MINING);
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
        if (ai->plan->should_search_for_metal)
        {
            for (auto vein : ai->plan->map_veins)
            {
                if (!is_raw_coke(vein.first).empty())
                {
                    ai->plan->dig_vein(out, vein.first, amount);
                    break;
                }
            }
        }
        return;
    }
    case stock_item::rock_pot:
    {
        tmpl.job_type = job_type::MakeTool;
        tmpl.item_subtype = manager_subtype.at(stock_item::rock_pot);
        tmpl.mat_type = 0;
        break;
    }
    case stock_item::rope:
    {
        tmpl.job_type = job_type::MakeChain;
        tmpl.material_category.bits.cloth = 1;
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
        tmpl.item_subtype = manager_subtype.at(stock_item::stepladder);
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
    case stock_item::training_weapon:
    {
        queue_need_weapon(out, what, num_needed(what), job_skill::NONE, true);
        return;
    }
    case stock_item::weapon:
    {
        queue_need_weapon(out, what, num_needed(what));
        return;
    }
    case stock_item::weapon_rack:
    {
        tmpl.job_type = job_type::ConstructWeaponRack;
        tmpl.mat_type = 0;
        break;
    }
    case stock_item::wheelbarrow:
    {
        tmpl.job_type = job_type::MakeTool;
        tmpl.item_subtype = manager_subtype.at(stock_item::wheelbarrow);
        tmpl.material_category.bits.wood = 1;
        break;
    }
    case stock_item::wood:
    {
        amount *= 2;
        if (amount > 30)
            amount = 30;

        last_cutpos = cuttrees(out, amount / 6 + 1);

        return;
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
            int32_t c = count.at(i);
            if (c < i_amount)
            {
                i_amount = c;
            }
            if (c < amount && Watch.Needed.count(i))
            {
                queue_need(out, i, amount - c);
            }
        }
        amount = i_amount;
    }

    if (tmpl.material_category.whole != 0)
    {
        stock_item::item matcat_item;
        if (tmpl.material_category.bits.wood)
        {
            matcat_item = stock_item::wood;
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
        int32_t i_amount = count.at(matcat_item) - count_manager_orders_matcat(tmpl.material_category, tmpl.job_type);
        if (i_amount < amount && Watch.Needed.count(matcat_item))
        {
            queue_need(out, matcat_item, amount - i_amount);
        }
        if (amount > i_amount)
        {
            amount = i_amount;
        }
    }

    add_manager_order(out, tmpl, amount);
}

// make it so the stocks of 'what' decrease by 'amount'
void Stocks::queue_use(color_ostream & out, stock_item::item what, int32_t amount)
{
    if (amount <= 0)
        return;

    df::manager_order_template tmpl;
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
        int32_t nhunters = 0;
        for (auto u : world->units.active)
        {
            if (Units::isCitizen(u) && !Units::isDead(u) && u->status.labors[unit_labor::HUNT])
            {
                nhunters++;
            }
        }
        if (!nhunters)
        {
            return;
        }
        int32_t need_crossbow = nhunters + 1 - count.at(stock_item::crossbow);
        if (need_crossbow > 0)
        {
            tmpl.job_type = job_type::MakeWeapon;
            tmpl.item_subtype = manager_subtype.at(stock_item::crossbow);
            tmpl.material_category.bits.bone = 1;
            if (amount > need_crossbow)
                amount = need_crossbow;
        }
        else
        {
            tmpl.job_type = job_type::MakeAmmo;
            tmpl.item_subtype = manager_subtype.at(stock_item::bone_bolts);
            tmpl.material_category.bits.bone = 1;
            int32_t stock = count.at(stock_item::bone_bolts);
            if (amount > 1000 - stock)
                amount = 1000 - stock;
            may_rot();
        }
        break;
    }
    case stock_item::clay:
    {
        input.push_back(stock_item::coal); // TODO: handle magma kilns
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

        if (count[stock_item::barrel] > count[stock_item::rock_pot])
        {
            input.push_back(stock_item::barrel);
        }
        else
        {
            input.push_back(stock_item::rock_pot);
        }

        if (!need_more(stock_item::drink))
        {
            return;
        }
        break;
    }
    case stock_item::food_ingredients:
    {
        tmpl.job_type = job_type::PrepareMeal;
        tmpl.mat_type = 4; // roasts
        amount = (amount + 4) / 5;
        if (!need_more(stock_item::meal))
        {
            return;
        }
        break;
    }
    case stock_item::honey:
    {
        tmpl.job_type = job_type::CustomReaction;
        tmpl.reaction_name = "MAKE_MEAD";
        if (count[stock_item::barrel] > count[stock_item::rock_pot])
        {
            input.push_back(stock_item::barrel);
        }
        else
        {
            input.push_back(stock_item::rock_pot);
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
        queue_use_metal_ore(out, amount);
        return;
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
    case stock_item::raw_adamantine:
    {
        tmpl.job_type = job_type::ExtractMetalStrands;
        tmpl.mat_type = 0;
        tmpl.mat_index = manager_subtype.at(stock_item::raw_adamantine);
        break;
    }
    case stock_item::raw_coke:
    {
        queue_use_raw_coke(out, amount);
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
        queue_use_gems(out, amount);
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
        for (auto i = input.begin(); i != input.end(); i++)
        {
            int32_t c = count.at(*i);
            if (i_amount > c)
                i_amount = c;
            if (c < amount && Watch.Needed.count(*i))
            {
                queue_need(out, *i, amount - c);
            }
        }
        amount = i_amount;
    }

    add_manager_order(out, tmpl, amount);
}

// cut gems
void Stocks::queue_use_gems(color_ostream & out, int32_t amount)
{
    for (auto mo : world->manager_orders)
    {
        if (mo->job_type == job_type::CutGems)
        {
            return;
        }
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
    add_manager_order(out, tmpl, amount);
}

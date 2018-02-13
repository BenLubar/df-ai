#include "ai.h"
#include "stocks.h"
#include "plan.h"

#include "modules/Materials.h"

#include "df/buildings_other_id.h"
#include "df/inorganic_raw.h"
#include "df/item_barst.h"
#include "df/manager_order.h"
#include "df/manager_order_template.h"
#include "df/reaction.h"
#include "df/reaction_product_itemst.h"
#include "df/reaction_reagent_itemst.h"
#include "df/world.h"

REQUIRE_GLOBAL(world);

static bool select_most_abundant_metal(const std::map<int32_t, int32_t> & bars, int32_t & chosen_type)
{
    // pick the metal we have the most of
    chosen_type = std::max_element(bars.begin(), bars.end(), [](std::pair<const int32_t, int32_t> a, std::pair<const int32_t, int32_t> b) -> bool
    {
        return a.second < b.second;
    })->first;
    return true;
}

void Stocks::queue_need_anvil(color_ostream & out)
{
    queue_need_forge(out, material_flags::ITEMS_ANVIL, 3, stock_item::anvil, job_type::ForgeAnvil, &select_most_abundant_metal);
}

void Stocks::queue_need_cage(color_ostream & out)
{
    queue_need_forge(out, material_flags::ITEMS_METAL, 3, stock_item::cage_metal, job_type::MakeCage, &select_most_abundant_metal);
}

void Stocks::queue_need_forge(color_ostream & out, df::material_flags preference, int32_t bars_per_item, stock_item::item item, df::job_type job, std::function<bool(const std::map<int32_t, int32_t> & bars, int32_t & chosen_type)> decide, df::item_type item_type, int16_t item_subtype)
{
    int32_t coal_bars = count.at(stock_item::coal);
    if (!world->buildings.other[buildings_other_id::FURNACE_SMELTER_MAGMA].empty())
        coal_bars = 50000;

    if (!metal_pref.count(preference))
    {
        auto & pref = metal_pref[preference];
        for (size_t mi = 0; mi < world->raws.inorganics.size(); mi++)
        {
            if (world->raws.inorganics[mi]->material.flags.is_set(preference))
            {
                pref.insert(int32_t(mi));
            }
        }
    }
    const auto & pref = metal_pref.at(preference);

    int32_t cnt = Watch.Needed.at(item);
    cnt -= count.at(item);

    for (auto mo : world->manager_orders)
    {
        if (mo->job_type == job && mo->item_type == item_type && mo->item_subtype == item_subtype && mo->material_category.whole == 0)
        {
            cnt -= mo->amount_left;
        }
    }

    std::map<int32_t, int32_t> bars = ingots;

    // rough account of already queued jobs consumption
    for (auto mo : world->manager_orders)
    {
        if (mo->mat_type == 0 && bars.count(mo->mat_index))
        {
            bars[mo->mat_index] -= 4 * mo->amount_left;
            coal_bars -= mo->amount_left;
        }
    }

    std::map<int32_t, int32_t> potential_bars = bars;
    if (ai->plan->should_search_for_metal)
    {
        for (auto mi : pref)
        {
            potential_bars[mi] += may_forge_bars(out, mi);
        }
    }

    std::map<int32_t, int32_t> to_queue;

    while (cnt > 0)
    {
        if (coal_bars < 1)
        {
            break;
        }

        for (auto it = potential_bars.begin(); it != potential_bars.end(); )
        {
            if (!pref.count(it->first) || it->second < bars_per_item)
            {
                it = potential_bars.erase(it);
            }
            else
            {
                it++;
            }
        }

        if (potential_bars.empty())
        {
            break;
        }

        int32_t mat_index;
        if (!decide(potential_bars, mat_index))
        {
            break;
        }

        if (!bars.count(mat_index) || bars.at(mat_index) < bars_per_item)
        {
            break;
        }

        to_queue[mat_index]++;
        potential_bars[mat_index] -= bars_per_item;
        bars[mat_index] -= bars_per_item;
        coal_bars--;
    }

    for (auto q : to_queue)
    {
        df::manager_order_template tmpl;
        tmpl.job_type = job;
        tmpl.item_type = item_type;
        tmpl.item_subtype = item_subtype;
        tmpl.mat_type = 0;
        tmpl.mat_index = q.first;
        add_manager_order(out, tmpl, q.second);
    }
}

// determine if we may be able to generate metal bars for this metal
// may queue manager_jobs to do so
// recursive (eg steel need pig_iron)
// return the potential number of bars available (in dimensions, eg 1 bar => 150)
int32_t Stocks::may_forge_bars(color_ostream & out, int32_t mat_index, int32_t div)
{
    int32_t can_melt = 0;
    for (auto i : world->items.other[items_other_id::BOULDER])
    {
        if (is_metal_ore(i) && simple_metal_ores.at(mat_index).count(i->getMaterialIndex()) && is_item_free(i))
        {
            can_melt++;
        }
    }

    if (can_melt < Watch.WatchStock.at(stock_item::metal_ore) && ai->plan->should_search_for_metal)
    {
        for (auto k : ai->plan->map_veins)
        {
            if (simple_metal_ores.at(mat_index).count(k.first))
            {
                can_melt += ai->plan->dig_vein(out, k.first);
            }
        }
    }

    if (can_melt > Watch.WatchStock.at(stock_item::metal_ore))
    {
        return 4 * 150 * (can_melt - Watch.WatchStock.at(stock_item::metal_ore));
    }

    // "make <mi> bars" customreaction
    for (auto r : world->raws.reactions)
    {
        // XXX choose best reaction from all reactions
        int32_t prod_mult = -1;
        for (auto rp : r->products)
        {
            df::reaction_product_itemst *rpi = virtual_cast<df::reaction_product_itemst>(rp);
            if (rpi && rpi->item_type == item_type::BAR && rpi->mat_type == 0 && rpi->mat_index == mat_index)
            {
                prod_mult = rpi->product_dimension;
                break;
            }
        }
        if (prod_mult == -1)
            continue;

        bool all = true;
        int32_t can_reaction = 30;
        bool future = false;
        for (auto rr : r->reagents)
        {
            // XXX may queue forge reagents[1] even if we dont handle reagents[2]
            df::reaction_reagent_itemst *rri = virtual_cast<df::reaction_reagent_itemst>(rr);
            if (!rri || (rri->item_type != item_type::BAR && rri->item_type != item_type::BOULDER))
            {
                all = false;
                break;
            }
            int32_t has = 0;
            df::items_other_id oidx;
            if (!find_enum_item(&oidx, ENUM_KEY_STR(item_type, rri->item_type)))
            {
                ai->debug(out, "[ERROR] missing items_other_id::" + ENUM_KEY_STR(item_type, rri->item_type));
                all = false;
                break;
            }
            for (auto i : world->items.other[oidx])
            {
                if (rri->mat_type != -1 && i->getMaterial() != rri->mat_type)
                    continue;
                if (rri->mat_index != -1 && i->getMaterialIndex() != rri->mat_index)
                    continue;
                if (!is_item_free(i))
                    continue;
                if (!rri->reaction_class.empty())
                {
                    MaterialInfo mi(i);
                    bool found = false;
                    for (auto c : mi.material->reaction_class)
                    {
                        if (*c == rri->reaction_class)
                        {
                            found = true;
                            break;
                        }
                    }
                    if (!found)
                        continue;
                }
                if (rri->metal_ore != -1 && i->getMaterial() == 0)
                {
                    bool found = false;
                    auto & mis = world->raws.inorganics[i->getMaterialIndex()]->metal_ore.mat_index;
                    for (auto mi : mis)
                    {
                        if (mi == rri->metal_ore)
                        {
                            found = true;
                            break;
                        }
                    }
                    if (!found)
                        continue;
                }
                if (rri->item_type == item_type::BAR)
                {
                    has += virtual_cast<df::item_barst>(i)->dimension;
                }
                else
                {
                    has++;
                }
            }
            if (has <= 0 && rri->item_type == item_type::BOULDER && rri->mat_type == 0 && rri->mat_index != -1 && ai->plan->should_search_for_metal)
            {
                has += ai->plan->dig_vein(out, rri->mat_index);
                if (has > 0)
                    future = true;
            }
            has /= rri->quantity;

            if (has <= 0 && rri->item_type == item_type::BAR && rri->mat_type == 0 && rri->mat_index != -1)
            {
                future = true;
                // 'div' tries to ensure that eg making pig iron wont consume all available iron
                // and leave some to make steel
                has = may_forge_bars(out, rri->mat_index, div + 1);
                if (has == -1)
                {
                    all = false;
                    break;
                }
            }

            if (can_reaction > has / div)
            {
                can_reaction = has / div;
            }
        }
        if (all)
        {
            if (can_reaction <= 0)
            {
                continue;
            }

            if (!future)
            {
                bool found = false;
                for (auto mo : world->manager_orders)
                {
                    if (mo->job_type == job_type::CustomReaction && mo->reaction_name == r->code)
                    {
                        found = true;
                        break;
                    }
                }
                if (!found)
                {
                    df::manager_order_template tmpl;
                    tmpl.job_type = job_type::CustomReaction;
                    tmpl.reaction_name = r->code;
                    tmpl.item_type = item_type::NONE;
                    tmpl.item_subtype = -1;
                    tmpl.mat_type = -1;
                    tmpl.mat_index = -1;
                    add_manager_order(out, tmpl, can_reaction);
                }
            }
            return prod_mult * can_reaction;
        }
    }
    return -1;
}

// smelt metal ores
void Stocks::queue_use_metal_ore(color_ostream & out, int32_t amount)
{
    // make coke from bituminous coal has priority
    if (count.at(stock_item::raw_coke) > Watch.WatchStock.at(stock_item::raw_coke) && count.at(stock_item::coal) < 100)
    {
        return;
    }
    for (auto mo : world->manager_orders)
    {
        if (mo->job_type == job_type::SmeltOre)
        {
            return;
        }
    }

    df::item *base = nullptr;
    for (auto i : world->items.other[items_other_id::BOULDER])
    {
        if (is_metal_ore(i) && is_item_free(i))
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
    for (auto i : world->items.other[items_other_id::BOULDER])
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

    if (world->buildings.other[buildings_other_id::FURNACE_SMELTER_MAGMA].empty())
    {
        if (amount > count.at(stock_item::coal))
            amount = count.at(stock_item::coal);
        if (amount <= 0)
            return;
    }

    df::manager_order_template tmpl;
    tmpl.job_type = job_type::SmeltOre;
    tmpl.item_type = item_type::NONE;
    tmpl.item_subtype = -1;
    tmpl.mat_type = base->getMaterial();
    tmpl.mat_index = base->getMaterialIndex();
    add_manager_order(out, tmpl, amount);
}

// bituminous_coal -> coke
void Stocks::queue_use_raw_coke(color_ostream & out, int32_t amount)
{
    is_raw_coke(0); // populate raw_coke_inv
    for (auto mo : world->manager_orders)
    {
        if (mo->job_type == job_type::CustomReaction && raw_coke_inv.count(mo->reaction_name))
        {
            return;
        }
    }

    std::string reaction;
    df::item *base = nullptr;
    for (auto i : world->items.other[items_other_id::BOULDER])
    {
        reaction = is_raw_coke(i);
        if (!reaction.empty() && is_item_free(i))
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
    for (auto i : world->items.other[items_other_id::BOULDER])
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

    if (world->buildings.other[buildings_other_id::FURNACE_SMELTER_MAGMA].empty())
    {
        // need at least 1 unit of fuel to bootstrap
        if (count.at(stock_item::coal) <= 0)
        {
            return;
        }
    }

    df::manager_order_template tmpl;
    tmpl.job_type = job_type::CustomReaction;
    tmpl.reaction_name = reaction;
    tmpl.item_type = item_type::NONE;
    tmpl.item_subtype = -1;
    tmpl.mat_type = -1;
    tmpl.mat_index = -1;
    add_manager_order(out, tmpl, amount);
}

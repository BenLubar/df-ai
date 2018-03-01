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

static bool select_most_abundant_metal(const std::map<int32_t, int32_t> & potential_bars, const std::map<int32_t, int32_t> & actual_bars, int32_t & chosen_type)
{
    // pick the metal we have the most of
    auto max = std::max_element(actual_bars.begin(), actual_bars.end(), [](std::pair<const int32_t, int32_t> a, std::pair<const int32_t, int32_t> b) -> bool
    {
        return a.second < b.second;
    });

    if (max != actual_bars.end() && max->second > 0)
    {
        chosen_type = max->first;
        return true;
    }

    // fine, then pick a simple metal we can get a lot of
    chosen_type = -1;
    int32_t count = -1;
    for (auto potential : potential_bars)
    {
        extern AI *dwarfAI;
        if (!dwarfAI->stocks->simple_metal_ores.at(potential.first).empty())
        {
            if (count < potential.second)
            {
                chosen_type = potential.first;
                count = potential.second;
            }
        }
    }

    return chosen_type != -1;
}

void Stocks::queue_need_ammo(color_ostream & out, std::ostream & reason)
{
    queue_need_forge(out, material_flags::ITEMS_AMMO, 1, stock_item::ammo, job_type::MakeAmmo, &select_most_abundant_metal, reason, item_type::NONE, min_subtype_for_item(stock_item::ammo), 25);
}

void Stocks::queue_need_anvil(color_ostream & out, std::ostream & reason)
{
    queue_need_forge(out, material_flags::ITEMS_ANVIL, 3, stock_item::anvil, job_type::ForgeAnvil, &select_most_abundant_metal, reason);
}

void Stocks::queue_need_cage(color_ostream & out, std::ostream & reason)
{
    queue_need_forge(out, material_flags::ITEMS_METAL, 3, stock_item::cage_metal, job_type::MakeCage, &select_most_abundant_metal, reason);
}

void Stocks::queue_need_forge(color_ostream & out, df::material_flags preference, int32_t bars_per_item, stock_item::item item, df::job_type job, std::function<bool(const std::map<int32_t, int32_t> & potential_bars, const std::map<int32_t, int32_t> & current_bars, int32_t & chosen_type)> decide, std::ostream & reason, df::item_type item_type, int16_t item_subtype, int32_t items_created_per_job)
{
    int32_t coal_bars = count_free.at(stock_item::coal);
    if (!world->buildings.other[buildings_other_id::WORKSHOP_MAGMA_FORGE].empty())
    {
        coal_bars = 50000;
    }

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

    int32_t cnt = num_needed(item);
    cnt -= count_free.at(item);
    cnt = (cnt + items_created_per_job - 1) / items_created_per_job;

    for (auto mo : world->manager_orders)
    {
        if (mo->job_type == job && mo->item_type == item_type && mo->item_subtype == item_subtype && mo->material_category.whole == 0)
        {
            cnt -= mo->amount_left;
        }
    }
    events.each_exclusive<ManagerOrderExclusive>([&cnt, job, item_type, item_subtype](const ManagerOrderExclusive *excl) -> bool
    {
        if (excl->tmpl.job_type == job && excl->tmpl.item_type == item_type && excl->tmpl.item_subtype == item_subtype && excl->tmpl.material_category.whole == 0)
        {
            cnt -= excl->amount;
        }
        return false;
    });

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
    events.each_exclusive<ManagerOrderExclusive>([&bars, &coal_bars](const ManagerOrderExclusive *excl) -> bool
    {
        if (excl->tmpl.mat_type == 0 && bars.count(excl->tmpl.mat_index))
        {
            bars[excl->tmpl.mat_index] -= excl->amount;
            coal_bars -= excl->amount;
        }
        return false;
    });

    std::map<int32_t, int32_t> potential_bars = bars;
    if (ai->plan->should_search_for_metal)
    {
        std::ofstream discard;
        for (auto mi : pref)
        {
            potential_bars[mi] += may_forge_bars(out, mi, discard, 1, true) / 150;
        }
    }

    std::map<int32_t, int32_t> to_queue;

    while (cnt > 0)
    {
        if (coal_bars < 1)
        {
            reason << "not enough coal\n";
        }

        for (auto it = bars.begin(); it != bars.end(); )
        {
            if (!pref.count(it->first))
            {
                it = bars.erase(it);
            }
            else
            {
                it++;
            }
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
            reason << (ai->plan->should_search_for_metal ? "no viable ores available\n" : "not ready to mine ores\n");
            break;
        }

        int32_t mat_index;
        if (!decide(potential_bars, bars, mat_index))
        {
            reason << "not enough metal\n";
            break;
        }

        if (!bars.count(mat_index) || bars.at(mat_index) < bars_per_item)
        {
            reason << "not enough " << MaterialInfo(0, mat_index).toString() << ":\n";
            may_forge_bars(out, mat_index, reason, bars_per_item);
            break;
        }

        if (coal_bars < 1)
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
        reason << "\n";
        add_manager_order(out, tmpl, q.second, reason);
    }
}

// determine if we may be able to generate metal bars for this metal
// may queue manager_jobs to do so
// recursive (eg steel need pig_iron)
// return the potential number of bars available (in dimensions, eg 1 bar => 150)
int32_t Stocks::may_forge_bars(color_ostream & out, int32_t mat_index, std::ostream & reason, int32_t div, bool dry_run)
{
    std::set<int32_t> waiting_for_smelting;

    int32_t can_melt = 0;
    for (auto i : world->items.other[items_other_id::BOULDER])
    {
        if (is_metal_ore(i) && simple_metal_ores.at(mat_index).count(i->getMaterialIndex()) && is_item_free(i))
        {
            waiting_for_smelting.insert(i->getMaterialIndex());
            can_melt++;
        }
    }

    if ((can_melt <= Watch.WatchStock.at(stock_item::metal_ore) && ai->plan->should_search_for_metal) || dry_run)
    {
        for (auto & k : ai->plan->map_veins)
        {
            if (simple_metal_ores.at(mat_index).count(k.first))
            {
                can_melt += dry_run ? ai->plan->can_dig_vein(k.first) : ai->plan->dig_vein(out, k.first);

                reason << "mining more " << MaterialInfo(0, k.first).toString() << "\n";
            }
        }
    }

    if (can_melt > Watch.WatchStock.at(stock_item::metal_ore) && !dry_run)
    {
        for (auto mat : waiting_for_smelting)
        {
            reason << "waiting for " << MaterialInfo(0, mat).toString() << " to be smelted\n";
        }
        return 4 * 150 * (can_melt - Watch.WatchStock.at(stock_item::metal_ore));
    }

    std::string best_reaction;
    int32_t best_reaction_count = -1;
    int32_t best_reaction_output = -1;

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
        {
            continue;
        }

        bool all = true;
        int32_t can_reaction = dry_run ? std::numeric_limits<int32_t>::max() : 30;
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

            if (((has <= 0 && ai->plan->should_search_for_metal) || dry_run) && rri->item_type == item_type::BOULDER)
            {
                if (rri->mat_type == 0 && rri->mat_index != -1)
                {
                    has += dry_run ? ai->plan->can_dig_vein(rri->mat_index) : ai->plan->dig_vein(out, rri->mat_index);
                }
                else if (rri->metal_ore != -1)
                {
                    for (auto mat : simple_metal_ores.at(rri->metal_ore))
                    {
                        has += dry_run ? ai->plan->can_dig_vein(mat) : ai->plan->dig_vein(out, mat);
                    }
                }
                if (has > 0)
                    future = true;
            }

            if ((has < rri->quantity || dry_run) && rri->item_type == item_type::BAR && rri->mat_type == 0 && rri->mat_index != -1)
            {
                future = true;
                // 'div' tries to ensure that eg making pig iron wont consume all available iron
                // and leave some to make steel
                reason << "preparing to smelt " << MaterialInfo(0, mat_index).toString() << ":\n";
                has += may_forge_bars(out, rri->mat_index, reason, div + 1, dry_run);
                if (has < rri->quantity)
                {
                    all = false;
                    break;
                }
            }

            has /= rri->quantity;

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

            if (!future && !dry_run)
            {
                bool already_making = false;
                for (auto mo : world->manager_orders)
                {
                    if (mo->job_type == job_type::CustomReaction && mo->reaction_name == r->code)
                    {
                        reason << "already smelting " << MaterialInfo(0, mat_index).toString() << ": " << AI::describe_job(mo) << " (" << mo->amount_left << " remaining)";
                        already_making = true;
                        break;
                    }
                }

                if (!already_making)
                {
                    already_making = events.each_exclusive<ManagerOrderExclusive>([&reason, r, mat_index](const ManagerOrderExclusive *excl) -> bool
                    {
                        if (excl->tmpl.job_type == job_type::CustomReaction && excl->tmpl.reaction_name == r->code)
                        {
                            reason << "already smelting " << MaterialInfo(0, mat_index).toString() << ": " << AI::describe_job(&excl->tmpl) << " (" << excl->amount << " remaining)";
                            return true;
                        }

                        return false;
                    });
                }

                if (already_making)
                {
                    return prod_mult * can_reaction;
                }
            }

            if (!future || dry_run)
            {
                if (prod_mult * can_reaction > best_reaction_output)
                {
                    best_reaction = r->code;
                    best_reaction_count = can_reaction;
                    best_reaction_output = prod_mult * can_reaction;
                }
            }
        }
    }

    if (dry_run)
    {
        int32_t result = std::max(4 * 150 * can_melt, 0) + std::max(best_reaction_output, 0);
        return result ? result : -1;
    }

    if (!best_reaction.empty())
    {
        df::manager_order_template tmpl;
        tmpl.job_type = job_type::CustomReaction;
        tmpl.reaction_name = best_reaction;
        tmpl.item_type = item_type::NONE;
        tmpl.item_subtype = -1;
        tmpl.mat_type = -1;
        tmpl.mat_index = -1;
        reason << "smelting " << MaterialInfo(0, mat_index).toString() << ": ";
        add_manager_order(out, tmpl, best_reaction_count, reason);
        reason << "\n";

        return best_reaction_output;
    }

    reason << "cannot produce " << MaterialInfo(0, mat_index).toString() << "\n";
    return -1;
}

// smelt metal ores
void Stocks::queue_use_metal_ore(color_ostream & out, int32_t amount, std::ostream & reason)
{
    // make coke from bituminous coal has priority if there isn't a magma smelter
    if (world->buildings.other[buildings_other_id::FURNACE_SMELTER_MAGMA].empty() &&
        count_free.at(stock_item::raw_coke) > Watch.WatchStock.at(stock_item::raw_coke) &&
        count_free.at(stock_item::coal) < 100)
    {
        reason << "making coal instead";
        return;
    }

    for (auto mo : world->manager_orders)
    {
        if (mo->job_type == job_type::SmeltOre)
        {
            reason << "already smelting ore: " << AI::describe_job(mo) << " (" << mo->amount_left << " remaining)";
            return;
        }
    }
    if (events.each_exclusive<ManagerOrderExclusive>([&reason](const ManagerOrderExclusive *excl) -> bool
    {
        if (excl->tmpl.job_type == job_type::SmeltOre)
        {
            reason << "already smelting ore: " << AI::describe_job(&excl->tmpl) << " (" << excl->amount << " remaining)";
            return true;
        }
        return false;
    }))
    {
        return;
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
        reason << "could not find metal ore";
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
        amount = std::min(amount, count_free.at(stock_item::coal));
        if (amount <= 0)
        {
            reason << "not enough coal";
            return;
        }
    }

    df::manager_order_template tmpl;
    tmpl.job_type = job_type::SmeltOre;
    tmpl.item_type = item_type::NONE;
    tmpl.item_subtype = -1;
    tmpl.mat_type = base->getMaterial();
    tmpl.mat_index = base->getMaterialIndex();
    add_manager_order(out, tmpl, amount, reason);
}

// bituminous_coal -> coke
void Stocks::queue_use_raw_coke(color_ostream & out, int32_t amount, std::ostream & reason)
{
    is_raw_coke(0); // populate raw_coke_inv
    for (auto mo : world->manager_orders)
    {
        if (mo->job_type == job_type::CustomReaction && raw_coke_inv.count(mo->reaction_name))
        {
            reason << "already using raw coke: " << AI::describe_job(mo) << " (" << mo->amount_left << " remaining)";
            return;
        }
    }
    if (events.each_exclusive<ManagerOrderExclusive>([this, &reason](const ManagerOrderExclusive *excl) -> bool
    {
        if (excl->tmpl.job_type == job_type::CustomReaction && raw_coke_inv.count(excl->tmpl.reaction_name))
        {
            reason << "already using raw coke: " << AI::describe_job(&excl->tmpl) << " (" << excl->amount << " remaining)";
            return true;
        }
        return false;
    }))
    {
        return;
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
        reason << "could not find raw coke";
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
        if (count_free.at(stock_item::coal) <= 0)
        {
            reason << "need coal or charcoal";
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
    add_manager_order(out, tmpl, amount, reason);
}

#include "ai.h"
#include "stocks.h"
#include "population.h"

#include "modules/Buildings.h"
#include "modules/Gui.h"
#include "modules/Maps.h"

#include "df/building_slabst.h"
#include "df/historical_figure.h"
#include "df/item_plant_growthst.h"
#include "df/item_plantst.h"
#include "df/item_slabst.h"
#include "df/manager_order.h"
#include "df/tile_occupancy.h"
#include "df/ui.h"
#include "df/unit.h"
#include "df/viewscreen_overallstatusst.h"
#include "df/world.h"

REQUIRE_GLOBAL(cur_year);
REQUIRE_GLOBAL(cur_year_tick);
REQUIRE_GLOBAL(ui);
REQUIRE_GLOBAL(world);

void Stocks::update(color_ostream & out)
{
    if (!updating.empty() && lastupdating != updating.size() + updating_count.size())
    {
        // avoid stall if cb_bg crashed and was unregistered
        ai.debug(out, "not updating stocks");
        lastupdating = updating.size() + updating_count.size();
        return;
    }

    if (last_unforbidall_year != *cur_year)
    {
        last_unforbidall_year = *cur_year;
        for (auto i : world->items.all)
        {
            i->flags.bits.forbid = 0;
        }
    }

    // trim stalled manager orders once per month
    if (last_managerstall != *cur_year_tick / 28 / 1200)
    {
        last_managerstall = *cur_year_tick / 28 / 1200;
        if (!world->manager_orders.empty())
        {
            auto m = world->manager_orders.front();
            if (m->status.bits.validated)
            {
                if (m->job_type == last_managerorder)
                {
                    if (m->amount_left > 3)
                    {
                        m->amount_left -= 3;
                        m->amount_total -= 3;
                    }
                    else
                    {
                        world->manager_orders.erase(world->manager_orders.begin());
                        delete m;
                    }
                }
                else
                {
                    last_managerorder = m->job_type;
                }
            }
        }
    }

    updating.clear();
    for (auto it = Watch.Needed.begin(); it != Watch.Needed.end(); it++)
    {
        updating.push_back(it->first);
    }
    for (auto it = Watch.WatchStock.begin(); it != Watch.WatchStock.end(); it++)
    {
        updating.push_back(it->first);
    }
    updating_count = updating;
    updating_count.insert(updating_count.end(), Watch.AlsoCount.begin(), Watch.AlsoCount.end());
    updating_seeds = true;
    updating_plants = true;
    updating_corpses = true;
    updating_slabs = true;
    updating_ingots = true;
    updating_farmplots.clear();

    ai.find_room(room_type::farmplot, [this](room *r) -> bool
    {
        if (r->dfbuilding())
        {
            updating_farmplots.push_back(r);
        }
        return false; // search all farm plots
    });

    if (ai.eventsJson.is_open())
    {
        // update wealth by opening the status screen
        Gui::getCurViewscreen(true)->feed_key(interface_key::D_STATUS);
        if (auto view = strict_virtual_cast<df::viewscreen_overallstatusst>(Gui::getCurViewscreen(true)))
        {
            // only leave if we're on the status screen
            view->feed_key(interface_key::LEAVESCREEN);
        }
        Json::Value payload(Json::objectValue);
        payload["total"] = Json::Int(ui->tasks.wealth.total);
        payload["weapons"] = Json::Int(ui->tasks.wealth.weapons);
        payload["armor"] = Json::Int(ui->tasks.wealth.armor);
        payload["furniture"] = Json::Int(ui->tasks.wealth.furniture);
        payload["other"] = Json::Int(ui->tasks.wealth.other);
        payload["architecture"] = Json::Int(ui->tasks.wealth.architecture);
        payload["displayed"] = Json::Int(ui->tasks.wealth.displayed);
        payload["held"] = Json::Int(ui->tasks.wealth.held);
        payload["imported"] = Json::Int(ui->tasks.wealth.imported);
        payload["exported"] = Json::Int(ui->tasks.wealth.exported);
        ai.event("wealth", payload);
    }

    // do stocks accounting 'in the background' (ie one bit at a time)
    events.onupdate_register_once("df-ai stocks bg", [this](color_ostream & out) -> bool
    {
        if (updating_seeds)
        {
            count_seeds(out);
            return false;
        }
        if (updating_plants)
        {
            count_plants(out);
            return false;
        }
        if (updating_corpses)
        {
            update_corpses(out);
            return false;
        }
        if (updating_slabs)
        {
            update_slabs(out);
            return false;
        }
        if (updating_ingots)
        {
            update_ingots(out);
            return false;
        }
        if (!updating_count.empty())
        {
            stock_item::item key = updating_count.back();
            updating_count.pop_back();
            count_stocks(out, key);
            return false;
        }
        if (!updating.empty())
        {
            for (auto key : updating)
            {
                act(out, key);
            }
            updating.clear();
            return false;
        }
        if (!updating_farmplots.empty())
        {
            room *r = updating_farmplots.back();
            updating_farmplots.pop_back();
            farmplot(out, r, false);
            return false;
        }
        for (auto job : world->jobs.list)
        {
            if (job->flags.bits.do_now)
            {
                continue;
            }

            if (need_more(stock_item::drink) && job->job_type == job_type::CustomReaction && (job->reaction_name == "BREW_DRINK_FROM_PLANT" || job->reaction_name == "BREW_DRINK_FROM_PLANT_GROWTH" || job->reaction_name == "MAKE_MEAD"))
            {
                // booze is vital
                job->flags.bits.do_now = true;
                continue;
            }

            if (need_more(stock_item::meal) && job->job_type == job_type::PrepareMeal)
            {
                // food is pretty important too
                job->flags.bits.do_now = true;
                continue;
            }

            if (job->job_type == job_type::LinkBuildingToTrigger || job->job_type == job_type::PullLever)
            {
                // freeing people is high priority
                job->flags.bits.do_now = true;
                continue;
            }

            if (job->job_type == job_type::PenLargeAnimal || job->job_type == job_type::BringItemToDepot || job->job_type == job_type::StoreItemInHospital || job->job_type == job_type::DumpItem)
            {
                // high priority hauling jobs
                job->flags.bits.do_now = true;
                continue;
            }
        }
        if (ai.eventsJson.is_open())
        {
            std::ostringstream stringify;
            Json::Value payload(Json::objectValue);
            for (auto it = Watch.Needed.begin(); it != Watch.Needed.end(); it++)
            {
                Json::Value needed(Json::arrayValue);
                needed.append(count_free.at(it->first));
                needed.append(num_needed(it->first));
                needed.append(count_total.at(it->first));
                stringify.str(std::string());
                stringify.clear();
                stringify << it->first;
                payload[stringify.str()] = needed;
            }
            for (auto it = Watch.WatchStock.begin(); it != Watch.WatchStock.end(); it++)
            {
                Json::Value watch(Json::arrayValue);
                watch.append(count_free.at(it->first));
                watch.append(-it->second);
                watch.append(count_total.at(it->first));
                stringify.str(std::string());
                stringify.clear();
                stringify << it->first;
                payload[stringify.str()] = watch;
            }
            for (auto it = Watch.AlsoCount.begin(); it != Watch.AlsoCount.end(); it++)
            {
                Json::Value also(Json::arrayValue);
                also.append(count_free.at(*it));
                also.append(0);
                also.append(count_total.at(*it));
                stringify.str(std::string());
                stringify.clear();
                stringify << *it;
                payload[stringify.str()] = also;
            }
            ai.event("stocks update", payload);
        }
        // finished, dismiss callback
        return true;
    });
}

void Stocks::count_plants(color_ostream &)
{
    plants.clear();
    for (auto it = world->items.other[items_other_id::PLANT].begin(); it != world->items.other[items_other_id::PLANT].end(); it++)
    {
        df::item_plantst *p = virtual_cast<df::item_plantst>(*it);
        if (p && is_item_free(p))
        {
            plants[p->mat_index] += p->stack_size;
        }
    }
    for (auto it = world->items.other[items_other_id::PLANT_GROWTH].begin(); it != world->items.other[items_other_id::PLANT_GROWTH].end(); it++)
    {
        df::item_plant_growthst *p = virtual_cast<df::item_plant_growthst>(*it);
        if (p && is_item_free(p))
        {
            plants[p->mat_index] += p->stack_size;
        }
    }
    updating_plants = false;
}

void Stocks::update_ingots(color_ostream & out)
{
    std::ofstream discard;
    for (int32_t mat_index = 0; mat_index < int32_t(world->raws.inorganics.size()); mat_index++)
    {
        if (may_forge_bars(out, mat_index, discard, 1, true) >= 150)
        {
            ingots[mat_index] = 0;
        }
    }

    // Set to 0 instead of clearing so metals stay in
    // the report after we use them all up.
    for (auto & i : ingots)
    {
        i.second = 0;
    }
    for (auto i : world->items.other[items_other_id::BAR])
    {
        if (i->getMaterial() == 0 && is_item_free(i))
        {
            ingots[i->getMaterialIndex()] += i->getStackSize();
        }
    }
    updating_ingots = false;
}

void Stocks::update_corpses(color_ostream & out)
{
    room *r = ai.find_room(room_type::garbagedump);
    if (!r)
    {
        updating_corpses = false;
        return;
    }
    df::coord t = r->min - df::coord(0, 0, 1);

    for (auto it = world->items.other[items_other_id::ANY_CORPSE].begin(); it != world->items.other[items_other_id::ANY_CORPSE].end(); it++)
    {
        df::item *i = *it;
        int16_t race = -1;
        int16_t caste = -1;
        df::historical_figure *hf = nullptr;
        df::unit *u = nullptr;
        i->getCorpseInfo(&race, &caste, &hf, &u);
        if (is_item_free(i) &&
            i->flags.bits.on_ground && // ignore corpses that are not on the ground
            !i->flags.bits.in_inventory && // ignore corpses in inventories even if they're being hauled
            !i->flags.bits.in_building && // ignore corpses in buildings even if they're not construction materials
            i->pos != t &&
            !(Maps::getTileOccupancy(i->pos)->bits.building == tile_building_occ::Passable && virtual_cast<df::building_stockpilest>(Buildings::findAtTile(i->pos))))
        {
            if (!i->flags.bits.dump && u)
            {
                ai.debug(out, "stocks: dump " + enum_item_key(i->getType()) + " of " + AI::describe_unit(u) + " (" + AI::describe_item(i) + ")");
            }
            // dump corpses that aren't in a stockpile, a grave, or the dump.
            i->flags.bits.dump = true;
        }
        else if (i->pos == t)
        {
            if (i->flags.bits.forbid && u)
            {
                ai.debug(out, "stocks: unforbid " + enum_item_key(i->getType()) + " of " + AI::describe_unit(u) + " (" + AI::describe_item(i) + ")");
            }
            // unforbid corpses in the dump so dwarves get buried before the next year.
            i->flags.bits.forbid = false;
        }
    }
    updating_corpses = false;
}

void Stocks::update_slabs(color_ostream & out)
{
    for (auto it = world->items.other[items_other_id::SLAB].begin(); it != world->items.other[items_other_id::SLAB].end(); it++)
    {
        df::item_slabst *i = virtual_cast<df::item_slabst>(*it);
        if (is_item_free(i) && i->engraving_type == slab_engraving_type::Memorial)
        {
            df::coord pos;
            pos.clear();

            ai.find_room(room_type::cemetery, [&pos](room *r) -> bool
            {
                if (r->status == room_status::plan)
                    return false;
                for (int16_t x = r->min.x; x <= r->max.x; x++)
                {
                    for (int16_t y = r->min.y; y <= r->max.y; y++)
                    {
                        if (ENUM_ATTR(tiletype_shape, basic_shape, ENUM_ATTR(tiletype, shape, *Maps::getTileType(x, y, r->min.z))) == tiletype_shape_basic::Floor && Maps::getTileOccupancy(x, y, r->min.z)->bits.building == tile_building_occ::None)
                        {
                            df::coord t(x, y, r->min.z);
                            bool any = false;
                            for (auto f : r->layout)
                            {
                                if (r->min + f->pos == t)
                                {
                                    any = true;
                                    break;
                                }
                            }
                            if (!any)
                            {
                                pos = t;
                                return true;
                            }
                        }
                    }
                }
                return false;
            });

            if (pos.isValid())
            {
                df::building_slabst *bld = virtual_cast<df::building_slabst>(Buildings::allocInstance(pos, building_type::Slab));
                Buildings::setSize(bld, df::coord(1, 1, 1));
                std::vector<df::item *> item;
                item.push_back(i);
                Buildings::constructWithItems(bld, item);
                ai.debug(out, "slabbing " + AI::describe_unit(df::unit::find(df::historical_figure::find(i->topic)->unit_id)) + ": " + i->description);
            }
        }
    }
    updating_slabs = false;
}

int32_t Stocks::num_needed(stock_item::item key)
{
    int32_t amount = Watch.Needed.at(key);
    if (Watch.NeededPerDwarf.count(key))
    {
        amount += int32_t(ai.pop.citizen.size()) * Watch.NeededPerDwarf.at(key) / 100;
    }
    return amount;
}

void Stocks::act(color_ostream & out, stock_item::item key)
{
    if (Watch.Needed.count(key))
    {
        act_reason[key].str(std::string());
        if (need_more(key))
        {
            queue_need(out, key, num_needed(key) * 3 / 2 - count_free.at(key), act_reason[key]);
        }
    }

    if (Watch.WatchStock.count(key))
    {
        act_reason[key].str(std::string());

        int32_t amount = Watch.WatchStock.at(key);
        if (count_free.at(key) > amount)
        {
            queue_use(out, key, count_free.at(key) - amount, act_reason[key]);
        }
    }
}

// count unused stocks of one type of item
void Stocks::count_stocks(color_ostream & out, stock_item::item k)
{
    auto helper = find_item_helper(k);

    if (helper.count_min_subtype || !helper.subtypes.empty())
    {
        count_stocks_subtype(out, k, helper);
        return;
    }

    int32_t total = 0;
    int32_t free = 0;
    for (auto i : world->items.other[helper.oidx])
    {
        if (helper.pred(i))
        {
            int32_t c = helper.count(i);
            total += c;
            if (helper.free(i))
            {
                free += c;
            }
        }
    }

    count_free[k] = free;
    count_total[k] = total;
}

void Stocks::count_stocks_subtype(color_ostream &, stock_item::item k, find_item_info & helper)
{
    std::map<int16_t, std::pair<int32_t, int32_t>> subtype;
    for (auto i : helper.subtypes)
    {
        subtype[i] = std::pair<int32_t, int32_t>();
    }

    for (auto i : world->items.other[helper.oidx])
    {
        if (helper.pred(i))
        {
            int16_t st = i->getSubtype();
            int32_t c = helper.count(i);
            subtype[st].second += c;
            if (helper.free(i))
            {
                subtype[st].first += c;
            }
        }
    }

    count_subtype[k] = subtype;

    if (helper.count_min_subtype)
    {
        auto min = std::min_element(subtype.begin(), subtype.end(), [](std::pair<const int16_t, std::pair<int32_t, int32_t>> a, std::pair<const int16_t, std::pair<int32_t, int32_t>> b) -> bool
        {
            return a.second.first < b.second.first;
        });

        count_free[k] = min == subtype.end() ? 0 : min->second.first;
        count_total[k] = min == subtype.end() ? 0 : min->second.second;
    }
    else
    {
        count_free[k] = 0;
        count_total[k] = 0;
        for (auto st : subtype)
        {
            count_free[k] += st.second.first;
            count_total[k] += st.second.second;
        }
    }
}
